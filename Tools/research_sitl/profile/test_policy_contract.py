"""P2T3: exercise the real payload boundary, not a residual-attitude controller.

All numbers are synthetic contract fixtures. No plant, candidate selector,
physical feasibility test, live transport, or output publisher is implemented.
"""

import math
import pathlib
import struct
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "wire"))
from codec import (  # noqa: E402
    CodecError, decode_request, decode_session, encode_request, encode_session,
)


# Independently transcribed v1 contract fields/offsets, not codec internals.
AXES = ("acceleration_x_frd_m_s2", "acceleration_y_frd_m_s2", "acceleration_z_frd_m_s2")
ENUM_OFFSETS = {"frame": 82, "quantity": 83, "reference_point": 84}
FORBIDDEN_OBJECTIVES = {
    "heading_rad": 0.0, "yaw_rad": 0.0, "attitude_quaternion": (1.0, 0.0, 0.0, 0.0),
    "body_rate_frd_rad_s": (0.0, 0.0, 0.0), "position_ned_m": (0.0, 0.0, 0.0),
    "velocity_ned_m_s": (0.0, 0.0, 0.0), "speed_m_s": 0.0, "thrust": 0.0,
    "torque_frd": (0.0, 0.0, 0.0), "actuator_commands": (0.0,) * 5,
    "specific_force_frd_m_s2": (0.0, 0.0, 0.0), "force_frd_n": (0.0, 0.0, 0.0),
}


def request_fields():
    return {
        "source_id": 1, "session_id": 2, "sequence": 3,
        "producer_monotonic_us": 1_000_000, "producer_expiry_monotonic_us": 1_080_000,
        "controller_instance_id": 4, "transition_epoch": 5, "clock_mapping_epoch": 6,
        AXES[0]: 1.5, AXES[1]: -2.25, AXES[2]: 0.5,
        "protocol_version": 0x00010000, "target_system": 9, "target_component": 10,
        "frame": 1, "quantity": 1, "reference_point": 1,
        "flags": 0, "reserved": b"\0\0\0\0",
    }


def session_fields():
    return {
        "controller_instance_id": 4, "transition_epoch": 5, "clock_mapping_epoch": 6,
        "source_id": 1, "session_id": 2, "operation_sequence": 1,
        "producer_acknowledgement_monotonic_us": 999_000,
        "acknowledged_status_sequence": 7, "protocol_version": 0x00010000,
        "target_system": 9, "target_component": 10, "operation": 1,
        "flags": 0, "reserved": b"\0\0\0\0",
    }


class PolicyContractTest(unittest.TestCase):
    def test_exact_contract_preserves_vector_and_metadata(self):
        for vector in ((1.5, -2.25, 0.5), (0.0, 0.0, 0.0), (-0.0, 0.0, -0.0)):
            fields = request_fields()
            fields.update(zip(AXES, vector))
            original = fields.copy()
            for trim in (False, True):
                with self.subTest(vector=vector, trim=trim):
                    payload = encode_request(fields, trim=trim)
                    decoded = decode_request(payload)
                    self.assertEqual(decoded, fields)
                    self.assertEqual(set(decoded), set(original))
                    # Independent wire offsets preserve FRD component order/sign.
                    self.assertEqual(payload[64:76], struct.pack("<fff", *vector))
                    self.assertEqual(payload[82:85], b"\x01\x01\x01")
                    self.assertEqual(fields, original)
                    for name in AXES:
                        self.assertEqual(math.copysign(1.0, decoded[name]),
                                         math.copysign(1.0, fields[name]))

    def test_extra_objectives_rejected_in_request_and_session(self):
        for encode, factory in ((encode_request, request_fields), (encode_session, session_fields)):
            # Verify the baseline before using it to isolate each extra field.
            encode(factory())
            for name, value in FORBIDDEN_OBJECTIVES.items():
                with self.subTest(encoder=encode.__name__, extra=name):
                    fields = factory()
                    fields[name] = value
                    original = fields.copy()
                    with self.assertRaises(CodecError):
                        encode(fields)
                    self.assertEqual(fields, original)

    def test_no_objective_substitution_for_any_required_axis(self):
        for axis in AXES:
            fields = request_fields()
            del fields[axis]
            with self.subTest(missing=axis), self.assertRaises(CodecError):
                encode_request(fields)
            for name, value in FORBIDDEN_OBJECTIVES.items():
                substituted = dict(fields, **{name: value})
                with self.subTest(missing=axis, replacement=name), self.assertRaises(CodecError):
                    encode_request(substituted)

    def test_only_frd_inertial_cg_wire_enums(self):
        baseline = encode_request(request_fields(), trim=False)
        for name, offset in ENUM_OFFSETS.items():
            for value in range(256):
                if value == 1:
                    continue
                fields = request_fields()
                fields[name] = value
                with self.subTest(field=name, value=value, path="encode"), self.assertRaises(CodecError):
                    encode_request(fields)
                # Decode mutations bypass the encoder's own validation.
                malformed = bytearray(baseline)
                malformed[offset] = value
                with self.subTest(field=name, value=value, path="decode"), self.assertRaises(CodecError):
                    decode_request(bytes(malformed))

    def test_no_unused_or_nonfinite_axis(self):
        baseline = encode_request(request_fields(), trim=False)
        for index, axis in enumerate(AXES):
            for value in (math.nan, math.inf, -math.inf):
                fields = request_fields()
                fields[axis] = value
                with self.subTest(axis=axis, value=value, path="encode"), self.assertRaises(CodecError):
                    encode_request(fields)
                malformed = bytearray(baseline)
                offset = 64 + 4 * index
                malformed[offset:offset + 4] = struct.pack("<f", value)
                with self.subTest(axis=axis, value=value, path="decode"), self.assertRaises(CodecError):
                    decode_request(bytes(malformed))

    def test_no_objective_extension_in_flags_reserved_or_appended_bytes(self):
        for encode, decode, factory, flag_offset, reserved_offset in (
                (encode_request, decode_request, request_fields, 85, 86),
                (encode_session, decode_session, session_fields, 71, 72)):
            baseline = encode(factory(), trim=False)
            for offset in range(flag_offset, reserved_offset + 4):
                for value in (1, 128, 255):
                    malformed = bytearray(baseline)
                    malformed[offset] = value
                    with self.subTest(decoder=decode.__name__, offset=offset, value=value):
                        with self.assertRaises(CodecError):
                            decode(bytes(malformed))
            # Even a zero-valued extra objective is not a v1 extension.
            for extension in (b"\0", struct.pack("<f", 0.0), struct.pack("<f", 1.0)):
                with self.subTest(decoder=decode.__name__, extension=extension):
                    with self.assertRaises(CodecError):
                        decode(baseline + extension)


if __name__ == "__main__":
    unittest.main()
