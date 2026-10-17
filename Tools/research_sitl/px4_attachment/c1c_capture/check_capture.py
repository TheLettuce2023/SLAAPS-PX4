#!/usr/bin/env python3
"""Independent C1C-S2 receipt replay. No admission or flight-readiness claim."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re

MAX64 = (1 << 64) - 1
WINDOW_US = 2_000_000
NAV_STATES = {0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 13, 14, 15, 17, 18,
              19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30}
ANSI = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")
IDENTITIES = "producer_lifetime_id observer_lifetime_id clock_epoch"


class CaptureError(ValueError):
    """Retained evidence does not satisfy the finite capture contract."""


def require(condition, message):
    if not condition:
        raise CaptureError(message)


def fields(value, names):
    require(type(value) is dict and set(value) == set(names.split()),
            "Missing or unknown object fields: " + names)


def unique(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, "Duplicate JSON key: " + key)
        result[key] = value
    return result


def decode(raw):
    def invalid(value):
        raise CaptureError("Nonfinite JSON value: " + value)
    try:
        return json.loads(raw, object_pairs_hook=unique, parse_constant=invalid)
    except (UnicodeError, json.JSONDecodeError) as error:
        raise CaptureError("Malformed or truncated JSON") from error


def number(value, name, low=0, high=MAX64):
    require(type(value) is int and low <= value <= high, "Invalid integer " + name)
    return value


def u64(value, name, low=0):
    require(type(value) is str and re.fullmatch(r"0|[1-9][0-9]*", value) is not None,
            "Noncanonical decimal " + name)
    require(len(value) <= 20, "Out-of-range decimal " + name)
    return number(int(value), name, low)


def identities(value):
    fields(value, IDENTITIES)
    return {key: u64(value[key], key, 1) for key in IDENTITIES.split()}


def state(value):
    fields(value, "arming_state nav_state")
    require(number(value["arming_state"], "arming_state", 1, 2) == 1,
            "Actual capture is not disarmed")
    require(number(value["nav_state"], "nav_state", 0, 30) in NAV_STATES,
            "Reserved navigation state")
    return (value["arming_state"], value["nav_state"])


def check_trace(rows, expected_ids, start_ns, end_ns):
    """Replay source continuity from raw evidence, never producer pass flags."""
    require(type(rows) is list and len(rows) >= 4, "Incomplete capture")
    header = rows[0]
    fields(header, "type schema " + IDENTITIES +
           " receipt_queue_depth collector_capacity window_us clock_source")
    require(header["type"] == "bootstrap" and header["schema"] == "research-c1c-capture/v1",
            "Missing bootstrap or unsupported schema")
    actual_ids = identities({key: header[key] for key in IDENTITIES.split()})
    require(actual_ids == expected_ids, "Bootstrap/run identity mismatch")
    require(number(header["receipt_queue_depth"], "queue depth") == 32 and
            number(header["collector_capacity"], "collector capacity") == 4096,
            "Unsupported queue or collector capacity")
    require(u64(header["window_us"], "window") == WINDOW_US, "Wrong capture window")
    require(header["clock_source"] == "px4_hrt_absolute_time_native_posix",
            "Missing native source/observer clock provenance")
    completion = rows[-1]
    fields(completion, "type collector_fault receipt_count summary_count final_drain_complete")
    require(completion["type"] == "completion" and completion["collector_fault"] == "None" and
            completion["final_drain_complete"] is True, "Incomplete or failed collector")
    require(number(completion["summary_count"], "summary count") == 1,
            "Missing or duplicate producer summary")
    count = number(completion["receipt_count"], "receipt count", 1, 4096)
    require(count == len(rows) - 3, "Receipt inventory mismatch")
    sequence = epoch = producer_time = receive_time = 0
    previous_state = None
    first_time = None
    ages = []

    def timestamp(value, name):
        stamp = u64(value, name, 1)
        require(start_ns <= stamp * 1000 + 999 and stamp * 1000 <= end_ns,
                "Timestamp outside native process lifetime: " + name)
        return stamp

    for row in rows[1:-2]:
        fields(row, "type observer_lifetime_id receive_time_us receipt")
        require(row["type"] == "receipt", "Unexpected row or duplicate Initial/summary")
        require(u64(row["observer_lifetime_id"], "observer", 1) == expected_ids["observer_lifetime_id"],
                "Observer lifetime changed")
        receipt = row["receipt"]
        fields(receipt, "producer_lifetime_id clock_epoch sequence transition_epoch producer_time_us kind before after")
        for key in ("producer_lifetime_id", "clock_epoch"):
            require(u64(receipt[key], key, 1) == expected_ids[key], "Source lifetime changed")
        current_sequence = u64(receipt["sequence"], "sequence", 1)
        current_epoch = u64(receipt["transition_epoch"], "transition epoch")
        source = timestamp(receipt["producer_time_us"], "producer time")
        received = timestamp(row["receive_time_us"], "receive time")
        require(current_sequence == sequence + 1, "Lost, duplicate, replayed or reordered receipt")
        require(source >= producer_time and received >= receive_time and received >= source,
                "Source/receive time ordering violated")
        before, after = state(receipt["before"]), state(receipt["after"])
        kind = receipt["kind"]
        require(type(kind) is str and kind in {"Initial", "Transition", "Sample"}, "Unknown receipt kind")
        if sequence == 0:
            require(kind == "Initial" and current_epoch == 1 and before == after, "Missing valid Initial")
            first_time = source
        else:
            require(kind != "Initial" and before == previous_state, "Repeated Initial or before-state contradiction")
            changed = before != after
            require((kind == "Transition") == changed, "Omitted or fabricated transition")
            require(current_epoch == epoch + int(changed), "Transition epoch contradiction")
        sequence, epoch = current_sequence, current_epoch
        producer_time, receive_time, previous_state = source, received, after
        ages.append(received - source)

    row = rows[-2]
    fields(row, "type receive_time_us summary")
    require(row["type"] == "summary", "Missing final producer summary")
    summary = row["summary"]
    fields(summary, "identities attempted_sequence emitted_sequence transition_epoch producer_time_us final_time_us "
           "retained_state actual_state has_state finished first_fault emitted_state emitted_transition_epoch emitted_producer_time_us")
    require(identities(summary["identities"]) == expected_ids, "Summary lifetime changed")
    require(summary["has_state"] is True and summary["finished"] is True and summary["first_fault"] == "None",
            "Producer failed or summary incomplete")
    require(u64(summary["attempted_sequence"], "attempted sequence") == sequence ==
            u64(summary["emitted_sequence"], "emitted sequence"), "Missing tail or failed publication")
    require(u64(summary["transition_epoch"], "summary epoch") == epoch ==
            u64(summary["emitted_transition_epoch"], "emitted epoch"), "Summary epoch mismatch")
    require(u64(summary["producer_time_us"], "summary source time") == producer_time ==
            u64(summary["emitted_producer_time_us"], "emitted source time"), "Summary source time mismatch")
    require(state(summary["retained_state"]) == previous_state == state(summary["actual_state"]) ==
            state(summary["emitted_state"]), "Summary hides final state change")
    final = timestamp(summary["final_time_us"], "final source time")
    received = timestamp(row["receive_time_us"], "summary receive time")
    require(producer_time <= final <= received and receive_time <= received,
            "Summary source/receive ordering violated")
    require(final - first_time >= WINDOW_US, "Capture window incomplete")
    return {"receipt_count": sequence, "transition_count": epoch - 1, "source_span_us": final - first_time,
            "min_receipt_age_us": min(ages), "max_receipt_age_us": max(ages),
            "summary_age_us": received - final}


def check_topics(raw):
    lines = [ANSI.sub("", line).strip() for line in raw.decode("utf-8").splitlines()]
    positions = {}
    for marker in ("C1C_TOPICS_BEGIN", "C1C_TOPICS_END", "C1C_STARTUP_COMPLETE"):
        found = [index for index, line in enumerate(lines) if line == marker]
        require(len(found) == 1, "Missing or duplicate marker " + marker)
        positions[marker] = found[0]
    begin, end, complete = (positions[key] for key in positions)
    require(begin < end < complete, "Completion/topic audit ordering violated")
    snapshot = [line for line in lines[begin + 1:end] if line]
    require(snapshot and snapshot[0].split() == ["TOPIC", "NAME", "INST", "#SUB", "#Q", "SIZE", "PATH"],
            "Missing topic header")
    allowed = {"actuator_armed", "config_overrides", "event", "failsafe_flags", "failure_detector_status",
               "health_report", "led_control", "log_message", "mavlink_log", "parameter_update",
               "power_button_state", "tune_control", "vehicle_control_mode", "vehicle_status",
               "research_state_receipt", "research_state_summary"}
    topics = set()
    for line in snapshot[1:]:
        match = re.fullmatch(r"([a-z][a-z0-9_]*)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\S+)", line)
        require(match is not None, "Malformed topic audit")
        name, instance, subscribers, queue, size, path = match.groups()
        require(name in allowed and int(instance) == 0, "Unapproved topic or instance " + name)
        identity = (name, int(instance))
        require(identity not in topics and int(queue) > 0 and int(size) > 0, "Invalid/duplicate topic")
        if name in {"research_state_receipt", "research_state_summary"}:
            require(int(queue) == (32 if name == "research_state_receipt" else 2),
                    "Capture topic queue depth contradicts contract")
        topics.add(identity)
    # This audit follows Commander destruction: Publication.cpp unadvertises
    # queue-one publishers, and DeviceNode::print_statistics omits those nodes.
    # Conventional state topics are therefore allowed, not required. Only the
    # retained queued capture topics must survive here. This snapshot proves
    # neither all-run topic absence nor conventional-state publication coverage.
    require({("research_state_receipt", 0), ("research_state_summary", 0)} <= topics,
            "Missing required capture topics")
    require(not any("wrong type passed to param_get()" in line or
                    re.search(r"\[parameters\] get: param \d+ invalid", line) for line in lines),
            "Unresolved Commander parameter dependency")
    return sorted(name for name, _ in topics)


def check_run(runtime):
    runtime = Path(runtime).resolve()
    record = decode((runtime / "run.json").read_bytes())
    fields(record, "schema command instance runtime pid " + IDENTITIES +
           " start_monotonic_ns end_monotonic_ns exit_code forced_shutdown stdout_sha256 capture_sha256 startup_sha256 admission research_network status")
    require(record["schema"] == "research-c1c-run/v1", "Unsupported run schema")
    require(type(record["runtime"]) is str and Path(record["runtime"]).resolve() == runtime, "Wrong runtime")
    number(record["pid"], "PID", 1)
    instance = number(record["instance"], "instance", 1)
    command = record["command"]
    require(type(command) is list and len(command) == 7 and all(type(arg) is str for arg in command),
            "Invalid process command")
    require(command[1:5] == ["-i", str(instance), "-w", str(runtime)] and command[5] == "-s",
            "Unexpected process invocation")
    startup = runtime / "startup.sh"
    require(startup.is_file() and not startup.is_symlink(), "Staged startup is missing or not a regular owned file")
    require(Path(command[0]).is_absolute() and Path(command[0]).resolve() == (runtime.parent / "build/bin/px4").resolve() and
            Path(command[6]).is_absolute() and Path(command[6]).resolve() == startup,
            "Executable/startup command is not bound to owned capture")
    require(number(record["exit_code"], "exit code") == 0 and record["forced_shutdown"] is False,
            "Unsuccessful or forced process shutdown")
    require(record["admission"] == record["research_network"] == "not_instantiated", "Out-of-scope run")
    require(record["status"] == "captured_unscored", "Run is incomplete")
    start = number(record["start_monotonic_ns"], "process start", 1)
    end = number(record["end_monotonic_ns"], "process end", start)
    raw = (runtime / "stdout.log").read_bytes()
    capture = (runtime / "capture.jsonl").read_bytes()
    staged_startup = startup.read_bytes()
    for key, payload in (("stdout_sha256", raw), ("capture_sha256", capture), ("startup_sha256", staged_startup)):
        require(type(record[key]) is str and re.fullmatch(r"[0-9a-f]{64}", record[key]) is not None and
                record[key] == hashlib.sha256(payload).hexdigest(), "Stale artifact hash: " + key)
    require(staged_startup == (Path(__file__).resolve().parent / "startup.sh").read_bytes(),
            "Staged startup differs from reviewed source bytes")
    require(capture.endswith(b"\n") and capture.strip(), "Truncated/empty capture file")
    rows = [decode(line) for line in capture.splitlines()]
    result = check_trace(rows, identities({key: record[key] for key in IDENTITIES.split()}), start, end)
    return {"result": "PASS", "scope": "disarmed_source_capture_only", "admission": "not_instantiated",
            "guidance_clock_map": "not_qualified", "operational_freshness_budget": "not_defined",
            "topic_scope": "post_stop_advertised_only_snapshot", "advertised_topics": check_topics(raw), **result}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("runtime", type=Path)
    args = parser.parse_args()
    try:
        result = check_run(args.runtime)
    except (ValueError, OSError, UnicodeError) as error:
        print(json.dumps({"result": "FAIL", "error": str(error)}))
        return 1
    print(json.dumps(result, sort_keys=True, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
