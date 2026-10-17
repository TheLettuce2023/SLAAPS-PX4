"""Independent C1E-W1 diagnostic payload conformance checks.

The byte offsets and field lists below are transcribed from the reviewed W1
contract, not imported from codec implementation constants. These tests do not
claim MAVLink generation, signing, or native ingress conformance.
"""

import copy
import importlib.util
import math
from pathlib import Path
import struct
import unittest
import xml.etree.ElementTree as ET

from codec import (
    CodecError,
    decode_request,
    decode_status,
    encode_request,
    encode_status,
)


VERSION = 0x00010000
U64_MAX = (1 << 64) - 1
# Independently transcribed wire bytes for the fixture values below.
REQUEST_BYTES = bytes.fromhex(
    "0b000000000000000c000000000000000d000000000000000e00000000000000"
    "0f00000000000000100000000000000011000000000000006400000000000000"
    "b40000000000000000000000000000000000c03f000010c00000003f00000100"
    "01012abf0101010000000000"
)
STATUS_BYTES = bytes.fromhex(
    "0b000000000000000c000000000000000d000000000000000e00000000000000"
    "0f00000000000000100000000000000015000000000000001100000000000000"
    "03000000000000000200000000000000e803000000000000ea03000000000000"
    "e903000000000000000000000000000000000100010001010101010300000000"
    "00002abf00000000"
)
REQUEST_Q = (
    "native_run_id", "owner_lifetime_id", "controller_instance_id",
    "source_id", "transport_session_id", "request_sequence",
    "expected_transition_epoch", "producer_monotonic_us",
    "producer_expiry_monotonic_us", "clock_mapping_epoch",
)
REQUEST_F = (
    "acceleration_x_frd_m_s2", "acceleration_y_frd_m_s2",
    "acceleration_z_frd_m_s2",
)
REQUEST_B = (
    "domain", "profile", "target_system", "target_component", "frame",
    "quantity", "reference_point", "flags",
)
STATUS_Q = (
    "native_run_id", "owner_lifetime_id", "controller_instance_id",
    "source_id", "transport_session_id", "request_sequence",
    "status_sequence", "transition_epoch", "source_sequence",
    "state_generation", "arrival_native_us", "evaluation_native_us",
    "source_checked_native_us", "clock_mapping_epoch",
)
STATUS_B = (
    "domain", "profile", "execution_origin", "outcome", "arm_state",
    "raw_nav_state", "research_mode_state", "selector_state",
    "estimator_valid", "clock_map_valid", "eligible_for_control", "flags",
    "target_system", "target_component",
)
REQUEST_OFFSETS = {name: 8 * index for index, name in enumerate(REQUEST_Q)}
REQUEST_OFFSETS.update({name: 80 + 4 * index for index, name in enumerate(REQUEST_F)})
REQUEST_OFFSETS.update({name: 96 + index for index, name in enumerate(REQUEST_B)})
REQUEST_OFFSETS.update(protocol_version=92, reserved=104)
STATUS_OFFSETS = {name: 8 * index for index, name in enumerate(STATUS_Q)}
STATUS_OFFSETS.update({name: 118 + index for index, name in enumerate(STATUS_B)})
STATUS_OFFSETS.update(protocol_version=112, reason=116, reserved=132)


def request_fields():
    fields = dict(zip(REQUEST_Q, (11, 12, 13, 14, 15, 16, 17, 100, 180, 0)))
    fields.update(zip(REQUEST_F, (1.5, -2.25, 0.5)))
    fields.update(protocol_version=VERSION, domain=1, profile=1,
                  target_system=42, target_component=191, frame=1, quantity=1,
                  reference_point=1, flags=0, reserved=b"\0" * 4)
    return fields


def status_fields():
    fields = dict(zip(STATUS_Q, (
        11, 12, 13, 14, 15, 16, 21, 17, 3, 2, 1000, 1002, 1001, 0,
    )))
    fields.update(protocol_version=VERSION, reason=1, domain=1, profile=1,
                  execution_origin=1, outcome=1, arm_state=1, raw_nav_state=3,
                  research_mode_state=0, selector_state=0, estimator_valid=0,
                  clock_map_valid=0, eligible_for_control=0, flags=0,
                  target_system=42, target_component=191, reserved=b"\0" * 4)
    return fields


def request_oracle(fields):
    return (b"".join(fields[name].to_bytes(8, "little") for name in REQUEST_Q)
            + b"".join(struct.pack("<f", fields[name]) for name in REQUEST_F)
            + fields["protocol_version"].to_bytes(4, "little")
            + bytes(fields[name] for name in REQUEST_B) + fields["reserved"])


def status_oracle(fields):
    return (b"".join(fields[name].to_bytes(8, "little") for name in STATUS_Q)
            + fields["protocol_version"].to_bytes(4, "little")
            + fields["reason"].to_bytes(2, "little")
            + bytes(fields[name] for name in STATUS_B) + fields["reserved"])


class DiagnosticCodecTest(unittest.TestCase):
    def assert_bad(self, function, *args, **kwargs):
        with self.assertRaises(CodecError):
            function(*args, **kwargs)

    def test_independent_oracle_offsets_and_trimmed_lengths(self):
        for fields, oracle, encoder, decoder, minimum, full in (
            (request_fields(), request_oracle, encode_request, decode_request, 103, 108),
            (status_fields(), status_oracle, encode_status, decode_status, 132, 136),
        ):
            raw = oracle(fields)
            self.assertEqual(len(raw), full)
            self.assertEqual(encoder(fields, trim=False), raw)
            self.assertEqual(encoder(fields, trim=True), raw[:minimum])
            self.assertEqual(encoder(fields), raw)
            for length in range(minimum, full + 1):
                with self.subTest(full=full, length=length):
                    self.assertEqual(decoder(raw[:length]), fields)
            self.assertEqual(decoder(raw), fields)
            self.assertEqual(len(set(fields)), len(fields))

        request = request_oracle(request_fields())
        status = status_oracle(status_fields())
        self.assertEqual(request, REQUEST_BYTES)
        self.assertEqual(status, STATUS_BYTES)
        self.assertEqual(request[REQUEST_OFFSETS["target_component"]], 191)
        self.assertEqual(request[REQUEST_OFFSETS["clock_mapping_epoch"]:80], b"\0" * 8)
        self.assertEqual(status[STATUS_OFFSETS["reason"]:118], b"\x01\x00")
        self.assertEqual(status[STATUS_OFFSETS["source_sequence"]:72], (3).to_bytes(8, "little"))

    def test_exact_input_types_keys_and_output_preservation(self):
        for fields, encoder, decoder, raw, minimum in (
            (request_fields(), encode_request, decode_request, request_oracle(request_fields()), 103),
            (status_fields(), encode_status, decode_status, status_oracle(status_fields()), 132),
        ):
            original = copy.deepcopy(fields)
            self.assertEqual(encoder(fields, trim=False), raw)
            self.assertEqual(fields, original)
            for name in fields:
                missing = dict(fields)
                del missing[name]
                self.assert_bad(encoder, missing)
            extra = dict(fields, unexpected=1)
            self.assert_bad(encoder, extra)
            for bad in (None, [], tuple(fields.items()), bytes(raw)):
                self.assert_bad(encoder, bad)
            for bad in (0, 1, None, "yes"):
                self.assert_bad(encoder, fields, trim=bad)
            for bad in (bytearray(raw), memoryview(raw), str(raw), None):
                self.assert_bad(decoder, bad)
            self.assert_bad(decoder, raw[:minimum - 1])
            self.assert_bad(decoder, raw + b"\0")

    def test_request_each_malformed_field_at_encode_and_decode(self):
        fields = request_fields()
        raw = request_oracle(fields)

        def bad_field(name, value, replacement):
            candidate = dict(fields, **{name: value})
            self.assert_bad(encode_request, candidate)
            offset = REQUEST_OFFSETS[name]
            self.assert_bad(decode_request, raw[:offset] + replacement + raw[offset + len(replacement):])

        for name in REQUEST_Q[:-1]:
            with self.subTest(name=name):
                bad_field(name, 0, b"\0" * 8)
                self.assert_bad(encode_request, dict(fields, **{name: True}))
                self.assert_bad(encode_request, dict(fields, **{name: U64_MAX + 1}))
        bad_field("clock_mapping_epoch", 1, (1).to_bytes(8, "little"))
        bad_field("producer_expiry_monotonic_us", 100, (100).to_bytes(8, "little"))
        for name in REQUEST_Q:
            for value in (-1, U64_MAX + 1, 1.25, "1", True):
                with self.subTest(name=name, value=value):
                    self.assert_bad(encode_request, dict(fields, **{name: value}))
        for name in REQUEST_F:
            with self.subTest(name=name):
                bad_field(name, math.nan, b"\x00\x00\xc0\x7f")
                self.assert_bad(encode_request, dict(fields, **{name: math.inf}))
                self.assert_bad(encode_request, dict(fields, **{name: 1e100}))
                self.assert_bad(encode_request, dict(fields, **{name: True}))
                offset = REQUEST_OFFSETS[name]
                for nonfinite in (b"\0\0\x80\x7f", b"\0\0\x80\xff"):
                    self.assert_bad(decode_request, raw[:offset] + nonfinite + raw[offset + 4:])
        bad_field("protocol_version", 0, b"\0" * 4)
        for value in (-1, 1 << 32, True, 1.5, "65536"):
            self.assert_bad(encode_request, dict(fields, protocol_version=value))
        for name in REQUEST_B:
            with self.subTest(name=name):
                bad_value = 1 if name == "flags" else 0
                bad_field(name, bad_value, bytes((bad_value,)))
                self.assert_bad(encode_request, dict(fields, **{name: True}))
                for value in (-1, 256, 1.5, "1"):
                    self.assert_bad(encode_request, dict(fields, **{name: value}))
        for name in ("domain", "profile", "frame", "quantity", "reference_point"):
            bad_field(name, 2, b"\x02")
        bad_field("reserved", b"\0\0\0\1", b"\0\0\0\1")

    def test_status_each_malformed_field_at_encode_and_decode(self):
        fields = status_fields()
        raw = status_oracle(fields)

        def bad_field(name, value, replacement):
            self.assert_bad(encode_status, dict(fields, **{name: value}))
            offset = STATUS_OFFSETS[name]
            self.assert_bad(decode_status, raw[:offset] + replacement + raw[offset + len(replacement):])

        for name in STATUS_Q[:-1]:
            with self.subTest(name=name):
                bad_field(name, 0, b"\0" * 8)
                self.assert_bad(encode_status, dict(fields, **{name: True}))
                self.assert_bad(encode_status, dict(fields, **{name: U64_MAX + 1}))
        bad_field("clock_mapping_epoch", 1, (1).to_bytes(8, "little"))
        bad_field("arrival_native_us", 1003, (1003).to_bytes(8, "little"))
        bad_field("source_checked_native_us", 1003, (1003).to_bytes(8, "little"))
        bad_field("state_generation", 4, (4).to_bytes(8, "little"))
        for name in STATUS_Q:
            for value in (-1, U64_MAX + 1, 1.25, "1", True):
                with self.subTest(name=name, value=value):
                    self.assert_bad(encode_status, dict(fields, **{name: value}))
        bad_field("protocol_version", 0, b"\0" * 4)
        for value in (-1, 1 << 32, True, 1.5, "65536"):
            self.assert_bad(encode_status, dict(fields, protocol_version=value))
        bad_field("reason", 0, b"\0" * 2)
        for value in (-1, 1 << 16, True, 1.5, "1"):
            self.assert_bad(encode_status, dict(fields, reason=value))
        for name in STATUS_B:
            with self.subTest(name=name):
                if name in ("research_mode_state", "selector_state", "estimator_valid",
                            "clock_map_valid", "eligible_for_control", "flags"):
                    bad_value = 1
                elif name == "raw_nav_state":
                    bad_value = 9
                else:
                    bad_value = 0
                bad_field(name, bad_value, bytes((bad_value,)))
                self.assert_bad(encode_status, dict(fields, **{name: True}))
                for value in (-1, 256, 1.5, "1"):
                    self.assert_bad(encode_status, dict(fields, **{name: value}))
        for name in ("domain", "profile", "execution_origin", "arm_state"):
            bad_field(name, 3, b"\x03")
        bad_field("outcome", 3, b"\x03")
        bad_field("reserved", b"\0\0\0\1", b"\0\0\0\1")

    def test_valid_status_reason_outcome_origin_and_raw_nav_boundaries(self):
        fields = status_fields()
        for reason in range(1, 9):
            for origin in (1, 2):
                with self.subTest(reason=reason, origin=origin):
                    candidate = dict(fields, reason=reason, outcome=1 if reason <= 5 else 2,
                                     execution_origin=origin)
                    self.assertEqual(decode_status(encode_status(candidate, trim=False)), candidate)
                    bad_outcome = dict(candidate, outcome=2 if reason <= 5 else 1)
                    self.assert_bad(encode_status, bad_outcome)
        for nav in (0, 1, 8, 10, 15, 17, 30):
            candidate = dict(fields, raw_nav_state=nav)
            self.assertEqual(decode_status(encode_status(candidate, trim=False)), candidate)
        for nav in (9, 11, 16, 31, 255):
            self.assert_bad(encode_status, dict(fields, raw_nav_state=nav))
        self.assertEqual(decode_status(encode_status(dict(fields, arm_state=2), trim=False))["arm_state"], 2)

    def test_coherent_width_time_and_binary32_boundaries(self):
        request = request_fields()
        request.update(native_run_id=U64_MAX, owner_lifetime_id=U64_MAX,
                       request_sequence=U64_MAX,
                       producer_monotonic_us=U64_MAX - 1,
                       producer_expiry_monotonic_us=U64_MAX,
                       target_system=255, target_component=255,
                       acceleration_x_frd_m_s2=0.1,
                       acceleration_y_frd_m_s2=-1e-50)
        decoded_request = decode_request(encode_request(request, trim=False))
        self.assertEqual(decoded_request["native_run_id"], U64_MAX)
        self.assertEqual(decoded_request["producer_expiry_monotonic_us"], U64_MAX)
        self.assertEqual(decoded_request["acceleration_x_frd_m_s2"],
                         struct.unpack("<f", struct.pack("<f", 0.1))[0])
        self.assertEqual(decoded_request["acceleration_y_frd_m_s2"], 0.0)
        self.assertLess(math.copysign(1.0, decoded_request["acceleration_y_frd_m_s2"]), 0)
        self.assertEqual(encode_request(request, trim=False)[84:88],
                         struct.pack("<f", -1e-50))

        status = status_fields()
        status.update(native_run_id=U64_MAX, status_sequence=U64_MAX,
                      source_sequence=U64_MAX, state_generation=U64_MAX,
                      arrival_native_us=U64_MAX,
                      evaluation_native_us=U64_MAX,
                      source_checked_native_us=U64_MAX,
                      target_system=255, target_component=255)
        self.assertEqual(decode_status(encode_status(status, trim=False)), status)
        equal_boundary = status_fields()
        equal_boundary.update(source_sequence=2, state_generation=2,
                              arrival_native_us=1002, source_checked_native_us=1002)
        self.assertEqual(decode_status(encode_status(equal_boundary, trim=False)), equal_boundary)

    def test_xml_field_type_and_wire_order_agreement(self):
        xml_path = Path(__file__).with_name("diagnostic.xml")
        tree = ET.parse(xml_path)
        messages = {int(node.attrib["id"]): node for node in tree.findall(".//message")}
        for message_id, message_name, expected in (
            (200010, "RESEARCH_DIAGNOSTIC_REQUEST",
             [(name, "uint64_t") for name in REQUEST_Q]
             + [(name, "float") for name in REQUEST_F]
             + [("protocol_version", "uint32_t")]
             + [(name, "uint8_t") for name in REQUEST_B]
             + [("reserved", "uint8_t[4]")]),
            (200011, "RESEARCH_DIAGNOSTIC_STATUS",
             [(name, "uint64_t") for name in STATUS_Q]
             + [("protocol_version", "uint32_t"), ("reason", "uint16_t")]
             + [(name, "uint8_t") for name in STATUS_B]
             + [("reserved", "uint8_t[4]")]),
        ):
            with self.subTest(message_id=message_id):
                self.assertIn(message_id, messages)
                message = messages[message_id]
                self.assertEqual(message.attrib["name"], message_name)
                actual = [(field.attrib["name"], field.attrib["type"])
                          for field in message.findall("field")]
                self.assertEqual(actual, expected)

    def test_frozen_flight_payloads_cannot_cross_decode(self):
        flight_path = Path(__file__).resolve().parents[1] / "wire" / "codec.py"
        spec = importlib.util.spec_from_file_location("frozen_flight_codec", flight_path)
        flight = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(flight)
        self.assert_bad(decode_request, b"\0" * 90)
        self.assert_bad(decode_status, b"\0" * 106)
        with self.assertRaises(flight.CodecError):
            flight.decode_request(request_oracle(request_fields()))
        with self.assertRaises(flight.CodecError):
            flight.decode_status(status_oracle(status_fields()))


if __name__ == "__main__":
    unittest.main()
