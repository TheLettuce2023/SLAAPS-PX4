"""Independent closed-schedule oracle for injected P2T2 status-delivery evidence."""

import copy
import hashlib
import json
import pathlib
import subprocess
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "wire"))
from codec import decode_status, encode_status  # noqa: E402


def require(condition, message):
    if not condition:
        raise ValueError(message)


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, "duplicate JSON key")
        result[key] = value
    return result


def invalid_constant(value):
    raise ValueError("non-finite JSON constant: " + value)


def strict_equal(actual, expected):
    if type(actual) is not type(expected):
        return False
    if isinstance(expected, dict):
        return actual.keys() == expected.keys() and all(strict_equal(actual[k], v) for k, v in expected.items())
    if isinstance(expected, list):
        return len(actual) == len(expected) and all(strict_equal(a, e) for a, e in zip(actual, expected))
    return actual == expected


def status_fields(report, time, sequence, decision, age, rejections, *, request=False, broadcast=False):
    # Frozen wire values; not C++ enum ordinals or values copied from emitted evidence.
    flags = 59 if decision == 1 else 48 if decision == 12 else 24 if decision == 20 else 16
    return dict(controller_instance_id=11, transition_epoch=3, clock_mapping_epoch=41,
                source_id=21, session_id=31, correlation_sequence=sequence if request else 0,
                last_accepted_sequence=sequence, validator_time_us=time, status_sequence=report,
                rejection_count=rejections, conservative_age_us=age, protocol_version=65536,
                decision_code=decision, first_latched_code=0, status_flags=flags,
                target_system=0 if broadcast else 1, target_component=0 if broadcast else 1,
                status_class=1 if request else 5, arm_state=2, research_mode_state=2,
                session_state=2, transport_state=1, clock_mapping_state=1)


def expected_case(name):
    """Generate only the declared finite experiment, not a general guard implementation."""
    late = name == "late_admission"
    faulted = name == "status_faults"
    base = 1256000 if late else 1216000
    duration = 40000 if late else 2007000
    ticks = sorted(set(range(0, duration + 1, 1000)) | {1 if late else 80001})
    events, queue, generated = [], [], {}
    counts = dict(requests=0, accesses=0, heartbeats=0, snapshots=0, enqueued=0, dropped=0, returned=0)
    report, last_admitted = 6, base

    def emit(kind, now, **fields):
        events.append(dict(case=name, event=kind, t_us=now, provenance="injected-us-v1", **fields))

    def enqueue(identifier, now):
        if len(queue) == 2:
            emit("drop", now, status_id=identifier, depth=2, reason="backpressure", returned_us=None)
            counts["dropped"] += 1
        else:
            queue.append(identifier)
            emit("enqueue", now, status_id=identifier, depth=len(queue))
            counts["enqueued"] += 1

    def returned(now, index=0):
        identifier = queue.pop(index)
        emit("return", now, status_id=identifier, depth=len(queue), returned_us=now,
             payload=generated[identifier])
        counts["returned"] += 1

    for relative in ticks:
        now = base + relative
        if relative == 0 or (not late and relative in (40000, 60000)):
            sequence = 1 if relative == 0 else 2
            created = 200000 if relative == 0 else 240000
            duplicate = relative == 60000
            report += 1
            fields = status_fields(report, now, sequence, 12 if duplicate else 1,
                                   0 if duplicate else 60000 if late else 20000,
                                   2 if duplicate else 1, request=True)
            generated[report] = encode_status(fields).hex()
            emit("request", now, sequence=sequence, created_us=created, sent_us=created,
                 received_us=now - 4000, status_id=report, payload=generated[report])
            counts["requests"] += 1
            if not duplicate:
                last_admitted = now
            enqueue(report, now)

        report += 1
        if late:
            sequence, eligible = 1, relative == 0
            decision = 1 if eligible else 20 if relative == 1 else 36
            age = 60000 + relative if decision in (1, 20) else 0
            rejections = 1 if eligible else 2
        else:
            sequence = 1 if relative < 40000 else 2
            eligible = relative <= 80000
            decision = 1 if eligible else 20 if relative == 80001 else 36
            age = relative + 20000 - (40000 if sequence == 2 else 0) if decision in (1, 20) else 0
            rejections = 1 if relative < 60000 else 2 if relative <= 80000 else 3
        fields = status_fields(report, now, sequence, decision, age, rejections)
        emit("access", now, eligible=eligible, sequence=sequence if eligible else 0,
             last_admitted_us=last_admitted, no_fresh=now - last_admitted >= 40000,
             payload=encode_status(fields).hex())
        counts["accesses"] += 1

        if relative % 1000000 == 0:
            emit("heartbeat", now)
            counts["heartbeats"] += 1
            fields["target_system"] = fields["target_component"] = 0
            generated[report] = encode_status(fields).hex()
            emit("snapshot", now, status_id=report, payload=generated[report])
            counts["snapshots"] += 1
            enqueue(report, now)

        if not faulted:
            while queue:
                returned(now)
        elif relative == 60000:
            returned(now, -1)
        elif relative in (65000, 2007000):
            returned(now)
        elif relative == 1000000:
            identifier = queue.pop(0)
            emit("drop", now, status_id=identifier, depth=len(queue), reason="injected_loss", returned_us=None)
            counts["dropped"] += 1

    emit("summary", base + duration, **counts, pending=len(queue))
    return events


def expected_records():
    return [row for name in ("baseline", "status_faults", "late_admission") for row in expected_case(name)]


def validate(actual):
    expected = expected_records()
    require(type(actual) is list and len(actual) == len(expected), "missing/excess evidence")
    for index, (row, oracle) in enumerate(zip(actual, expected)):
        require(strict_equal(row, oracle), "evidence differs from independent schedule at record " + str(index))
        if "payload" in row:
            # Decode as well as compare bytes, so schema validity is separately exercised.
            payload = bytes.fromhex(row["payload"])
            require(encode_status(decode_status(payload)) == payload, "status payload round trip")
    return len(expected)


def mutations(records):
    variants = [records[:-1], records[1:], records + [records[0]], list(reversed(records))]
    first = {kind: next(i for i, row in enumerate(records) if row["event"] == kind)
             for kind in ("request", "access", "heartbeat", "snapshot", "enqueue", "drop", "return", "summary")}
    for kind, index in first.items():
        removed = copy.deepcopy(records)
        del removed[index]
        variants.append(removed)
        changed = copy.deepcopy(records)
        changed[index]["t_us"] += 1
        variants.append(changed)
    for kind, field, value in (
        ("access", "eligible", 1), ("access", "no_fresh", True),
        ("access", "last_admitted_us", 0), ("drop", "returned_us", 0),
        ("drop", "reason", "success"), ("enqueue", "depth", 3),
        ("return", "status_id", 0), ("return", "returned_us", None),
        ("request", "provenance", "measured-pi"), ("request", "actuator_output", 0),
        ("summary", "pending", 1),
    ):
        changed = copy.deepcopy(records)
        changed[first[kind]][field] = value
        variants.append(changed)
    for kind in ("request", "return"):
        changed = copy.deepcopy(records)
        status = decode_status(bytes.fromhex(changed[first[kind]]["payload"]))
        status["rejection_count"] += 1  # codec-valid but false historical evidence
        changed[first[kind]]["payload"] = encode_status(status).hex()
        variants.append(changed)
    for variant in variants:
        try:
            validate(variant)
        except ValueError:
            continue
        raise ValueError("checker accepted malformed evidence")
    for malformed in ('{"a":1,"a":2}', '{"time":NaN}', '{"time":Infinity}'):
        try:
            json.loads(malformed, object_pairs_hook=unique_object, parse_constant=invalid_constant)
        except ValueError:
            continue
        raise ValueError("checker accepted malformed JSON")
    return len(variants) + 3


def main():
    require(len(sys.argv) == 2, "usage: check_status_delivery.py <StatusDeliveryTest executable>")
    executable = pathlib.Path(sys.argv[1]).resolve(strict=True)
    result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=30, check=False)
    require(result.returncode == 0 and not result.stderr, "fixture failed: " + result.stderr)
    records = [json.loads(line, object_pairs_hook=unique_object, parse_constant=invalid_constant)
               for line in result.stdout.splitlines()]
    count = validate(records)
    negative_count = mutations(records)
    print(json.dumps(dict(scope="injected offline status delivery only", records_verified=count,
                          malformed_packages_rejected=negative_count,
                          executable_sha256=hashlib.sha256(executable.read_bytes()).hexdigest(),
                          evidence_sha256=hashlib.sha256(result.stdout.encode()).hexdigest()), sort_keys=True))


if __name__ == "__main__":
    main()
