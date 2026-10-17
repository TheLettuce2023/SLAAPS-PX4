#!/usr/bin/env python3
"""Independent bounded C1D native closed-owner replay, not flight authorization."""
from __future__ import annotations

import argparse
import copy
import hashlib
import json
import math
from pathlib import Path
import re

MAX64 = (1 << 64) - 1
WINDOW_US = 2_000_000
BINDINGS = "permitted_source controller_instance_id owner_lifetime_id native_run_id producer_lifetime_id clock_epoch"
ORIGIN = "NativePosixSitl"
PROFILE = "c1d-closed-native/v1"
NAV = {0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 13, 14, 15, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30}
SOURCE_FIELDS = "initialized state sequence transition_epoch physical_generation committed_time_us checked_time_us"
ANSI = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")


class EvidenceError(ValueError):
    pass


def require(condition, message):
    if not condition:
        raise EvidenceError(message)


def fields(value, names, path):
    require(type(value) is dict and set(value) == set(names.split()), "Missing/unknown fields: " + path)


def integer(value, path, low=0, high=MAX64):
    require(type(value) is int and low <= value <= high, "Invalid integer: " + path)
    return value


def u64(value, path, low=0):
    require(type(value) is str and len(value) <= 20 and re.fullmatch(r"0|[1-9][0-9]*", value) is not None,
            "Noncanonical uint64: " + path)
    return integer(int(value), path, low)


def decode(raw):
    def pairs(items):
        result = {}
        for key, value in items:
            require(key not in result, "Duplicate JSON key: " + key)
            result[key] = value
        return result

    def nonfinite(value):
        raise EvidenceError("Nonfinite JSON: " + value)
    try:
        return json.loads(raw, object_pairs_hook=pairs, parse_constant=nonfinite)
    except (UnicodeError, json.JSONDecodeError) as error:
        raise EvidenceError("Malformed/truncated JSON") from error


def equal(actual, expected, path):
    """Exact typed recursive comparison also rejects unknown nested fields."""
    require(type(actual) is type(expected), "Wrong type: " + path)
    if type(expected) is dict:
        require(set(actual) == set(expected), "Missing/unknown fields: " + path)
        for key in expected:
            equal(actual[key], expected[key], path + "." + key)
    elif type(expected) is list:
        require(len(actual) == len(expected), "Wrong array length: " + path)
        for index, value in enumerate(expected):
            equal(actual[index], value, path + "[" + str(index) + "]")
    else:
        require((type(actual) is not float or math.isfinite(actual)) and actual == expected,
                "Contradictory value: " + path)


def checked_bindings(values):
    fields(values, BINDINGS, "bindings")
    for key, value in values.items():
        u64(value, "bindings." + key, 1)
    return values


def raw_state(value, path):
    fields(value, "arming_state nav_state", path)
    require(integer(value["arming_state"], path + ".arming_state", 1, 2) == 1,
            "Actual native run is not disarmed: " + path)
    require(integer(value["nav_state"], path + ".nav_state", 0, 30) in NAV, "Unknown native navigation state")
    return value


def zero_map(controller="0"):
    return dict(valid=False, source="0", session="0", controller_instance_id=controller, map_epoch="0",
                validator_minus_producer_us="0", uncertainty_us="0", valid_from_producer_us="0", valid_until_producer_us="0")


def physical(generation="0", stamp="0", controller="0", continuous=False):
    return {"generation": generation, "observer_continuous": continuous,
            "request_guard_context": {"validator_receive_time_us": stamp, "arm_state": "Disarmed", "mode_state": "NotSelected",
                                      "clock_mapping_valid": False, "transport_connected": False, "required_state_valid": False},
            "map": zero_map(controller)}


def identity(bindings=None):
    return {"owner_lifetime_id": bindings["owner_lifetime_id"] if bindings else "0",
            "native_run_id": bindings["native_run_id"] if bindings else "0", "origin": ORIGIN}


def cause(value="None", kind="DiagnosticLocalReason"):
    return {"type": kind, "value": value}


def correlation(operation="None", close=None):
    return {"operation": operation, "ingress": {"packet_kind": "None", **dict.fromkeys(
        "source session controller_instance_id transition_epoch map_epoch packet_sequence acknowledged_status_sequence".split(), "0")},
        "admin": {"has_packet": False, "packet_source": "0", "packet_session": "0", "packet_operation_sequence": "0",
                  "has_supervisor_event": False, "supervisor_event_id": "0", "supervisor_event_consumed": False},
        "operator_instance": "0", "close_reason": close, "authentication": None}


def snapshot(bindings=None):
    return {"domain": "SitlDisarmedDiagnostic" if bindings else "Flight", "identity": identity(bindings),
            "controller_instance_id": bindings["controller_instance_id"] if bindings else "0",
            "configured": bindings is not None, "initialized": False, "terminal": False, "selection": "Disabled",
            "selector_epoch": "0", "selection_reason": cause(), "operator_attached": False, "operator_instance": "0",
            "adapter": {"initialized": False, "terminal": False, **dict.fromkeys(
                "state_generation transition_epoch barrier_report_sequence barrier_validator_time_us last_report_sequence".split(), "0")},
            "session": {"gate": "NoTrustedSession", "has_last_successful_session": False,
                        "last_successful_session": {"owner": "0", "session": "0"}, "has_wire_operation_floor": False,
                        "wire_operation_floor": "0", "has_supervisor_floor": False, "supervisor_floor": "0"},
            "admission": {"acknowledgement_current": False, **dict.fromkeys(
                ("acknowledgement_operation_sequence acknowledged_status_sequence acknowledgement_raw_producer_time_us "
                 "mapped_acknowledgement_time_us earliest_acknowledgement_time_us latest_acknowledgement_time_us "
                 "source session controller_instance_id transition_epoch map_epoch").split(), "0"), "map": zero_map()},
            "latched": False, "first_latched_code": "Accepted", "rejection_count": "0",
            "has_last_accepted_sequence": False, "last_accepted_sequence": "0", "last_accepted_authenticated": False}


def empty_report():
    return {"domain": "Flight", "identity": identity(), "report_sequence": "0", "transition_epoch": "0", "selector_epoch": "0",
            "kind": "PhysicalBarrier", "code": "Observed", "cause": cause(), "correlation": correlation(),
            "state_reference_valid": False, "physical": physical(), "snapshot": snapshot(), "common_decision": None,
            "eligible_for_control": False}


def closed_configuration(bindings):
    return {"contract": {"protocol_version": 65536, "permitted_source": bindings["permitted_source"], "disarmed_policy": "ValidateOnly",
                         "acceleration_bounds": {"minimum_m_s2": [-10.0] * 3, "maximum_m_s2": [10.0] * 3, "maximum_norm_m_s2": 12.0},
                         "timing": {"minimum_validity_us": "80000", "maximum_validity_us": "80000", "maximum_command_age_us": "60000",
                                    "maximum_future_skew_us": "8000", "maximum_clock_uncertainty_us": "4000"},
                         "quaternion_norm_tolerance": 0.00999999978},
            "adapter_identity": {"controller_instance_id": bindings["controller_instance_id"]}, "identity": identity(bindings),
            "producer_lifetime_id": bindings["producer_lifetime_id"], "clock_epoch": bindings["clock_epoch"]}


def empty_source():
    return {"initialized": False, "state": {"arming_state": 0, "nav_state": 0}, **dict.fromkeys(
        "sequence transition_epoch physical_generation committed_time_us checked_time_us".split(), "0")}


class ClosedReplay:
    """Finite closed-poll oracle; never admits packets or supplies trusted inputs."""
    def __init__(self, bindings):
        self.state = snapshot(bindings)
        self.bindings = bindings
        self.report_count = 0

    def report(self, sample, kind, code, reason, stop=False, common=None):
        self.report_count += 1
        self.state["adapter"]["last_report_sequence"] = str(self.report_count)
        return {"domain": "SitlDisarmedDiagnostic", "identity": identity(self.bindings), "report_sequence": str(self.report_count),
                "transition_epoch": self.state["adapter"]["transition_epoch"], "selector_epoch": "1", "kind": kind,
                "code": code, "cause": reason, "correlation": correlation("LifecycleClose" if stop else "Poll", "OwnerStop" if stop else None),
                "state_reference_valid": True, "physical": copy.deepcopy(sample), "snapshot": copy.deepcopy(self.state),
                "common_decision": common, "eligible_for_control": False}

    def turn(self, sample, generation, stop=False):
        reports = []
        if not stop and self.state["adapter"]["state_generation"] != str(generation):
            self.state["initialized"] = self.state["adapter"]["initialized"] = True
            self.state["selector_epoch"] = "1"
            self.state["adapter"].update(state_generation=str(generation), transition_epoch=str(generation),
                                          barrier_report_sequence=str(self.report_count + 1),
                                          barrier_validator_time_us=sample["request_guard_context"]["validator_receive_time_us"])
            reports.append(self.report(sample, "PhysicalBarrier", "Observed", cause()))
        rejections = int(self.state["rejection_count"]) + 1
        self.state["rejection_count"] = str(rejections)
        decision = {"code": "ClockMappingInvalid", "syntactically_valid": False, "eligible_for_control": False,
                    "latched": False, "first_latched_code": "Accepted", "conservative_age_valid": False,
                    "conservative_age_us": "0", "rejection_count": str(rejections)}
        if stop:
            self.state["session"]["gate"] = "EstablishmentRequired"
        code = "AlreadyDisabled" if stop else "Rejected"
        reports.append(self.report(sample, "Operation", code, cause() if stop else cause("ClockMappingInvalid", "DecisionCode"),
                                   stop, None if stop else decision))
        count = len(reports)
        return {"code": code, "snapshot": copy.deepcopy(self.state), "record_count": count,
                "records": reports + [empty_report() for _ in range(4 - count)], "has_operation_record": True,
                "operation_record_index": count - 1, "grant_consumed": False, "action_performed": stop}


def check_trace(data, expected_bindings):
    """Structural/semantic replay; caller separately verifies native build provenance."""
    bindings = checked_bindings(expected_bindings)
    fields(data, "schema header records closure summary lifecycle footer", "trace")
    equal(data["schema"], "research-c1d-native-owner/v1", "schema")
    header = data["header"]
    fields(header, "profile_id execution_origin configuration normal_record_capacity clock_source owner_size_bytes observation_window_us", "header")
    equal(header["profile_id"], PROFILE, "profile")
    equal(header["execution_origin"], ORIGIN, "execution origin")
    equal(header["configuration"], closed_configuration(bindings), "configuration")
    equal(header["normal_record_capacity"], 256, "capacity")
    equal(header["clock_source"], "px4_hrt_absolute_time_native_posix", "clock source")
    require(0 < u64(header["owner_size_bytes"], "owner size") <= 2 * 1024 * 1024, "Owner storage exceeds fixed diagnostic cap")
    equal(header["observation_window_us"], str(WINDOW_US), "window")
    life = data["lifecycle"]
    fields(life, "bootstrap_count claim_count normal_source_done retirement commander_stop_result bridge_fault stack_measured "
           "stack_size_bytes stack_guard_bytes stack_marker_offset_bytes export_after_retirement", "lifecycle")
    for key, value in {"bootstrap_count": 1, "claim_count": 1, "normal_source_done": True, "retirement": "Retired",
                       "commander_stop_result": 0, "bridge_fault": "None", "stack_measured": True,
                       "export_after_retirement": True}.items():
        equal(life[key], value, "lifecycle." + key)
    stack = u64(life["stack_size_bytes"], "actual usable stack", 1)
    guard = u64(life["stack_guard_bytes"], "stack guard")
    marker = u64(life["stack_marker_offset_bytes"], "stack marker offset", 1)
    require(guard < stack and 49152 <= marker < stack - guard, "Native diagnostic stack criterion not established")
    records = data["records"]
    require(type(records) is list and 1 <= len(records) <= 256, "Incomplete or overflowing normal archive")
    source = empty_source()
    replay = ClosedReplay(bindings)
    initial_time = None
    for index, record in enumerate(records):
        path = "records[" + str(index) + "]"
        fields(record, "observation before after adapter_input result", path)
        observation = record["observation"]
        fields(observation, "kind before after source_time_us", path + ".observation")
        kind = observation["kind"]
        require(type(kind) is str and kind in {"Initial", "Transition", "Sample"}, "Invalid normal source kind")
        require((index == 0) == (kind == "Initial"), "Missing/repeated Initial")
        before_raw = raw_state(observation["before"], path + ".before raw")
        after_raw = raw_state(observation["after"], path + ".after raw")
        require(before_raw == (after_raw if index == 0 else source["state"]), "Source before-state contradiction")
        changed = before_raw != after_raw
        require(changed == (kind == "Transition"), "Omitted/fabricated/unchanged archived Transition")
        stamp = u64(observation["source_time_us"], path + ".source time", 1)
        fields(record["before"], SOURCE_FIELDS, path + ".before")
        floor = u64(record["before"]["checked_time_us"], path + ".checked floor")
        require(int(source["checked_time_us"]) <= floor <= stamp, "Unrecorded checked-time floor regressed or exceeds source time")
        expected_before = copy.deepcopy(source)
        if index > 0:
            expected_before["checked_time_us"] = str(floor)
        equal(record["before"], expected_before, path + ".before")
        generation = 1 if index == 0 else int(source["physical_generation"]) + int(changed)
        source = {"initialized": True, "state": copy.deepcopy(after_raw), "sequence": str(index + 1),
                  "transition_epoch": str(generation), "physical_generation": str(generation),
                  "committed_time_us": str(stamp), "checked_time_us": str(stamp)}
        equal(record["after"], source, path + ".after")
        sample = physical(str(generation), str(stamp), bindings["controller_instance_id"], True)
        equal(record["adapter_input"], sample, path + ".adapter_input")
        equal(record["result"], replay.turn(sample, generation), path + ".result")
        if initial_time is None:
            initial_time = stamp
    final = data["closure"]
    fields(final, "fault attempted retained_source adapter_input result", "closure")
    equal(final["fault"], "None", "closure fault")
    fields(final["attempted"], "kind before after source_time_us", "Stop observation")
    stop_time = u64(final["attempted"]["source_time_us"], "Stop time", 1)
    require(stop_time >= int(source["checked_time_us"]) and stop_time - initial_time >= WINDOW_US,
            "Stop chronology or observation window incomplete")
    equal(final["attempted"], {"kind": "Stop", "before": source["state"], "after": source["state"],
                               "source_time_us": str(stop_time)}, "Stop observation")
    source["checked_time_us"] = str(stop_time)
    equal(final["retained_source"], source, "closure retained source")
    sample = physical(source["physical_generation"], str(stop_time), bindings["controller_instance_id"], True)
    equal(final["adapter_input"], sample, "Stop adapter input")
    equal(final["result"], replay.turn(sample, int(source["physical_generation"]), True), "Stop adapter result")
    equal(data["summary"], {"state": "Stopped", "fault": "None", "source": source, "record_count": len(records), "has_closure": True},
          "actual final owner summary")
    equal(data["footer"], {"normal_record_count": len(records), "adapter_report_count": replay.report_count,
                           "last_report_sequence": str(replay.report_count)}, "footer")
    return {"result": "PASS", "scope": "closed_native_owner_semantics_only", "source_record_count": len(records),
            "transition_count": int(source["transition_epoch"]) - 1, "adapter_report_count": replay.report_count,
            "initial_time_us": str(initial_time), "stop_time_us": str(stop_time), "source_span_us": str(stop_time - initial_time),
            "authority": "none", "guidance_clock_map": "not_qualified", "unchanged_assignments": "not_individually_archived"}


def check_topics(raw):
    lines = [ANSI.sub("", line).strip() for line in raw.decode("utf-8").splitlines()]
    markers = []
    for name in ("C1D_SINK_FAILURES_REJECTED", "C1D_TOPICS_BEGIN", "C1D_TOPICS_END", "C1D_STARTUP_COMPLETE"):
        positions = [index for index, line in enumerate(lines) if line == name]
        require(len(positions) == 1, "Missing/duplicate completion marker: " + name)
        markers.append(positions[0])
    sink, begin, end, complete = markers
    require(sink < begin < end < complete, "Post-stop sink/topic/completion order violated")
    rows = [line for line in lines[begin + 1:end] if line]
    require(rows and rows[0].split() == ["TOPIC", "NAME", "INST", "#SUB", "#Q", "SIZE", "PATH"], "Missing topic inventory header")
    allowed = {"actuator_armed", "config_overrides", "event", "failsafe_flags", "failure_detector_status", "health_report",
               "led_control", "log_message", "mavlink_log", "parameter_update", "power_button_state", "tune_control",
               "vehicle_control_mode", "vehicle_status"}
    topics = set()
    for row in rows[1:]:
        match = re.fullmatch(r"([a-z][a-z0-9_]*)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\S+)", row)
        require(match is not None, "Malformed advertised topic row")
        name, instance, subscribers, queue, size, path = match.groups()
        require(name in allowed and int(instance) == 0 and int(queue) > 0 and int(size) > 0 and name not in topics,
                "Unknown/forbidden/duplicate advertised topic")
        topics.add(name)
    require(not any("wrong type passed to param_get()" in line or
                    re.search(r"\[parameters\] get: param \d+ invalid", line) for line in lines), "Unresolved Commander parameter")
    return sorted(topics)


def check_run(runtime, expected_bindings):
    """Check retained runtime bytes; runner owns source/executable/build binding."""
    bindings = checked_bindings(expected_bindings)
    runtime = Path(runtime).resolve()
    run = decode((runtime / "run.json").read_bytes())
    fields(run, "schema command instance runtime bindings scope status startup_sha256 start_monotonic_ns pid forced_shutdown "
           "exit_code end_monotonic_ns stdout_sha256 trace_sha256 repeat_trace_sha256", "run")
    equal(run["schema"], "research-c1d-run/v1", "run schema")
    equal(run["bindings"], bindings, "external run bindings")
    equal(run["runtime"], str(runtime), "runtime")
    equal(run["scope"], "closed_diagnostic_no_ingress_no_output", "scope")
    equal(run["status"], "captured_unscored", "run completion")
    equal(run["forced_shutdown"], False, "forced shutdown")
    equal(run["exit_code"], 0, "exit code")
    instance = integer(run["instance"], "instance", 1)
    integer(run["pid"], "PID", 1)
    start = integer(run["start_monotonic_ns"], "host start", 1)
    end = integer(run["end_monotonic_ns"], "host end", start)
    startup = runtime / "startup.sh"
    require(startup.is_file() and not startup.is_symlink(), "Missing/nonregular staged startup")
    equal(run["command"], [str(runtime.parent / "build/bin/px4"), "-i", str(instance), "-w", str(runtime), "-s", str(startup)],
          "owned process command")
    payloads = {}
    for name, key in (("startup.sh", "startup_sha256"), ("stdout.log", "stdout_sha256"),
                      ("trace.json", "trace_sha256"), ("trace-repeat.json", "repeat_trace_sha256")):
        payloads[name] = (runtime / name).read_bytes()
        require(type(run[key]) is str and re.fullmatch(r"[0-9a-f]{64}", run[key]) is not None and
                run[key] == hashlib.sha256(payloads[name]).hexdigest(), "Stale artifact hash: " + name)
    require(payloads["startup.sh"] == (Path(__file__).resolve().parent / "startup.sh").read_bytes(), "Startup differs from reviewed source")
    require(payloads["trace.json"] == payloads["trace-repeat.json"], "Repeated export changed retained owner evidence")
    result = check_trace(decode(payloads["trace.json"]), bindings)
    require(start <= int(result["initial_time_us"]) * 1000 + 999 and int(result["stop_time_us"]) * 1000 <= end,
            "Native source timestamps outside owned process lifetime")
    result.update(advertised_topics=check_topics(payloads["stdout.log"]), topic_scope="post_stop_advertised_only_snapshot")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("runtime", type=Path)
    parser.add_argument("--bindings", type=Path, required=True, help="Externally established six-identity JSON object")
    args = parser.parse_args()
    try:
        result = check_run(args.runtime, decode(args.bindings.read_bytes()))
    except (ValueError, OSError, UnicodeError) as error:
        print(json.dumps({"result": "FAIL", "error": str(error)}))
        return 1
    print(json.dumps(result, sort_keys=True, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
