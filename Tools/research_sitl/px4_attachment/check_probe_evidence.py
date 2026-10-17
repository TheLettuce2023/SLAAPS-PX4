#!/usr/bin/env python3
"""Independent C1A passive-observation evidence checks; never admission proof."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re

WINDOW_NS = 2_000_000_000  # Fixed diagnostic window declared by research_probe.
UINT64_MAX = (1 << 64) - 1
UINT32_MAX = (1 << 32) - 1
DISARMED = 1  # vehicle_status.msg ARMING_STATE_DISARMED, not an inferred mode.
ANSI = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")
WIRE_FIELDS = {
    "clock": {"b": "host_before_ns", "h": "px4_hrt_us", "a": "host_after_ns"},
    "status": {"us": "timestamp_us", "g": "generation", "d": "generation_delta",
               "f": "fresh", "s": "arming_state"},
    "armed": {"us": "timestamp_us", "g": "generation", "d": "generation_delta",
              "f": "fresh", "a": "armed"},
    "counts": {"c": "clock_samples", "v": "vehicle_status_fresh",
               "a": "actuator_armed_fresh", "ns": "host_elapsed_ns"},
    "summary": {"complete": "complete", "pass": "pass", "disarmed": "all_disarmed",
                "clocks": "clocks_valid", "obs": "observations_valid"},
}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def integer(value: object, name: str, minimum: int = 0,
            maximum: int = UINT64_MAX) -> int:
    require(type(value) is int and minimum <= value <= maximum, f"Invalid {name}")
    return value


def unique_object(pairs: list[tuple]) -> dict:
    result = {}
    for key, value in pairs:
        require(key not in result, f"Duplicate JSON field {key}")
        result[key] = value
    return result


def decode(text: str) -> dict:
    value = json.loads(text, object_pairs_hook=unique_object)
    require(type(value) is dict, "Expected JSON object")
    return value


def fields(row: dict, names: str) -> None:
    require(set(row) == set(names.split()), f"Unexpected {row.get('type')} fields")


def check_topics(lines: list[str], last_probe_line: int, completion_line: int) -> list[str]:
    begin = [i for i, line in enumerate(lines) if line == "C1A_TOPICS_BEGIN"]
    end = [i for i, line in enumerate(lines) if line == "C1A_TOPICS_END"]
    require(len(begin) == len(end) == 1, "Missing/duplicate topic snapshot markers")
    require(last_probe_line < begin[0] < end[0] < completion_line, "Topic snapshot ordering mismatch")
    snapshot = [line for line in lines[begin[0] + 1:end[0]] if line]
    require(snapshot and snapshot[0].split() == ["TOPIC", "NAME", "INST", "#SUB", "#Q", "SIZE", "PATH"],
            "Missing topic snapshot header")
    topics = set()
    forbidden = ("offboard_control_mode", "trajectory_setpoint", "vehicle_attitude_setpoint",
                 "vehicle_rates_setpoint", "vehicle_torque_setpoint", "vehicle_thrust_setpoint",
                 "actuator_motors", "actuator_servos", "actuator_outputs", "actuator_test")
    for line in snapshot[1:]:
        match = re.fullmatch(r"([a-z][a-z0-9_]*)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\S+)", line)
        require(match is not None, "Malformed topic snapshot row")
        name, instance, subscribers, queue, size, path = match.groups()
        identity = (name, int(instance))
        require(identity not in topics, "Duplicate topic snapshot row")
        require(int(queue) > 0 and int(size) > 0, "Invalid topic dimensions")
        require(not name.startswith(forbidden), f"Forbidden advertised topic {name}")
        topics.add(identity)
    require({("vehicle_status", 0), ("actuator_armed", 0)} <= topics,
            "Missing actual state topics in snapshot")
    return sorted({name for name, _ in topics})


def check_run(runtime: Path) -> dict:
    """Check one retained run, raising ValueError on incomplete or invalid evidence."""
    runtime = Path(runtime).resolve()
    record = decode((runtime / "run.json").read_text())
    raw = (runtime / "stdout.log").read_bytes()
    require(record.get("stdout_sha256") == hashlib.sha256(raw).hexdigest(), "Log digest mismatch")
    require(type(record.get("exit_code")) is int and record["exit_code"] == 0, "Unsuccessful child exit")
    require(record.get("forced_shutdown") is False, "Missing normal shutdown")
    require(record.get("admission") == "not_instantiated", "Admission scope mismatch")
    require(record.get("research_network") == "not_instantiated", "Network scope mismatch")
    require(type(record.get("runtime")) is str and Path(record["runtime"]).resolve() == runtime,
            "Runtime identity mismatch")
    integer(record.get("pid"), "PID", 1)
    integer(record.get("instance"), "instance", 1)
    start = integer(record.get("start_monotonic_ns"), "process start", 1)
    end = integer(record.get("end_monotonic_ns"), "process end", start)
    rows = []
    marker_line = None
    last_row_line = -1
    lines = [ANSI.sub("", text).strip() for text in raw.decode("utf-8").splitlines()]
    require(not any("wrong type passed to param_get()" in line or
                    re.search(r"\[parameters\] get: param \d+ invalid", line) for line in lines),
            "Unresolved Commander parameter dependency")
    for number, text in enumerate(lines):
        if text == "C1A_STARTUP_COMPLETE":
            require(marker_line is None, "Duplicate startup completion")
            marker_line = number
        if "C1A " in text:
            compact = decode(text.split("C1A ", 1)[1])
            kind = compact.get("t")
            require(type(kind) is str and kind in WIRE_FIELDS, "Unknown compact row type")
            mapping = WIRE_FIELDS[kind]
            require(set(compact) == {"t", *mapping}, "Unexpected compact fields")
            normalized_kind = {"status": "vehicle_status", "armed": "actuator_armed"}.get(kind, kind)
            rows.append({"type": normalized_kind, **{name: compact[key] for key, name in mapping.items()}})
            last_row_line = number
    require(rows and rows[0].get("type") == "clock", "Missing initial clock")
    require(marker_line is not None and marker_line > last_row_line, "Missing/falsely ordered startup completion")
    advertised_topics = check_topics(lines, last_row_line, marker_line)
    require(rows[-1].get("type") == "summary", "Missing final summary")
    require(sum(row.get("type") == "summary" for row in rows) == 1, "Duplicate summary")
    require(len(rows) >= 3 and rows[-2].get("type") == "counts", "Missing final counts")
    require(sum(row.get("type") == "counts" for row in rows) == 1, "Duplicate counts")
    counts = rows.pop(-2)
    rows[-1].update({key: value for key, value in counts.items() if key != "type"})
    first = rows[0]
    previous_clock = None
    clocks = []
    observations = {"vehicle_status": [], "actuator_armed": []}
    fresh_counts = {kind: 0 for kind in observations}
    sampled_topics = set()
    for row in rows[:-1]:
        kind = row.get("type")
        if kind == "clock":
            fields(row, "type host_before_ns px4_hrt_us host_after_ns")
            before = integer(row["host_before_ns"], "host before", start, end)
            after = integer(row["host_after_ns"], "host after", before, end)
            hrt = integer(row["px4_hrt_us"], "PX4 hrt", 1, (UINT64_MAX - 999) // 1000)
            # Native no-lockstep diagnostic identity, not a qualified clock map.
            require(hrt * 1000 <= after and before <= hrt * 1000 + 999,
                    "PX4 hrt outside native host bracket")
            if previous_clock:
                require(previous_clock["host_after_ns"] - first["host_before_ns"] < WINDOW_NS,
                        "Sampling continued after native completion")
                require(before >= previous_clock["host_after_ns"], "Host clock moved backward")
                require(hrt >= previous_clock["px4_hrt_us"], "PX4 clock moved backward")
            previous_clock = row
            clocks.append(row)
            sampled_topics = set()
            continue
        require(kind in observations, "Unknown probe row type")
        require(len(clocks) >= 2 and kind not in sampled_topics, "Invalid observation ordering")
        if kind == "vehicle_status":
            require("actuator_armed" not in sampled_topics, "Native state row order mismatch")
            fields(row, "type timestamp_us generation generation_delta fresh arming_state")
            require(type(row["arming_state"]) is int and row["arming_state"] == DISARMED,
                    "Vehicle state not actually disarmed")
        else:
            fields(row, "type timestamp_us generation generation_delta fresh armed")
            require(row["armed"] is False, "Actuator state invalid or armed")
        stamp = integer(row["timestamp_us"], "publication timestamp", 1, previous_clock["px4_hrt_us"])
        generation = integer(row["generation"], "generation", 0, UINT32_MAX)
        delta = integer(row["generation_delta"], "generation delta", 0, UINT32_MAX)
        prior = observations[kind][-1] if observations[kind] else None
        require(prior is None or stamp > prior["timestamp_us"], "Publication timestamp did not advance")
        expected_delta = (generation - prior["generation"]) & UINT32_MAX if prior else 0
        require(delta == expected_delta and (prior is None or delta != 0), "Generation evidence mismatch")
        expected_fresh = stamp >= first["px4_hrt_us"]
        require(type(row["fresh"]) is bool and row["fresh"] == expected_fresh, "False freshness flag")
        fresh_counts[kind] += int(expected_fresh)
        observations[kind].append(row)
        sampled_topics.add(kind)
    require(len(clocks) >= 2, "Insufficient clock observations")
    require(all(count >= 2 for count in fresh_counts.values()), "Insufficient fresh state publications")
    elapsed = clocks[-1]["host_after_ns"] - first["host_before_ns"]
    require(elapsed >= WINDOW_NS, "Diagnostic window incomplete")
    summary = rows[-1]
    fields(summary, "type complete pass clock_samples vehicle_status_fresh actuator_armed_fresh all_disarmed clocks_valid observations_valid host_elapsed_ns")
    for flag in ("complete", "pass", "all_disarmed", "clocks_valid", "observations_valid"):
        require(summary[flag] is True, f"Failed summary {flag}")
    expected_counts = {"clock_samples": len(clocks), "host_elapsed_ns": elapsed,
                       "vehicle_status_fresh": fresh_counts["vehicle_status"],
                       "actuator_armed_fresh": fresh_counts["actuator_armed"]}
    for name, expected in expected_counts.items():
        require(integer(summary[name], name) == expected, f"False summary {name}")
    return {"result": "PASS", "scope": "passive_state_clock_observations_only",
            "admission": "not_instantiated", "clock_map": "not_qualified",
            "transition_history": "not_proven", "runtime": str(runtime),
            "advertised_topic_snapshot": advertised_topics,
            "topic_scope": "snapshot_absence_only_not_continuous_publication_proof",
            **expected_counts,
            "host_span_ns": elapsed,
            "px4_hrt_delta_us": clocks[-1]["px4_hrt_us"] - first["px4_hrt_us"],
            "max_host_bracket_ns": max(row["host_after_ns"] - row["host_before_ns"] for row in clocks)}


def check_campaign(manifest_path: Path) -> dict:
    """Aggregate two passive runs, not source/build/no-output acceptance."""
    manifest = decode(Path(manifest_path).read_text())
    require(manifest.get("schema") == "research-c1a/v1", "Unknown campaign schema")
    require(manifest.get("scope") == "passive_no_output_no_admission", "Wrong campaign scope")
    require(manifest.get("status") == "built_runs_unscored", "Campaign not ready for scoring")
    require(manifest.get("changed_source_inputs") == [], "Source stability not established")
    runs = manifest.get("runs")
    require(type(runs) is list and len(runs) == 2, "Two fresh runs required")
    require(all(type(run) is dict for run in runs), "Malformed run inventory")
    require(len({run.get("runtime") for run in runs}) == 2, "Reused runtime")
    require(len({integer(run.get("instance"), "instance", 1) for run in runs}) == 2, "Reused instance")
    results = []
    prior_end = None
    for run in runs:
        runtime = Path(run["runtime"])
        require(decode((runtime / "run.json").read_text()) == run, "Manifest/run record mismatch")
        results.append(check_run(runtime))
        if prior_end is not None:
            require(run["start_monotonic_ns"] >= prior_end, "Runs overlap")
        prior_end = run["end_monotonic_ns"]
    return {"result": "PASS", "scope": "two_passive_runs_only_not_build_or_output_acceptance",
            "runs": results}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("path", type=Path, help="run directory or campaign manifest.json")
    args = parser.parse_args()
    try:
        result = check_run(args.path) if args.path.is_dir() else check_campaign(args.path)
    except (ValueError, OSError, KeyError, TypeError) as error:
        print(json.dumps({"result": "FAIL", "error": str(error)}))
        return 1
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
