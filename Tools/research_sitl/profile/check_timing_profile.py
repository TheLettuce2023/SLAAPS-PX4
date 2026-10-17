"""Strict P2T1 synthetic-evidence oracle; never measures a live link or vehicle."""

import copy
import hashlib
import json
import pathlib
import subprocess
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "wire"))
from codec import decode_status, encode_status  # noqa: E402


SUMMARY = {"summary": "injected-only", "stream_requests": 100, "boundary_cases": 13,
           "cache_fault_cases": 9, "heartbeat_events": 3, "heartbeat_accesses": 2001}
FIELDS = set("case sequence provenance created_us sent_us received_us admitted_us accessed_us "
             "status_emitted_us status_returned_us offset_us uncertainty_us map_from_us "
             "map_until_us validity_us hard_age_us eligible request_status access_status".split())
NON_INTEGER = {"case", "provenance", "eligible", "request_status", "access_status"}


def require(condition, reason):
    if not condition:
        raise ValueError(reason)


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, "duplicate JSON key")
        result[key] = value
    return result


def expected_cases():
    """Independent, fixed test schedule; no expectations taken from emitter output."""
    cases = []
    for stream in ("periodic", "jitter"):
        created, report = 200000, 7
        for index in range(50):
            cases.append((stream, index + 1, created, 12000, 4000, 0, 60000, report))
            period = 24000 if stream == "jitter" and index % 2 else 20000
            created += period
            report += period // 1000 + 1  # request, initial access, intervening 1 ms accesses
    for name, receive, queue, access, limit in (
        ("receive_below", 11999, 4000, 0, 60000),
        ("receive_at", 12000, 4000, 0, 60000),
        ("receive_above", 12001, 4000, 0, 60000),
        ("queue_below", 12000, 3999, 0, 60000),
        ("queue_at", 12000, 4000, 0, 60000),
        ("queue_above", 12000, 4001, 0, 60000),
        ("age_at", 52000, 4000, 0, 60000),
        ("age_above", 52001, 4000, 0, 60000),
        ("retained_at", 12000, 4000, 40000, 60000),
        ("retained_above", 12000, 4000, 40001, 60000),
        ("delayed_access", 12000, 4000, 60000, 60000),
        ("expiry_before_isolation", 12000, 4000, 59999, 100000),
        ("expiry_at_isolation", 12000, 4000, 60000, 100000),
    ):
        cases.append((name, 1, 200000, receive, queue, access, limit, 7))
    return cases


def verify_status(payload_hex, *, sequence, time, report, decision, age, accepted,
                  request, has_last):
    require(type(payload_hex) is str and len(payload_hex) == 212, "missing/excess status payload")
    payload = bytes.fromhex(payload_hex)
    require(payload.hex() == payload_hex, "noncanonical payload hex")
    status = decode_status(payload)
    require(encode_status(status) == payload, "status codec round trip")
    expected = {
        "controller_instance_id": 11, "transition_epoch": 3, "clock_mapping_epoch": 41,
        "source_id": 21, "session_id": 31, "correlation_sequence": sequence if request else 0,
        "validator_time_us": time, "status_sequence": report, "decision_code": decision,
        "conservative_age_us": age, "status_class": 1 if request else 5,
        "last_accepted_sequence": sequence if has_last else 0,
        "target_system": 1, "target_component": 1,
        "protocol_version": 65536, "arm_state": 2, "research_mode_state": 2,
        "session_state": 2, "transport_state": 1, "clock_mapping_state": 1,
        "first_latched_code": 0, "rejection_count": 1 if accepted else 2,
        "status_flags": (59 if accepted else 40 if request else
                         24 if decision == 20 else 16 if has_last else 0),
    }
    require(status == expected, "complete status differs from independent oracle")


def validate(records):
    cases = expected_cases()
    require(type(records) is list and len(records) == len(cases) + 1, "incomplete/excess evidence")
    require(records[-1] == SUMMARY and all(type(records[-1][key]) is type(value)
            for key, value in SUMMARY.items()), "missing/incorrect terminal summary")
    nominal_violations = 0
    for row, case in zip(records[:-1], cases):
        require(type(row) is dict and set(row) == FIELDS, "missing/unexpected event field")
        for field in FIELDS - NON_INTEGER:
            require(type(row[field]) is int and 0 <= row[field] < 2**64, "invalid integer timestamp/id")
        name, sequence, created, receive, queue, access, limit, report = case
        admitted = created + 1000000 + receive + queue
        accessed = admitted + access
        age = receive + queue + 4000
        request_ok = age <= limit
        access_age = age + access
        expired = accessed >= created + 1000000 + 80000 - 4000
        eligible = request_ok and access_age <= limit and not expired
        expected = dict(case=name, sequence=sequence, provenance="injected-us-v1",
                        created_us=created, sent_us=created, received_us=created + 1000000 + receive,
                        admitted_us=admitted, accessed_us=accessed, status_emitted_us=admitted,
                        status_returned_us=admitted, offset_us=1000000, uncertainty_us=4000,
                        map_from_us=0, map_until_us=10000000, validity_us=80000,
                        hard_age_us=limit, eligible=eligible)
        require(type(row["eligible"]) is bool, "eligibility is not Boolean")
        for field, value in expected.items():
            require(row[field] == value, "event mismatch: " + field)
        require(row["map_from_us"] <= created and created + 80000 <= row["map_until_us"],
                "map does not cover entire lifetime")
        # 20 ms nominal target is NOT the 60 ms guard rejection threshold.
        if receive > 12000 or queue > 4000 or age > 20000:
            nominal_violations += 1
        verify_status(row["request_status"], sequence=sequence, time=admitted, report=report,
                      decision=1 if request_ok else 20, age=age, accepted=request_ok,
                      request=True, has_last=request_ok)
        access_decision = 36 if not request_ok else 22 if expired else 20 if access_age > limit else 1
        verify_status(row["access_status"], sequence=sequence, time=accessed, report=report + 1,
                      decision=access_decision, age=access_age if access_decision in (1, 20) else 0,
                      accepted=eligible, request=False, has_last=request_ok)
    require(nominal_violations == 4, "nominal timing violations conflated with hard rejection")
    return len(cases)


def mutation_tests(records):
    """Each corrupted package must fail; acceptance of one is a test failure."""
    candidates = [records[:-1], records[1:], records + [records[0]], list(reversed(records))]
    for key, value in (
        ("received_us", None), ("admitted_us", True), ("created_us", -1),
        ("accessed_us", 0), ("uncertainty_us", 0), ("uncertainty_us", 4001),
        ("map_until_us", 200001), ("hard_age_us", 100000), ("eligible", False),
        ("provenance", "measured-pi"), ("status_returned_us", None),
        ("request_status", records[1]["request_status"]), ("access_status", "00" * 106),
        ("actuator_output", 0),
    ):
        changed = copy.deepcopy(records)
        changed[0][key] = value
        candidates.append(changed)
    missing = copy.deepcopy(records)
    del missing[0]["sent_us"]
    candidates.append(missing)
    # Structurally valid alternate statuses must still fail the history oracle.
    rejected_index = next(index for index, row in enumerate(records[:-1]) if row["case"] == "age_above")
    for index, field, value in (
        (0, "rejection_count", 123), (0, "status_flags", 58),
        (rejected_index, "arm_state", 1), (rejected_index, "research_mode_state", 1),
        (rejected_index, "transport_state", 2), (rejected_index, "session_state", 3),
        (rejected_index, "clock_mapping_state", 2),
    ):
        changed = copy.deepcopy(records)
        status = decode_status(bytes.fromhex(changed[index]["request_status"]))
        status[field] = value
        changed[index]["request_status"] = encode_status(status).hex()
        candidates.append(changed)
    for changed in candidates:
        try:
            validate(changed)
        except (ValueError, TypeError):
            continue
        raise ValueError("checker accepted deliberately malformed evidence")
    try:
        json.loads('{"a":1,"a":2}', object_pairs_hook=unique_object)
    except ValueError:
        return len(candidates) + 1
    raise ValueError("checker accepted duplicate JSON keys")


def main():
    require(len(sys.argv) == 2, "usage: check_timing_profile.py <TimingProfileTest executable>")
    executable = pathlib.Path(sys.argv[1]).resolve(strict=True)
    completed = subprocess.run([str(executable)], capture_output=True, text=True, timeout=30, check=False)
    require(completed.returncode == 0 and not completed.stderr, "profile fixture failed: " + completed.stderr)
    records = [json.loads(line, object_pairs_hook=unique_object) for line in completed.stdout.splitlines()]
    count = validate(records)
    mutations = mutation_tests(records)
    print(json.dumps({"scope": "injected offline profile only", "records_verified": count,
                      "malformed_packages_rejected": mutations, "nominal_target_violations_expected": 4,
                      "executable_sha256": hashlib.sha256(executable.read_bytes()).hexdigest(),
                      "evidence_sha256": hashlib.sha256(completed.stdout.encode()).hexdigest()}, sort_keys=True))


if __name__ == "__main__":
    main()
