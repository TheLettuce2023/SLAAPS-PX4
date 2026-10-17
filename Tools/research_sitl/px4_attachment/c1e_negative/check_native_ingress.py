#!/usr/bin/env python3
"""Independent C1E negative-ingress archive checks.

An archive check establishes bounded, internally consistent diagnostic evidence.
Only a later run wrapper may bind it to a source-built, disarmed native process.
Synthetic fixtures exercising this checker never count as PX4 acceptance.
"""

from __future__ import annotations

import argparse
import copy
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import re
import sys


MAX64 = (1 << 64) - 1
PROFILE = "c1e-negative-native/v1"
ORIGIN = "NativePosixSitl"
SCHEMA = "research-c1e-native-ingress/v1"
RUN_SCHEMA = "research-c1e-native-run/v1"
NAV = {0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 13, 14, 15, 17, 18, 19,
       20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30}
SOURCE_FIELDS = "initialized state sequence transition_epoch physical_generation committed_time_us checked_time_us"
CANDIDATE_FIELDS = (
    "token native_run_id owner_lifetime_id controller_instance_id source_id transport_binding_id "
    "request_sequence expected_transition_epoch producer_monotonic_us producer_expiry_monotonic_us "
    "clock_mapping_epoch arrival_native_us signing_timestamp_48 acceleration_x_frd_m_s2 "
    "acceleration_y_frd_m_s2 acceleration_z_frd_m_s2 protocol_version domain profile target_system "
    "target_component frame quantity reference_point flags reserved channel header_system "
    "header_component incoming_link_id mavlink_sequence payload_length parser_stage frame_crc signature peer_port"
)
STATUS_FIELDS = (
    "native_run_id owner_lifetime_id controller_instance_id source_id transport_binding_id request_sequence "
    "status_sequence transition_epoch source_sequence state_generation arrival_native_us evaluation_native_us "
    "source_checked_native_us clock_mapping_epoch protocol_version reason domain profile execution_origin "
    "outcome arm_state raw_nav_state research_mode_state selector_state estimator_valid clock_map_valid "
    "eligible_for_control flags target_system target_component reserved"
)
STAGES = ("Published Busy Closed NotConfigured MalformedEnvelope WrongMessage WrongChannel BadCrc "
          "BadSignature Replay WrongHeader WrongLink InvalidPayload WrongIdentity WrongTarget ResourceFault").split()
HASH = re.compile(r"[0-9a-f]{64}")
BUILTINS = ["commander", "list_files", "list_tasks", "listener", "param",
            "research_native_ingress", "shutdown", "sleep", "uorb", "ver"]
ARTIFACTS = ("startup.sh", "stdout.log", "trace.json", "trace-repeat.json",
             "request.bin", "status.bin", "sender.json")


class EvidenceError(ValueError):
    pass


def require(condition, message):
    if not condition:
        raise EvidenceError(message)


def fields(value, names, path):
    require(type(value) is dict and set(value) == set(names.split()),
            "Missing/unknown fields: " + path)


def integer(value, path, low=0, high=MAX64):
    require(type(value) is int and low <= value <= high, "Invalid integer: " + path)
    return value


def u64(value, path, low=0):
    require(type(value) is str and len(value) <= 20
            and re.fullmatch(r"0|[1-9][0-9]*", value) is not None,
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


def raw_state(value, path):
    fields(value, "arming_state nav_state", path)
    require(integer(value["arming_state"], path + ".arming_state", 1, 2) == 1,
            "Native run is not disarmed: " + path)
    require(integer(value["nav_state"], path + ".nav_state", 0, 30) in NAV,
            "Unknown native navigation state: " + path)
    return value


def sha256_file(path):
    require(path.is_file() and not path.is_symlink(), "Missing/nonregular artifact: " + str(path))
    return hashlib.sha256(path.read_bytes()).hexdigest()


def prior_closed_oracle():
    """Use the separately accepted C1D negative poll model for shared turns."""
    path = Path(__file__).resolve().parents[1] / "c1d_closed/check_closed_owner.py"
    specification = importlib.util.spec_from_file_location("c1d_closed_oracle", path)
    module = importlib.util.module_from_spec(specification)
    specification.loader.exec_module(module)
    return module


def checked_configuration(header, old):
    fields(header, "profile_id execution_origin clock_source configuration source_capacity "
           "transaction_capacity preauth_reject_capacity owner_size_bytes worker_size_bytes", "header")
    equal(header["profile_id"], PROFILE, "profile")
    equal(header["execution_origin"], ORIGIN, "origin")
    equal(header["clock_source"], "px4_hrt_absolute_time_native_posix", "clock source")
    for name, limit in (("source_capacity", 256), ("transaction_capacity", 32),
                        ("preauth_reject_capacity", 64)):
        equal(header[name], limit, "header." + name)
    require(0 < u64(header["owner_size_bytes"], "owner size") <= 4 * 1024 * 1024,
            "owner storage exceeds fixed cap")
    require(0 < u64(header["worker_size_bytes"], "worker size") <= 1024 * 1024,
            "worker storage exceeds bounded cap")
    config = header["configuration"]
    fields(config, "contract adapter_identity identity transport_binding_id producer_lifetime_id "
           "source_clock_id channel peer_port", "configuration")
    fields(config["contract"], "protocol_version permitted_source disarmed_policy "
           "acceleration_bounds timing quaternion_norm_tolerance", "contract")
    fields(config["adapter_identity"], "controller_instance_id", "adapter identity")
    fields(config["identity"], "owner_lifetime_id native_run_id origin", "native identity")
    bindings = {
        "permitted_source": config["contract"]["permitted_source"],
        "controller_instance_id": config["adapter_identity"]["controller_instance_id"],
        "owner_lifetime_id": config["identity"]["owner_lifetime_id"],
        "native_run_id": config["identity"]["native_run_id"],
        "producer_lifetime_id": config["producer_lifetime_id"],
        "clock_epoch": config["source_clock_id"],
    }
    old.checked_bindings(bindings)
    expected = old.closed_configuration(bindings)
    del expected["clock_epoch"]
    expected.update(transport_binding_id=config["transport_binding_id"],
                    source_clock_id=config["source_clock_id"],
                    channel=config["channel"], peer_port=config["peer_port"])
    equal(config, expected, "immutable configuration")
    u64(config["transport_binding_id"], "transport binding", 1)
    equal(config["channel"], 1, "compiled native channel")
    integer(config["peer_port"], "peer port", 1, 65535)
    return config, bindings


def checked_candidate(value, config, path):
    fields(value, CANDIDATE_FIELDS, path)
    names = {"native_run_id": config["identity"]["native_run_id"],
             "owner_lifetime_id": config["identity"]["owner_lifetime_id"],
             "controller_instance_id": config["adapter_identity"]["controller_instance_id"],
             "source_id": config["contract"]["permitted_source"],
             "transport_binding_id": config["transport_binding_id"]}
    for name, expected in names.items():
        equal(value[name], expected, path + "." + name)
    for name in ("token", "request_sequence", "expected_transition_epoch", "producer_monotonic_us",
                 "producer_expiry_monotonic_us", "arrival_native_us"):
        u64(value[name], path + "." + name, 1)
    require(u64(value["producer_expiry_monotonic_us"], path + ".expiry") >
            u64(value["producer_monotonic_us"], path + ".creation"), "Producer expiry does not follow creation")
    equal(value["clock_mapping_epoch"], "0", path + ".absent map")
    require(u64(value["signing_timestamp_48"], path + ".signing timestamp") < (1 << 48),
            "Signing timestamp exceeded 48 bits")
    for name in ("acceleration_x_frd_m_s2", "acceleration_y_frd_m_s2", "acceleration_z_frd_m_s2"):
        require(type(value[name]) in (int, float) and math.isfinite(value[name]),
                "Nonfinite acceleration: " + path)
    for name, expected in {"protocol_version": 65536, "domain": 1, "profile": 1,
                           "target_system": 1, "target_component": 1, "frame": 1, "quantity": 1,
                           "reference_point": 1, "flags": 0, "channel": config["channel"],
                           "header_system": 42, "header_component": 191, "incoming_link_id": 7,
                           "parser_stage": 1, "peer_port": config["peer_port"]}.items():
        equal(value[name], expected, path + "." + name)
    equal(value["reserved"], [0, 0, 0, 0], path + ".reserved")
    integer(value["mavlink_sequence"], path + ".MAVLink sequence", 0, 255)
    integer(value["payload_length"], path + ".payload length", 103, 108)
    integer(value["frame_crc"], path + ".frame CRC", 0, 65535)
    require(type(value["signature"]) is list and len(value["signature"]) == 13,
            "Invalid retained signature metadata")
    for byte in value["signature"]:
        integer(byte, path + ".signature byte", 0, 255)
    equal(value["signature"][:7], [7] + list(int(value["signing_timestamp_48"]).to_bytes(6, "little")),
          path + ".signature link and timestamp")


def expected_status(candidate, source, evaluation, sequence):
    return {
        "native_run_id": candidate["native_run_id"],
        "owner_lifetime_id": candidate["owner_lifetime_id"],
        "controller_instance_id": candidate["controller_instance_id"],
        "source_id": candidate["source_id"],
        "transport_binding_id": candidate["transport_binding_id"],
        "request_sequence": candidate["request_sequence"],
        "status_sequence": str(sequence),
        "transition_epoch": source["transition_epoch"],
        "source_sequence": source["sequence"],
        "state_generation": source["physical_generation"],
        "arrival_native_us": candidate["arrival_native_us"],
        "evaluation_native_us": str(evaluation),
        "source_checked_native_us": source["checked_time_us"],
        "clock_mapping_epoch": "0", "protocol_version": 65536, "reason": 4,
        "domain": 1, "profile": 1, "execution_origin": 2, "outcome": 1,
        "arm_state": 1, "raw_nav_state": source["state"]["nav_state"],
        "research_mode_state": 0, "selector_state": 0, "estimator_valid": 0,
        "clock_map_valid": 0, "eligible_for_control": 0, "flags": 0,
        "target_system": 42, "target_component": 191, "reserved": [0, 0, 0, 0],
    }


def first_report(result, path):
    fields(result, "code snapshot record_count records has_operation_record "
           "operation_record_index grant_consumed action_performed", path)
    count = integer(result["record_count"], path + ".record_count", 1, 4)
    require(type(result["records"]) is list and len(result["records"]) == 4,
            "DiagnosticResult does not preserve all four report slots: " + path)
    return u64(result["records"][0]["report_sequence"], path + ".first report", 1)


def request_result(old, replay, sample, candidate):
    """One Disabled-selector request report; no guard evaluation or authority."""
    # The adapter polls its guard at the start of every diagnostic turn,
    # including a request later rejected by the Disabled selector. Preserve
    # that real rejection in the independent snapshot oracle.
    replay.state["rejection_count"] = str(int(replay.state["rejection_count"]) + 1)
    report = replay.report(sample, "Operation", "Rejected",
                           old.cause("SelectionRequired", "DiagnosticLocalReason"))
    correlation = old.correlation("Request")
    correlation["ingress"] = {
        "packet_kind": "Request", "source": candidate["source_id"],
        "session": candidate["transport_binding_id"],
        "controller_instance_id": candidate["controller_instance_id"],
        "transition_epoch": candidate["expected_transition_epoch"],
        "map_epoch": "0", "packet_sequence": candidate["request_sequence"],
        "acknowledged_status_sequence": "0",
    }
    correlation["authentication"] = {
        "mavlink2": True, "parser_authenticated": True,
        "configured_header_channel_source_binding": True,
        "configured_target_binding": True,
    }
    report["correlation"] = correlation
    return {"code": "Rejected", "snapshot": copy.deepcopy(replay.state),
            "record_count": 1, "records": [report] + [old.empty_report() for _ in range(3)],
            "has_operation_record": True, "operation_record_index": 0,
            "grant_consumed": False, "action_performed": False}


def check_timeline(data, config, bindings, old):
    sources = data["source_records"]
    transactions = data["transactions"]
    require(type(sources) is list and 1 <= len(sources) <= 256,
            "Missing/overflowing source archive")
    require(type(transactions) is list and len(transactions) <= 32,
            "Overflowing transaction archive")
    events = []
    for index, record in enumerate(sources):
        fields(record, "observation before after adapter_input result", "source record")
        events.append((first_report(record["result"], "source result"), "source", index))
    for index, transaction in enumerate(transactions):
        fields(transaction, "kind candidate source poll_input poll_result has_request "
               "request_input request_result disposition", "transaction")
        events.append((first_report(transaction["poll_result"], "transaction poll"), "transaction", index))
    events.sort()
    require(len({event[0] for event in events}) == len(events), "Duplicate event first report")
    replay = old.ClosedReplay(bindings)
    source = old.empty_source()
    source_index = transaction_index = status_sequence = 0
    last_token = 0
    first_source_time = 0

    for _, kind, index in events:
        if kind == "source":
            require(index == source_index, "Source archive reordered")
            source_index += 1
            record = sources[index]
            observation = record["observation"]
            fields(observation, "kind before after source_time_us", "source observation")
            source_kind = observation["kind"]
            require((index == 0 and source_kind == "Initial")
                    or (index > 0 and source_kind in {"Sample", "Transition"}),
                    "Missing/repeated Initial or invalid source kind")
            before_raw = raw_state(observation["before"], "source before")
            after_raw = raw_state(observation["after"], "source after")
            require(before_raw == (after_raw if index == 0 else source["state"]),
                    "Source before-state contradiction")
            changed = before_raw != after_raw
            require((source_kind == "Transition") == changed,
                    "Unchanged/fabricated/missing source transition")
            stamp = u64(observation["source_time_us"], "source time", 1)
            if first_source_time == 0:
                first_source_time = stamp
            fields(record["before"], SOURCE_FIELDS, "source before snapshot")
            floor = u64(record["before"]["checked_time_us"], "source checked floor")
            require(int(source["checked_time_us"]) <= floor <= stamp,
                    "Source checked floor regressed or exceeded observation")
            expected_before = copy.deepcopy(source)
            if index:
                expected_before["checked_time_us"] = str(floor)
            equal(record["before"], expected_before, "source before snapshot")
            generation = 1 if index == 0 else int(source["physical_generation"]) + int(changed)
            source = {"initialized": True, "state": copy.deepcopy(after_raw),
                      "sequence": str(index + 1), "transition_epoch": str(generation),
                      "physical_generation": str(generation),
                      "committed_time_us": str(stamp), "checked_time_us": str(stamp)}
            equal(record["after"], source, "source after snapshot")
            sample = old.physical(str(generation), str(stamp),
                                  bindings["controller_instance_id"], True)
            equal(record["adapter_input"], sample, "source adapter input")
            equal(record["result"], replay.turn(sample, generation), "source full result")
            continue

        require(index == transaction_index, "Transaction archive reordered")
        transaction_index += 1
        require(source["initialized"], "Transaction preceded actual Initial")
        record = transactions[index]
        equal(record["kind"], "Completed", "transaction kind")
        equal(record["has_request"], True, "actual request present")
        candidate = record["candidate"]
        checked_candidate(candidate, config, "transaction candidate")
        token = u64(candidate["token"], "candidate token", 1)
        require(token > last_token, "Candidate token did not advance")
        last_token = token
        fields(record["source"], SOURCE_FIELDS, "transaction source")
        transaction_source = copy.deepcopy(source)
        floor = u64(record["source"]["checked_time_us"], "transaction checked floor")
        require(int(source["checked_time_us"]) <= floor,
                "Transaction checked floor regressed")
        transaction_source["checked_time_us"] = str(floor)
        equal(record["source"], transaction_source, "transaction current source")
        equal(candidate["expected_transition_epoch"], source["transition_epoch"],
              "candidate transition")
        fields(record["poll_input"], "generation observer_continuous request_guard_context map",
               "poll input")
        evaluation = u64(record["poll_input"]["request_guard_context"]["validator_receive_time_us"],
                         "evaluation native time", 1)
        require(max(floor, u64(candidate["arrival_native_us"], "arrival", 1)) <= evaluation,
                "Arrival or source floor exceeds evaluation")
        sample = old.physical(source["physical_generation"], str(evaluation),
                              bindings["controller_instance_id"], True)
        equal(record["poll_input"], sample, "actual poll input")
        equal(record["poll_result"], replay.turn(sample, int(source["physical_generation"])),
              "actual full poll result")
        equal(record["request_input"], sample, "same-generation request input")
        equal(record["request_result"], request_result(old, replay, sample, candidate),
              "actual full SelectionRequired request result")
        disposition = record["disposition"]
        fields(disposition, "token kind has_actual_request has_status status", "disposition")
        equal(disposition["token"], candidate["token"], "disposition token")
        equal(disposition["kind"], "Completed", "completed disposition")
        equal(disposition["has_actual_request"], True, "actual request disposition")
        equal(disposition["has_status"], True, "correlated status present")
        fields(disposition["status"], STATUS_FIELDS, "status")
        status_sequence += 1
        equal(disposition["status"], expected_status(candidate, transaction_source,
                                                     evaluation, status_sequence),
              "status projection from actual request")
        source["checked_time_us"] = str(evaluation)

    require(source_index == len(sources) and transaction_index == len(transactions),
            "Unreplayed source or transaction")
    closure = data["closure"]
    fields(closure, "cause fault attempted retained_source adapter_input result", "closure")
    equal(closure["cause"], "OwnerStop", "normal first closure")
    equal(closure["fault"], "None", "closure fault")
    attempted = closure["attempted"]
    fields(attempted, "kind before after source_time_us", "Stop observation")
    stop_time = u64(attempted["source_time_us"], "Stop time", 1)
    require(stop_time >= int(source["checked_time_us"]), "Stop regressed source time")
    equal(attempted, {"kind": "Stop", "before": source["state"],
                      "after": source["state"], "source_time_us": str(stop_time)}, "Stop")
    source["checked_time_us"] = str(stop_time)
    equal(closure["retained_source"], source, "closure retained source")
    sample = old.physical(source["physical_generation"], str(stop_time),
                          bindings["controller_instance_id"], True)
    equal(closure["adapter_input"], sample, "Stop adapter input")
    equal(closure["result"], replay.turn(sample, int(source["physical_generation"]), True),
          "full closure result")
    equal(data["summary"], {"state": "Stopped", "fault": "None", "source": source,
                            "source_count": len(sources), "transaction_count": len(transactions),
                            "last_status_sequence": str(status_sequence), "has_closure": True,
                            "evidence_complete": True}, "owner summary")
    return {"source": source, "first_source_time": first_source_time, "stop_time": stop_time,
            "report_count": replay.report_count, "status_count": status_sequence,
            "transaction_tokens": [int(record["candidate"]["token"]) for record in transactions]}


def check_worker(worker, config, timeline):
    fields(worker, "summary rejections deliveries", "worker")
    summary = worker["summary"]
    fields(summary, "local_port peer_port channel reject_counts busy_drop_count wrong_peer_count "
           "arrival_clock_count completed_replay_count discarded_replay_count status_attempt_count "
           "status_sent_count status_failed_count last_arrival_native_us last_token reject_record_count "
           "delivery_record_count last_send_state retired", "worker summary")
    integer(summary["local_port"], "local port", 1, 65535)
    equal(summary["peer_port"], config["peer_port"], "peer port")
    require(summary["local_port"] != summary["peer_port"], "Endpoint ports collide")
    equal(summary["channel"], config["channel"], "channel")
    equal(summary["retired"], True, "worker retirement")
    equal(summary["arrival_clock_count"], "0", "no arrival clock fault")
    require(type(summary["reject_counts"]) is list and len(summary["reject_counts"]) == 16,
            "Wrong parser rejection count shape")
    actual_counts = [u64(value, "reject_counts") for value in summary["reject_counts"]]
    require(actual_counts[0] == 0 and actual_counts[1] == 0 and actual_counts[15] == 0,
            "Published/Busy/ResourceFault counted as parser rejection")
    busy = u64(summary["busy_drop_count"], "busy drops")
    wrong_peer = u64(summary["wrong_peer_count"], "wrong peers")
    u64(summary["last_arrival_native_us"], "last arrival")
    last_token = u64(summary["last_token"], "last staged token")
    rejections = worker["rejections"]
    deliveries = worker["deliveries"]
    require(type(rejections) is list and len(rejections) <= 64 and type(deliveries) is list
            and len(deliveries) <= 33, "Worker ledger exceeded fixed capacity")
    equal(summary["reject_record_count"], len(rejections), "reject record count")
    equal(summary["delivery_record_count"], len(deliveries), "delivery record count")
    observed_counts = [0] * 16
    observed_wrong_peer = 0
    last_reject_time = 0
    for index, record in enumerate(rejections):
        fields(record, "kind stage arrival_native_us peer_port", "worker rejection")
        require(record["kind"] in {"Parser", "WrongPeer"},
                "Clock fault cannot pass a normal native run")
        require(record["stage"] in STAGES, "Unknown parser stage in rejection")
        arrival = u64(record["arrival_native_us"], "rejection arrival", 1)
        require(last_reject_time <= arrival <= int(summary["last_arrival_native_us"]),
                "Rejection clock regressed or exceeds worker floor")
        last_reject_time = arrival
        integer(record["peer_port"], "rejection peer port", 0, 65535)
        if record["kind"] == "Parser":
            require(record["stage"] not in {"Published", "Busy", "ResourceFault"}
                    and record["peer_port"] == config["peer_port"],
                    "Parser rejection was not bound to configured peer")
            observed_counts[STAGES.index(record["stage"])] += 1
        else:
            equal(record["stage"], "WrongIdentity", "wrong peer stage")
            observed_wrong_peer += 1
    equal(actual_counts, observed_counts, "checked parser rejection counts")
    equal(wrong_peer, observed_wrong_peer, "wrong peer count")
    completed = discarded = 0
    seen_tokens = set()
    previous_token = previous_signature = 0
    completed_tokens = []
    for record in deliveries:
        fields(record, "token status_sequence signing_timestamp_48 disposition send_state "
               "signed_size replay_committed has_status", "worker delivery")
        token = u64(record["token"], "delivery token", 1)
        require(token > previous_token and token not in seen_tokens and token <= last_token,
                "Delivery token reordered, duplicated or beyond staged floor")
        previous_token = token
        seen_tokens.add(token)
        require(record["disposition"] in {"Completed", "DiscardedStale", "DiscardedForeign",
                                          "DiscardedClosed", "Faulted"}, "Unknown disposition")
        if record["disposition"] == "Completed":
            completed += 1
            completed_tokens.append(token)
            equal(record["replay_committed"], True, "completed replay committed")
            equal(record["has_status"], True, "completed status projected")
            equal(record["send_state"], "Sent", "local status send")
            equal(record["status_sequence"], str(completed), "status sequence delivery")
            integer(record["signed_size"], "signed status size", 157, 161)
            signing = u64(record["signing_timestamp_48"], "outgoing signing timestamp", 1)
            require(previous_signature < signing < (1 << 48), "Outgoing signing floor regressed")
            previous_signature = signing
        else:
            discarded += 1
            equal(record["replay_committed"], False, "discard did not commit replay")
            equal(record["has_status"], False, "unarchived close/status not accepted as delivery")
            equal(record["send_state"], "None", "discard has no send")
            equal(record["status_sequence"], "0", "discard has no status sequence")
            equal(record["signed_size"], 0, "discard has no signed frame")
            equal(record["signing_timestamp_48"], "0", "discard has no signing timestamp")
    equal(completed_tokens, timeline["transaction_tokens"], "completed replay versus actual owner turns")
    equal(u64(summary["completed_replay_count"], "completed replay count"), completed,
          "completed replay count")
    equal(u64(summary["discarded_replay_count"], "discarded replay count"), discarded,
          "discarded replay count")
    for name in ("status_attempt_count", "status_sent_count"):
        equal(u64(summary[name], name), completed, name)
    equal(summary["status_failed_count"], "0", "no failed status send")
    equal(summary["last_send_state"], "Sent" if completed else "None", "last send state")
    require(last_token >= previous_token and (last_token > 0 or not deliveries),
            "Last staged token contradicts delivery ledger")
    require(int(summary["last_arrival_native_us"]) >= max(
        (int(tx["candidate"]["arrival_native_us"]) for tx in timeline["transactions"]), default=0),
        "Worker last arrival precedes authenticated request")
    return {"completed": completed, "discarded": discarded, "rejections": len(rejections),
            "busy_drops": busy}


def check_lifecycle(lifecycle):
    fields(lifecycle, "bootstrap_count claim_count stack_size_bytes stack_guard_bytes "
           "stack_marker_offset_bytes commander_stop_result bridge_fault source_done "
           "worker_joined stack_measured observation_window_us waited wait_completed", "lifecycle")
    for name, value in {"bootstrap_count": 1, "claim_count": 1, "commander_stop_result": "0",
                        "bridge_fault": 0, "source_done": True, "worker_joined": True,
                        "stack_measured": True, "waited": True, "wait_completed": True}.items():
        equal(lifecycle[name], value, "lifecycle." + name)
    equal(lifecycle["observation_window_us"], "2000000", "native observation window")
    stack = u64(lifecycle["stack_size_bytes"], "stack size", 1)
    guard = u64(lifecycle["stack_guard_bytes"], "stack guard")
    marker = u64(lifecycle["stack_marker_offset_bytes"], "stack marker", 1)
    require(guard < stack and 49152 <= marker < stack - guard,
            "Native diagnostic stack criterion not established")


def _check_archive(data, expected_bindings=None):
    old = prior_closed_oracle()
    fields(data, "schema header source_records transactions closure summary worker lifecycle footer", "trace")
    equal(data["schema"], SCHEMA, "schema")
    config, bindings = checked_configuration(data["header"], old)
    if expected_bindings is not None:
        fields(expected_bindings, "permitted_source controller_instance_id owner_lifetime_id "
               "native_run_id transport_binding_id producer_lifetime_id source_clock_id channel "
               "peer_port local_port", "external bindings")
        for name in ("permitted_source", "controller_instance_id", "owner_lifetime_id",
                     "native_run_id", "producer_lifetime_id"):
            equal(bindings[name], expected_bindings[name], "external binding." + name)
        for name in ("transport_binding_id", "source_clock_id"):
            equal(config[name], expected_bindings[name], "external binding." + name)
        for name in ("channel", "peer_port"):
            equal(config[name], expected_bindings[name], "external binding." + name)
        equal(data["worker"]["summary"]["local_port"], expected_bindings["local_port"],
              "external local port")
    timeline = check_timeline(data, config, bindings, old)
    timeline["transactions"] = data["transactions"]
    worker = check_worker(data["worker"], config, timeline)
    check_lifecycle(data["lifecycle"])
    require(timeline["stop_time"] - timeline["first_source_time"] >= 2_000_000,
            "Source-bound observation window incomplete")
    fields(data["footer"], "source_count transaction_count adapter_report_count "
           "last_adapter_report_sequence last_status_sequence completed_replay_count "
           "status_sent_count", "footer")
    equal(data["footer"], {
        "source_count": len(data["source_records"]),
        "transaction_count": len(data["transactions"]),
        "adapter_report_count": timeline["report_count"],
        "last_adapter_report_sequence": str(timeline["report_count"]),
        "last_status_sequence": str(timeline["status_count"]),
        "completed_replay_count": str(worker["completed"]),
        "status_sent_count": str(worker["completed"]),
    }, "footer")
    return {"result": "PASS", "scope": "negative_native_archive_semantics_only",
            "source_count": len(data["source_records"]),
            "transaction_count": len(data["transactions"]),
            "rejection_count": worker["rejections"], "busy_drop_count": worker["busy_drops"],
            "adapter_report_count": timeline["report_count"],
            "first_source_time_us": str(timeline["first_source_time"]),
            "stop_time_us": str(timeline["stop_time"]),
            "authority": "none", "guidance_clock_map": "not_qualified"}


def check_archive(data, expected_bindings=None):
    try:
        return _check_archive(data, expected_bindings)
    except (KeyError, IndexError, TypeError, AttributeError) as error:
        raise EvidenceError("Malformed nested archive value") from error


def hex_hash(value, path):
    require(type(value) is str and HASH.fullmatch(value) is not None,
            "Invalid SHA-256: " + path)
    return value


def checked_build(build_root, reviewed_sha256):
    build_root = Path(build_root).resolve()
    manifest_path = build_root / "manifest.json"
    equal(sha256_file(manifest_path), hex_hash(reviewed_sha256, "reviewed build hash"),
          "reviewed build manifest hash")
    manifest = decode(manifest_path.read_bytes())
    equal(manifest["schema"], "research-c1e-native-build/v1", "build schema")
    equal(manifest["status"], "build_only_pending_native_review", "build completion")
    equal(manifest["runs"], [], "build-only scope")
    equal(manifest["changed_source_inputs"], [], "stable source inputs")
    equal(manifest["gazebo_before"], manifest["gazebo_after"], "stock simulator preservation")
    equal(manifest["build"]["configure_exit"], 0, "configure exit")
    equal(manifest["build"]["build_exit"], 0, "build exit")
    equal(Path(manifest["build"]["directory"]).resolve(), build_root / "build", "build directory")
    audit = manifest["configuration_audit"]
    equal(audit["enabled_modules"], ["CONFIG_MODULES_COMMANDER"], "enabled modules")
    equal(audit["enabled_drivers"], [], "enabled output drivers")
    equal(audit["builtins"], BUILTINS, "linked builtin allowlist")
    for name in ("boardconfig_sha256", "apps_sha256", "compile_commands_sha256"):
        hex_hash(audit[name], name)
    for name, registry in (("source_registry_sha256", "source_hashes.json"),
                           ("stack_registry_sha256", "stack_files.json")):
        equal(sha256_file(build_root / registry), hex_hash(manifest[name], name), name)
    equal(sha256_file(build_root / "generated_files.json"),
          hex_hash(manifest["generator"]["registry_sha256"], "generator registry"),
          "generated binding registry")
    executable = build_root / "build/bin/px4"
    equal(sha256_file(executable), hex_hash(manifest["executable_sha256"], "executable hash"),
          "reviewed executable")
    generated = Path(manifest["generator"]["python_output"]).resolve()
    require(generated.is_relative_to(build_root / "generated/python"),
            "Generated W2 binding escaped build root")
    registry = decode((build_root / "generated_files.json").read_bytes())
    require(type(registry) is dict and type(registry.get("files")) is dict
            and "python/diagnostic.py" in registry["files"],
            "Generated W2 binding absent from registry")
    equal(sha256_file(generated), hex_hash(registry["files"]["python/diagnostic.py"], "W2 binding hash"),
          "generated W2 binding")
    return manifest


def check_topics(raw):
    ansi = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")
    lines = [ansi.sub("", line).strip() for line in raw.decode("utf-8").splitlines()]
    markers = []
    for name in ("C1E_TOPICS_BEGIN", "C1E_TOPICS_END", "C1E_STARTUP_COMPLETE"):
        places = [index for index, line in enumerate(lines) if line == name]
        require(len(places) == 1, "Missing/duplicate runtime marker: " + name)
        markers.append(places[0])
    begin, end, complete = markers
    require(begin < end < complete, "Topic/completion order changed")
    rows = [line for line in lines[begin + 1:end] if line]
    require(rows and rows[0].split() == ["TOPIC", "NAME", "INST", "#SUB", "#Q", "SIZE", "PATH"],
            "Missing advertised topic inventory")
    allowed = {"actuator_armed", "config_overrides", "event", "failsafe_flags", "failure_detector_status",
               "health_report", "led_control", "log_message", "mavlink_log", "parameter_update",
               "power_button_state", "tune_control", "vehicle_control_mode", "vehicle_status"}
    topics = set()
    for row in rows[1:]:
        match = re.fullmatch(r"([a-z][a-z0-9_]*)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\S+)", row)
        require(match is not None, "Malformed advertised topic row")
        name, instance, _, queue, size, _ = match.groups()
        require(name in allowed and name not in topics and int(instance) == 0
                and int(queue) > 0 and int(size) > 0,
                "Forbidden/duplicate advertised topic: " + name)
        topics.add(name)
    return sorted(topics)


def read_checkpoint(raw):
    """Parse the sole read-only source checkpoint from an owned PX4 stdout."""
    ansi = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")
    lines = [ansi.sub("", line).strip() for line in raw.decode("utf-8").splitlines()]
    markers = [line for line in lines if line.startswith("C1E_SOURCE_CHECKPOINT")]
    require(len(markers) <= 1, "Repeated source checkpoint marker")
    if not markers:
        return None
    match = re.fullmatch(
        r"C1E_SOURCE_CHECKPOINT epoch=(0|[1-9][0-9]*) arm=([0-9]+) nav=([0-9]+) "
        r"sequence=(0|[1-9][0-9]*) checked_hrt_us=(0|[1-9][0-9]*)", markers[0])
    require(match is not None, "Malformed source checkpoint marker")
    epoch, arm, nav, sequence, checked = match.groups()
    u64(epoch, "checkpoint epoch", 1)
    u64(sequence, "checkpoint sequence", 3)
    u64(checked, "checkpoint checked HRT", 1)
    require(integer(int(arm), "checkpoint arm", 1, 1) == 1
            and integer(int(nav), "checkpoint nav", 0, 30) in NAV,
            "Source checkpoint is not disarmed or has invalid nav state")
    return {"epoch": epoch, "arm": int(arm), "nav": int(nav),
            "sequence": sequence, "checked_hrt_us": checked}


def check_checkpoint(raw, archive):
    point = read_checkpoint(raw)
    require(point is not None, "Missing source-bound checkpoint marker")
    sequence = int(point["sequence"])
    sources = archive["source_records"]
    require(1 <= sequence <= len(sources), "Checkpoint source sequence missing from archive")
    retained = sources[sequence - 1]["after"]
    equal(retained["sequence"], point["sequence"], "checkpoint source sequence")
    equal(retained["transition_epoch"], point["epoch"], "checkpoint transition epoch")
    equal(retained["state"], {"arming_state": point["arm"], "nav_state": point["nav"]},
          "checkpoint disarmed source state")
    checked = int(point["checked_hrt_us"])
    require(int(retained["checked_time_us"]) <= checked,
            "Checkpoint predates its source publication")
    require(len(archive["transactions"]) == 1, "Checkpoint requires one native request")
    tx = archive["transactions"][0]
    equal(tx["candidate"]["expected_transition_epoch"], point["epoch"],
          "signed request not bound to observed epoch")
    equal(tx["source"]["transition_epoch"], point["epoch"],
          "source transitioned after checkpoint")
    require(int(tx["source"]["sequence"]) >= sequence
            and int(tx["source"]["checked_time_us"]) >= checked
            and int(tx["candidate"]["arrival_native_us"]) >= checked,
            "Request preceded source checkpoint")
    return point


def checked_raw_datagrams(request, status, archive, bindings):
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import send_w1_request as wire

    def payload(packet, message_id, crc_extra, sender, link, minimum, maximum):
        require(len(packet) >= minimum + 25 and packet[0] == 0xFD
                and packet[2:4] == b"\x01\x00" and packet[5:7] == sender
                and int.from_bytes(packet[7:10], "little") == message_id
                and len(packet) == packet[1] + 25 and minimum <= packet[1] <= maximum
                and packet[-13] == link, "Wrong raw signed frame envelope")
        body = packet[10:10 + packet[1]]
        checksum = wire.x25(packet[1:10] + body + bytes((crc_extra,))).to_bytes(2, "little")
        require(packet[10 + packet[1]:12 + packet[1]] == checksum, "Raw signed frame CRC changed")
        return body

    req = wire.w1.decode_request(payload(request, 200010, 187, bytes((42, 191)), 7, 103, 108))
    returned = wire.w1.decode_status(payload(status, 200011, 159, bytes((1, 1)), 8, 132, 136))
    require(len(archive["transactions"]) == 1, "Runtime campaign expects one actual request")
    candidate = archive["transactions"][0]["candidate"]
    aliases = {"transport_session_id": "transport_binding_id"}
    for wire_name in wire.w1._REQUEST_FIELDS:
        native_name = aliases.get(wire_name, wire_name)
        observed = req[wire_name]
        retained = candidate[native_name]
        if type(retained) is str:
            observed = str(observed)
        elif type(retained) is list:
            observed = list(observed)
        equal(observed, retained, "raw request versus candidate." + native_name)
    equal(request[4], candidate["mavlink_sequence"], "raw request MAVLink sequence")
    equal(int.from_bytes(request[-12:-6], "little"),
          int(candidate["signing_timestamp_48"]), "raw signing timestamp")
    equal(list(request[-13:]), candidate["signature"], "retained authenticated signature")
    equal(int.from_bytes(request[10 + request[1]:12 + request[1]], "little"),
          candidate["frame_crc"], "retained authenticated CRC")
    projected = archive["transactions"][0]["disposition"]["status"]
    for wire_name in wire.w1._STATUS_FIELDS:
        native_name = aliases.get(wire_name, wire_name)
        observed = returned[wire_name]
        retained = projected[native_name]
        if type(retained) is str:
            observed = str(observed)
        elif type(retained) is list:
            observed = list(observed)
        equal(observed, retained, "raw returned status versus archive." + native_name)
    equal(returned["reason"], 4, "actual native rejection")
    return returned


def check_run(runtime, external_bindings, build_root, reviewed_build_sha256):
    runtime = Path(runtime).resolve()
    build_root = Path(build_root).resolve()
    manifest = checked_build(build_root, reviewed_build_sha256)
    run = decode((runtime / "run.json").read_bytes())
    fields(run, "schema build_root command instance runtime bindings scope status pid start_monotonic_ns "
           "end_monotonic_ns forced_shutdown exit_code key_fingerprint_sha256 artifact_sha256 "
           "build_manifest_sha256 source_registry_sha256 stack_registry_sha256 "
           "generated_registry_sha256 executable_sha256", "run manifest")
    equal(run["schema"], RUN_SCHEMA, "run schema")
    equal(run["runtime"], str(runtime), "owned runtime path")
    equal(run["build_root"], str(build_root), "reviewed build root")
    equal(run["bindings"], external_bindings, "external run identities")
    equal(run["scope"], "negative_diagnostic_disarmed_output_disabled", "run scope")
    equal(run["status"], "captured_unscored", "runtime capture status")
    equal(run["forced_shutdown"], False, "normal exit required")
    equal(run["exit_code"], 0, "PX4 exit code")
    instance = integer(run["instance"], "instance", 1)
    integer(run["pid"], "PID", 1)
    start = integer(run["start_monotonic_ns"], "host start", 1)
    end = integer(run["end_monotonic_ns"], "host end", start)
    executable = build_root / "build/bin/px4"
    equal(run["command"], [str(executable), "-i", str(instance), "-w", str(runtime),
                           "-s", str(runtime / "startup.sh")], "owned process command")
    for name, expected in (("build_manifest_sha256", reviewed_build_sha256),
                           ("source_registry_sha256", manifest["source_registry_sha256"]),
                           ("stack_registry_sha256", manifest["stack_registry_sha256"]),
                           ("generated_registry_sha256", manifest["generator"]["registry_sha256"]),
                           ("executable_sha256", manifest["executable_sha256"])):
        equal(run[name], expected, "run." + name)
    hex_hash(run["key_fingerprint_sha256"], "key fingerprint")
    fields(run["artifact_sha256"], " ".join(ARTIFACTS), "runtime artifact hashes")
    for filename in ARTIFACTS:
        equal(sha256_file(runtime / filename), hex_hash(run["artifact_sha256"][filename], filename),
              "runtime artifact hash: " + filename)
    equal((runtime / "startup.sh").read_bytes(),
          (Path(__file__).resolve().parent / "startup.sh").read_bytes(), "reviewed startup source")
    trace = (runtime / "trace.json").read_bytes()
    equal(trace, (runtime / "trace-repeat.json").read_bytes(), "read-only repeated export")
    archive = decode(trace)
    semantic = check_archive(archive, external_bindings)
    point = check_checkpoint((runtime / "stdout.log").read_bytes(), archive)
    require(len(archive["transactions"]) == 1, "Native campaign requires one signed request")
    worker_summary = archive["worker"]["summary"]
    equal(worker_summary["delivery_record_count"], 1, "exactly one worker delivery")
    equal(worker_summary["reject_record_count"], 0, "no extra parser traffic")
    equal(worker_summary["busy_drop_count"], "0", "no extra busy traffic")
    equal(worker_summary["wrong_peer_count"], "0", "no foreign peer traffic")
    equal(worker_summary["reject_counts"], ["0"] * len(STAGES), "no extra rejected traffic")
    sender = decode((runtime / "sender.json").read_bytes())
    fields(sender, "request_sent_count status_received_count signature_verified "
           "request_sha256 status_sha256 status", "sender capture")
    equal(sender["request_sent_count"], 1, "one request datagram")
    equal(sender["status_received_count"], 1, "one returned status datagram")
    equal(sender["signature_verified"], True, "sender independently verified status MAC")
    equal(sender["request_sha256"], run["artifact_sha256"]["request.bin"], "request capture hash")
    equal(sender["status_sha256"], run["artifact_sha256"]["status.bin"], "status capture hash")
    status = checked_raw_datagrams((runtime / "request.bin").read_bytes(),
                                   (runtime / "status.bin").read_bytes(), archive, external_bindings)
    comparable_status = copy.deepcopy(status)
    comparable_status["reserved"] = list(comparable_status["reserved"])
    equal(sender["status"], comparable_status, "sender parsed returned status")
    require(start <= int(semantic["first_source_time_us"]) * 1000 + 999
            and start <= int(point["checked_hrt_us"]) * 1000 + 999
            and int(point["checked_hrt_us"]) * 1000 <= end
            and int(semantic["stop_time_us"]) * 1000 <= end,
            "Native source times outside owned process lifetime")
    semantic.update(scope="reviewed_negative_native_run", advertised_topics=check_topics(
        (runtime / "stdout.log").read_bytes()), key_fingerprint=run["key_fingerprint_sha256"])
    return semantic


def check_campaign(run_a, bindings_a, run_b, bindings_b, build_root, reviewed_build_sha256):
    first = check_run(run_a, bindings_a, build_root, reviewed_build_sha256)
    second = check_run(run_b, bindings_b, build_root, reviewed_build_sha256)
    require(Path(run_a).resolve() != Path(run_b).resolve(), "Runs share one runtime directory")
    for name in ("native_run_id", "owner_lifetime_id", "transport_binding_id",
                 "producer_lifetime_id", "source_clock_id", "local_port", "peer_port"):
        require(bindings_a[name] != bindings_b[name], "Runs reused identity or endpoint: " + name)
    require(first["key_fingerprint"] != second["key_fingerprint"], "Runs reused signing key")
    return {"result": "PASS", "scope": "two_reviewed_disarmed_negative_native_runs",
            "runs": [first, second]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    args = parser.parse_args()
    try:
        result = check_archive(decode(args.archive.read_bytes()))
    except (EvidenceError, OSError, UnicodeError) as error:
        print(json.dumps({"result": "FAIL", "error": str(error)}))
        return 1
    print(json.dumps(result, sort_keys=True, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
