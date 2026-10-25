"""C0a payload-codec conformance tests using frozen table-derived vectors."""

import copy
import math
import unittest

from codec import (
    CodecError,
    decode_request,
    decode_session,
    decode_status,
    encode_request,
    encode_session,
    encode_status,
)


# Independently transcribed from ACCELERATION_WIRE_CONTRACT.md offsets/tables.
REQUEST_HEX = (
    "0100000000000000020000000000000003000000000000000400000000000000"
    "0500000000000000060000000000000007000000000000000800000000000000"
    "0000c03f000010c00000003f00000100090a0101010000000000"
)
SESSION_HEX = (
    "0b000000000000000c000000000000000d000000000000000e00000000000000"
    "0f00000000000000100000000000000011000000000000001200000000000000"
    "000001001314030000000000"
)
STATUS_HEX = (
    "1500000000000000160000000000000017000000000000001800000000000000"
    "19000000000000001a000000000000001b000000000000001c00000000000000"
    "1d000000000000001e000000000000001f000000000000000000010001000000"
    "3b002122010202020101"
)
REQUEST_BYTES = bytes.fromhex(REQUEST_HEX)
SESSION_BYTES = bytes.fromhex(SESSION_HEX)
STATUS_BYTES = bytes.fromhex(STATUS_HEX)
VERSION = 0x00010000
UINT64_MAX = (1 << 64) - 1


def request_fields():
    return {
        "source_id": 1, "session_id": 2, "sequence": 3,
        "producer_monotonic_us": 4, "producer_expiry_monotonic_us": 5,
        "controller_instance_id": 6, "transition_epoch": 7,
        "clock_mapping_epoch": 8, "acceleration_x_frd_m_s2": 1.5,
        "acceleration_y_frd_m_s2": -2.25, "acceleration_z_frd_m_s2": 0.5,
        "protocol_version": VERSION, "target_system": 9, "target_component": 10,
        "frame": 1, "quantity": 1, "reference_point": 1, "flags": 0,
        "reserved": b"\0\0\0\0",
    }


def session_fields():
    return {
        "controller_instance_id": 11, "transition_epoch": 12,
        "clock_mapping_epoch": 13, "source_id": 14, "session_id": 15,
        "operation_sequence": 16, "producer_acknowledgement_monotonic_us": 17,
        "acknowledged_status_sequence": 18, "protocol_version": VERSION,
        "target_system": 19, "target_component": 20, "operation": 3,
        "flags": 0, "reserved": b"\0\0\0\0",
    }


def status_fields():
    return {
        "controller_instance_id": 21, "transition_epoch": 22,
        "clock_mapping_epoch": 23, "source_id": 24, "session_id": 25,
        "correlation_sequence": 26, "last_accepted_sequence": 27,
        "validator_time_us": 28, "status_sequence": 29, "rejection_count": 30,
        "conservative_age_us": 31, "protocol_version": VERSION,
        "decision_code": 1, "first_latched_code": 0, "status_flags": 59,
        "target_system": 33, "target_component": 34, "status_class": 1,
        "arm_state": 2, "research_mode_state": 2, "session_state": 2,
        "transport_state": 1, "clock_mapping_state": 1,
    }


class CodecConformanceTest(unittest.TestCase):
    def assert_codec_error(self, function, *args, **kwargs):
        with self.assertRaises(CodecError):
            function(*args, **kwargs)

    def test_table_derived_golden_vectors_and_all_trimmed_lengths(self):
        self.assertEqual(len(REQUEST_BYTES), 90)
        self.assertEqual(len(SESSION_BYTES), 76)
        self.assertEqual(len(STATUS_BYTES), 106)
        self.assertEqual(encode_request(request_fields(), trim=False), REQUEST_BYTES)
        self.assertEqual(encode_session(session_fields(), trim=False), SESSION_BYTES)
        self.assertEqual(encode_status(status_fields(), trim=False), STATUS_BYTES)
        self.assertEqual(encode_request(request_fields()), REQUEST_BYTES[:85])
        self.assertEqual(encode_session(session_fields()), SESSION_BYTES[:71])
        self.assertEqual(encode_status(status_fields()), STATUS_BYTES)
        for length in range(85, 91):
            self.assertEqual(decode_request(REQUEST_BYTES[:length]), decode_request(REQUEST_BYTES))
        for length in range(71, 77):
            self.assertEqual(decode_session(SESSION_BYTES[:length]), decode_session(SESSION_BYTES))

    def test_table_derived_decoding(self):
        request = decode_request(REQUEST_BYTES)
        session = decode_session(SESSION_BYTES)
        status = decode_status(STATUS_BYTES)
        for key, value in request_fields().items():
            self.assertEqual(request[key], value)
        for key, value in session_fields().items():
            self.assertEqual(session[key], value)
        self.assertEqual(status, status_fields())

    def test_exact_fields_no_defaults_no_mutation_and_argument_types(self):
        for encode, fields in ((encode_request, request_fields()), (encode_session, session_fields()),
                               (encode_status, status_fields())):
            original = copy.deepcopy(fields)
            encode(fields)
            self.assertEqual(fields, original)
            missing = copy.deepcopy(fields)
            missing.pop(next(iter(missing)))
            self.assert_codec_error(encode, missing)
            extra = copy.deepcopy(fields)
            extra["velocity_ned"] = 0
            self.assert_codec_error(encode, extra)
            self.assert_codec_error(encode, fields, trim=1)
        for decode, payload in ((decode_request, REQUEST_BYTES), (decode_session, SESSION_BYTES),
                                (decode_status, STATUS_BYTES)):
            self.assert_codec_error(decode, bytearray(payload))
            self.assert_codec_error(decode, memoryview(payload))
        for encode, fields in ((encode_request, request_fields()), (encode_session, session_fields())):
            invalid = copy.deepcopy(fields)
            invalid["reserved"] = True
            self.assert_codec_error(encode, invalid)
            invalid["reserved"] = b"\0\0\0"
            self.assert_codec_error(encode, invalid)

    def test_lengths_reserved_version_enums_and_flags_reject(self):
        for decode, payload, minimum in ((decode_request, REQUEST_BYTES, 85), (decode_session, SESSION_BYTES, 71),
                                         (decode_status, STATUS_BYTES, 106)):
            self.assert_codec_error(decode, payload[:minimum - 1])
            self.assert_codec_error(decode, payload + b"\0")
        for encode, fields, names in (
                (encode_request, request_fields(), ("protocol_version", "target_system", "target_component",
                                                    "frame", "quantity", "reference_point", "flags")),
                (encode_session, session_fields(), ("protocol_version", "target_system", "target_component",
                                                    "operation", "flags"))):
            for name in names:
                invalid = copy.deepcopy(fields)
                invalid[name] = 0 if name != "flags" else 1
                self.assert_codec_error(encode, invalid)
                if name in ("frame", "quantity", "reference_point", "operation"):
                    invalid[name] = 2 if name != "operation" else 4
                    self.assert_codec_error(encode, invalid)
                elif name not in ("flags", "protocol_version"):
                    invalid[name] = 256
                    self.assert_codec_error(encode, invalid)
            invalid = copy.deepcopy(fields)
            for index in range(4):
                malformed = copy.deepcopy(invalid)
                malformed["reserved"] = b"\0" * index + b"\1" + b"\0" * (3 - index)
                self.assert_codec_error(encode, malformed)
        for name in ("status_class", "arm_state", "research_mode_state", "session_state",
                     "transport_state", "clock_mapping_state"):
            invalid = status_fields()
            invalid[name] = 0
            self.assert_codec_error(encode_status, invalid)
            invalid[name] = 255
            self.assert_codec_error(encode_status, invalid)
        invalid = status_fields()
        invalid["status_flags"] = 1 << 6
        self.assert_codec_error(encode_status, invalid)
        invalid = status_fields()
        invalid["protocol_version"] = VERSION + 1
        self.assert_codec_error(encode_status, invalid)
        # Byte-level malformed vectors exercise decoders rather than encoders.
        for decode, payload, offset, value in (
                (decode_request, REQUEST_BYTES, 76, 1),
                (decode_request, REQUEST_BYTES, 82, 2),
                (decode_request, REQUEST_BYTES, 85, 1),
                (decode_request, REQUEST_BYTES, 86, 1),
                (decode_session, SESSION_BYTES, 64, 1),
                (decode_session, SESSION_BYTES, 70, 4),
                (decode_session, SESSION_BYTES, 72, 1),
                (decode_status, STATUS_BYTES, 88, 1),
                (decode_status, STATUS_BYTES, 96, 0x40),
                (decode_status, STATUS_BYTES, 100, 0)):
            malformed = bytearray(payload)
            malformed[offset] = value
            self.assert_codec_error(decode, bytes(malformed))

    def test_unsigned_negative_overflow_and_boolean_by_wire_width(self):
        groups = (
            (encode_request, request_fields(), ("protocol_version",), ("target_system", "target_component",
             "frame", "quantity", "reference_point", "flags")),
            (encode_session, session_fields(), ("protocol_version",), ("target_system", "target_component",
             "operation", "flags")),
            (encode_status, status_fields(), ("protocol_version",), ("target_system", "target_component",
             "status_class", "arm_state", "research_mode_state", "session_state", "transport_state",
             "clock_mapping_state")),
        )
        for encode, fields, uint32_names, uint8_names in groups:
            for name in uint32_names:
                for value in (-1, 1 << 32, True):
                    invalid = copy.deepcopy(fields)
                    invalid[name] = value
                    self.assert_codec_error(encode, invalid)
            for name in uint8_names:
                for value in (-1, 256, True):
                    invalid = copy.deepcopy(fields)
                    invalid[name] = value
                    self.assert_codec_error(encode, invalid)
        for name in ("decision_code", "first_latched_code", "status_flags"):
            for value in (-1, 1 << 16, True):
                invalid = status_fields()
                invalid[name] = value
                self.assert_codec_error(encode_status, invalid)

    def test_width_boundaries_and_boolean_rejection(self):
        groups = (
            (encode_request, request_fields(), ("source_id", "session_id", "sequence", "producer_monotonic_us",
             "producer_expiry_monotonic_us", "controller_instance_id", "transition_epoch", "clock_mapping_epoch")),
            (encode_session, session_fields(), ("controller_instance_id", "transition_epoch", "clock_mapping_epoch",
             "source_id", "session_id", "operation_sequence", "producer_acknowledgement_monotonic_us",
             "acknowledged_status_sequence")),
            (encode_status, status_fields(), ("controller_instance_id", "transition_epoch", "clock_mapping_epoch",
             "source_id", "session_id", "correlation_sequence", "last_accepted_sequence", "validator_time_us",
             "status_sequence", "rejection_count", "conservative_age_us")),
        )
        for encode, fields, uint64_names in groups:
            for name in uint64_names:
                valid = copy.deepcopy(fields)
                valid[name] = UINT64_MAX
                if encode is encode_request and name == "producer_monotonic_us":
                    valid["producer_expiry_monotonic_us"] = UINT64_MAX
                    valid[name] = UINT64_MAX - 1
                encode(valid)
                invalid = copy.deepcopy(fields)
                invalid[name] = UINT64_MAX + 1
                self.assert_codec_error(encode, invalid)
                invalid[name] = True
                self.assert_codec_error(encode, invalid)
        for encode, fields in ((encode_request, request_fields()), (encode_session, session_fields()),
                               (encode_status, status_fields())):
            for name, value in fields.items():
                if type(value) is int:
                    invalid = copy.deepcopy(fields)
                    invalid[name] = True
                    self.assert_codec_error(encode, invalid)
        for expiry in (4, 3):
            invalid = request_fields()
            invalid["producer_expiry_monotonic_us"] = expiry
            self.assert_codec_error(encode_request, invalid)

    def test_binary32_finite_overflow_subnormal_and_signed_zero(self):
        for name in ("acceleration_x_frd_m_s2", "acceleration_y_frd_m_s2", "acceleration_z_frd_m_s2"):
            invalid = request_fields()
            invalid[name] = math.inf
            self.assert_codec_error(encode_request, invalid)
            invalid[name] = math.nan
            self.assert_codec_error(encode_request, invalid)
            invalid[name] = 1e100
            self.assert_codec_error(encode_request, invalid)
            subnormal = request_fields()
            subnormal[name] = 1e-50
            self.assertEqual(decode_request(encode_request(subnormal, trim=False))[name], 0.0)
            signed_zero = request_fields()
            signed_zero[name] = -0.0
            self.assertLess(math.copysign(1.0, decode_request(encode_request(signed_zero, trim=False))[name]), 0)
            invalid[name] = True
            self.assert_codec_error(encode_request, invalid)
        smallest = request_fields()
        smallest["acceleration_x_frd_m_s2"] = math.ldexp(1.0, -149)
        self.assertEqual(encode_request(smallest, trim=False)[64:68], bytes.fromhex("01000000"))
        rounded = request_fields()
        rounded["acceleration_x_frd_m_s2"] = 0.1
        self.assertEqual(encode_request(rounded, trim=False)[64:68], bytes.fromhex("cdcccc3d"))
        enormous = request_fields()
        enormous["acceleration_x_frd_m_s2"] = 1 << 100000
        self.assert_codec_error(encode_request, enormous)
        malformed = bytearray(REQUEST_BYTES)
        malformed[64:68] = bytes.fromhex("0000c07f")
        self.assert_codec_error(decode_request, bytes(malformed))

    def test_status_claim_and_latch_coherence(self):
        cases = []
        invalid = status_fields()
        invalid["decision_code"] = 2
        cases.append(invalid)
        invalid = status_fields()
        invalid["status_class"] = 3
        cases.append(invalid)
        invalid = status_fields()
        invalid["status_flags"] &= ~(1 << 3)
        cases.append(invalid)
        invalid = status_fields()
        invalid["status_flags"] &= ~(1 << 4)
        cases.append(invalid)
        invalid = status_fields()
        invalid["first_latched_code"] = 18
        cases.append(invalid)
        invalid = status_fields()
        invalid["session_state"] = 5
        cases.append(invalid)
        invalid = status_fields()
        invalid["source_id"] = 0
        cases.append(invalid)
        invalid = status_fields()
        invalid["target_system"] = 0
        cases.append(invalid)
        invalid = status_fields()
        invalid["clock_mapping_epoch"] = 0
        cases.append(invalid)
        invalid = status_fields()
        invalid["decision_code"] = 39
        cases.append(invalid)
        invalid = status_fields()
        invalid["decision_code"] = 0
        cases.append(invalid)
        for fields in cases:
            self.assert_codec_error(encode_status, fields)
        latched = status_fields()
        latched["status_flags"] = (1 << 2) | (1 << 1) | (1 << 5)
        latched["session_state"] = 5
        latched["decision_code"] = 18
        latched["first_latched_code"] = 18
        latched["conservative_age_us"] = 0
        latched["last_accepted_sequence"] = 0
        encode_status(latched)
        zero_age = status_fields()
        zero_age["conservative_age_us"] = 0
        encode_status(zero_age)

    def test_v2_does_not_change_v1_request_codec(self):
        fields = request_fields()
        fields['protocol_version'] = 0x00020000
        fields['flags'] = 1
        self.assert_codec_error(encode_request, fields)
        payload = bytearray(REQUEST_BYTES)
        payload[76:80] = (0x00020000).to_bytes(4, 'little')
        payload[85] = 1
        self.assert_codec_error(decode_request, bytes(payload))
        self.assert_codec_error(decode_request, bytes(payload) + bytes.fromhex('0000803f') + bytes(12))
        rejected = status_fields()
        rejected.update(decision_code=38, status_flags=32, conservative_age_us=0,
                        last_accepted_sequence=0)
        self.assertEqual(decode_status(encode_status(rejected))['decision_code'], 38)


if __name__ == "__main__":
    unittest.main()
