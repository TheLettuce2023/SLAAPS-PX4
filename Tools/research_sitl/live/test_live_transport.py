"""Offline reverse-status authentication tests; no sockets or live owner.

Pass the generated research.py path from the source-pinned C1H runner. Public
fixture signing bytes exist only for these offline tests and are never logged.
"""

import argparse
import copy
import hashlib
import importlib.util
import os
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "wire"))
from codec import decode_status
from live_transport import Guidance, TransportError


DIALECT = None
KEY = bytes(range(1, 33))
SEED = 10_000_000
CONTROLLER = 13
FUTURE = SEED + 100_000_000


def status_fields():
    return dict(
        controller_instance_id=CONTROLLER, transition_epoch=5, clock_mapping_epoch=6,
        source_id=11, session_id=12, correlation_sequence=1, last_accepted_sequence=1,
        validator_time_us=1115, status_sequence=7, rejection_count=1, conservative_age_us=7,
        protocol_version=65536, decision_code=1, first_latched_code=0, status_flags=59,
        target_system=42, target_component=191, status_class=1, arm_state=2,
        research_mode_state=2, session_state=2, transport_state=1, clock_mapping_state=1,
    )


def offline_receiver():
    # Exercise the exact production verification method without its socket-owning
    # constructor. A patched socket factory below makes accidental I/O a failure.
    receiver = Guidance.__new__(Guidance)
    receiver.module = DIALECT
    receiver.key = KEY
    receiver.replay = {}
    receiver.status_timestamp = SEED
    return receiver


def signed_status(timestamp, *, fields=None, system=1, component=1, link=8, key=KEY, signed=True):
    mav = DIALECT.MAVLink(None, srcSystem=system, srcComponent=component)
    mav.signing.secret_key = key
    mav.signing.link_id = link
    mav.signing.timestamp = timestamp
    mav.signing.sign_outgoing = signed
    values = status_fields()
    values.update(fields or {})
    return bytes(DIALECT.MAVLink_research_acceleration_status_message(**values).pack(mav))


def x25(data):
    # Independent test oracle only, never used by the production verifier.
    value = 0xffff
    for byte in data:
        temporary = byte ^ (value & 0xff)
        temporary = (temporary ^ (temporary << 4)) & 0xff
        value = ((value >> 8) ^ (temporary << 8) ^ (temporary << 3) ^ (temporary >> 4)) & 0xffff
    return value


def with_header(frame, offset, replacement, crc_extra=131):
    frame = bytearray(frame)
    frame[offset:offset + len(replacement)] = replacement
    frame[-15:-13] = x25(frame[1:-15] + bytes((crc_extra,))).to_bytes(2, "little")
    frame[-6:] = hashlib.sha256(KEY + frame[:-6]).digest()[:6]
    return bytes(frame)


class ReverseStatusTests(unittest.TestCase):
    def setUp(self):
        self.assertIsNotNone(DIALECT, "generated module path is mandatory")
        self.no_socket = patch("live_transport.socket.socket", side_effect=AssertionError("offline test opened socket"))
        self.no_socket.start()
        self.addCleanup(self.no_socket.stop)

    def reject_then_valid(self, frame):
        for populated in (False, True):
            with self.subTest(replay_populated=populated):
                receiver = offline_receiver()
                if populated:
                    receiver.verify_status(signed_status(SEED + 1), CONTROLLER)
                before = copy.deepcopy(receiver.replay)
                timestamp_before = receiver.status_timestamp
                with self.assertRaises(TransportError):
                    receiver.verify_status(frame, CONTROLLER)
                self.assertEqual(receiver.replay, before)
                self.assertEqual(receiver.status_timestamp, timestamp_before)
                # Every adversarial frame used FUTURE; this lower timestamp must
                # remain usable for the configured authenticated stream.
                accepted = receiver.verify_status(signed_status(SEED + 2), CONTROLLER)
                self.assertEqual(accepted, status_fields())
                self.assertEqual(receiver.replay[(8, 1, 1)], SEED + 2)

    def test_authenticated_fields_and_replay(self):
        receiver = offline_receiver()
        frame = signed_status(SEED + 1)
        self.assertEqual(receiver.verify_status(frame, CONTROLLER), decode_status(frame[10:116]))
        for replay in (frame, signed_status(SEED), signed_status(SEED + 1, fields={"status_sequence": 8})):
            with self.subTest(case="equal or lower signing timestamp"):
                before = copy.deepcopy(receiver.replay)
                with self.assertRaises(TransportError):
                    receiver.verify_status(replay, CONTROLLER)
                self.assertEqual(receiver.replay, before)
                self.assertEqual(receiver.status_timestamp, SEED + 1)
        self.assertEqual(receiver.verify_status(signed_status(SEED + 2), CONTROLLER), status_fields())

    def test_unsigned_bad_key_bad_signature_and_crc(self):
        bad_signature = bytearray(signed_status(FUTURE))
        bad_signature[-1] ^= 1
        bad_crc = bytearray(signed_status(FUTURE))
        bad_crc[-15] ^= 1
        # Keep MAC valid so this case specifically requires CRC enforcement.
        bad_crc[-6:] = hashlib.sha256(KEY + bad_crc[:-6]).digest()[:6]
        for frame in (
            signed_status(FUTURE, signed=False),
            signed_status(FUTURE, key=bytes(reversed(KEY))),
            bytes(bad_signature), bytes(bad_crc),
        ):
            with self.subTest(case="authentication rejection"):
                self.reject_then_valid(frame)

    def test_authenticated_foreign_bindings(self):
        for frame in (
            signed_status(FUTURE, system=2), signed_status(FUTURE, component=2),
            signed_status(FUTURE, link=9),
            signed_status(FUTURE, fields={"controller_instance_id": CONTROLLER - 1}),
            signed_status(FUTURE, fields={"target_system": 43}),
            signed_status(FUTURE, fields={"target_component": 192}),
        ):
            with self.subTest(case="foreign signed identity"):
                self.reject_then_valid(frame)

    def test_authenticated_codec_failures(self):
        for fields in ({"status_flags": 128}, {"protocol_version": 0}, {"status_class": 0},
                       {"arm_state": 0}, {"status_flags": 59, "decision_code": 22}):
            with self.subTest(fields=fields):
                self.reject_then_valid(signed_status(FUTURE, fields=fields))

    def test_unknown_identity_and_flags_with_valid_signature(self):
        high = signed_status(FUTURE)
        for frame in (
            with_header(high, 7, (200003).to_bytes(3, "little"), crc_extra=0),
            with_header(high, 2, bytes((3,))),
            with_header(high, 3, bytes((1,))),
        ):
            self.reject_then_valid(frame)

    def test_datagram_atomicity(self):
        high = signed_status(FUTURE)
        for frame in (b"", high[:-1], high[:65], high[65:], high + high, high + b"x", b"x" + high):
            self.reject_then_valid(frame)
        receiver = offline_receiver()
        for fragment in (high[:65], high[65:]):
            with self.assertRaises(TransportError):
                receiver.verify_status(fragment, CONTROLLER)
        self.assertEqual(receiver.replay, {})
        self.assertEqual(receiver.verify_status(signed_status(SEED + 1), CONTROLLER), status_fields())

    def test_constructor_crc_bypass_refused_before_socket(self):
        for setting in ("0", "1", ""):
            with self.subTest(setting=setting), patch.dict(os.environ, {"MAV_IGNORE_CRC": setting}):
                with self.assertRaises(TransportError):
                    Guidance(DIALECT, KEY, SEED)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("generated_module", type=Path)
    arguments = parser.parse_args()
    module_path = arguments.generated_module.resolve(strict=True)
    if "MAV_IGNORE_CRC" in os.environ:
        raise RuntimeError("CRC bypass environment is forbidden")
    specification = importlib.util.spec_from_file_location("c1h_test_generated_research", module_path)
    module = importlib.util.module_from_spec(specification)
    specification.loader.exec_module(module)
    if Path(module.__file__).resolve() != module_path or module.MAVLINK_IGNORE_CRC != 0:
        raise RuntimeError("generated module provenance/CRC mismatch")
    global DIALECT
    DIALECT = module
    result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(ReverseStatusTests))
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    raise SystemExit(main())
