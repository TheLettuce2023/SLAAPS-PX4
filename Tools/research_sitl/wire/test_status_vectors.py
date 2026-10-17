"""Independent C0b.3 status-vector verifier; fixtures are non-operational."""

import json
import pathlib
import subprocess
import sys

from codec import decode_status, encode_status


def fail(message):
    raise AssertionError(message)


def main():
    if len(sys.argv) != 2:
        fail("usage: test_status_vectors.py <WireStatusTest executable>")
    fixture = pathlib.Path(__file__).with_name("status_vectors.json")
    vectors = json.loads(fixture.read_text(encoding="utf-8"))
    names = (
        "establish_exogenous_barrier", "establish_administrative_barrier",
        "establish_operation", "arm_barrier", "arm_poll", "acknowledgement",
        "accepted_request", "eligible_access", "expiry_access",
    )
    if not isinstance(vectors, list) or len(vectors) != len(names):
        fail("vectors must contain the nine historical records")
    expected = {}
    for item in vectors:
        if not isinstance(item, dict) or set(item) - {"name", "fields", "hex"}:
            fail("invalid vector object")
        name = item.get("name")
        fields = item.get("fields")
        if not isinstance(name, str) or not name or name in expected or not isinstance(fields, dict):
            fail("invalid vector name or fields")
        expected[name] = item

    if tuple(expected) != names:
        fail("vectors must use the fixed historical names and order")

    completed = subprocess.run([sys.argv[1], "--emit-vectors"], check=False,
                               text=True, capture_output=True)
    if completed.returncode != 0 or completed.stderr:
        fail("vector emitter failed")
    actual = {}
    for line in completed.stdout.splitlines():
        parts = line.split()
        if len(parts) != 2 or parts[0] in actual:
            fail("emitter output is not exact name/hex pairs")
        name, payload_hex = parts
        if len(payload_hex) != 212:
            fail("status payload is not 106 bytes")
        try:
            payload = bytes.fromhex(payload_hex)
        except ValueError as error:
            raise AssertionError("emitter hex is invalid") from error
        actual[name] = payload
    if set(actual) != set(expected):
        fail("emitted vector names differ from independent fixture")
    for name, item in expected.items():
        payload = actual[name]
        decoded = decode_status(payload)
        if decoded != item["fields"]:
            fail(name + " fields differ")
        if encode_status(item["fields"], trim=False) != payload:
            fail(name + " codec re-encode differs")
        if "hex" in item and payload.hex() != item["hex"]:
            fail(name + " golden hex differs")


if __name__ == "__main__":
    main()
