#!/usr/bin/env python3
"""Protocol fixtures for the read-only bootloader inspection path; no hardware."""

import base64
from pathlib import Path
import struct
import tempfile
from types import SimpleNamespace
import unittest
import zlib
import json

import inspect_bootloader as diagnostic


class SerialFixture:
    def __init__(self, exchanges):
        self.exchanges = list(exchanges)
        self.pending = bytearray()
        self.writes = []

    def write(self, frame):
        expected, reply = self.exchanges.pop(0)
        if frame != expected or self.pending:
            raise AssertionError(f"Unexpected frame: {frame.hex()}")
        self.writes.append(frame)
        self.pending.extend(reply)
        return len(frame)

    def flush(self):
        pass

    def read(self, count):
        count = min(count, 3)  # Exercise fragmented USB/serial responses.
        data = bytes(self.pending[:count])
        del self.pending[:count]
        return data


def fixture(image, stored=None, board=53, size=1966080, reject_vectors=False):
    stored = image if stored is None else stored
    ok = diagnostic.OK
    exchanges = [(b"\x21\x20", ok),
                 (b"\x22\x01\x20", struct.pack("<I", 5) + ok),
                 (b"\x22\x02\x20", struct.pack("<I", board) + ok)]
    if board != diagnostic.EXPECTED_BOARD:
        return SerialFixture(exchanges)
    exchanges += [(b"\x22\x03\x20", struct.pack("<I", 0) + ok),
                  (b"\x22\x04\x20", struct.pack("<I", size) + ok)]
    if size != diagnostic.EXPECTED_SLOT:
        return SerialFixture(exchanges)
    exchanges += [(b"\x28\x20\x20", diagnostic.INVALID if reject_vectors else stored[:32] + ok)]
    if reject_vectors:
        exchanges += [(b"\x21\x20", ok)]
    exchanges += [(b"\x29\x20", struct.pack("<I", diagnostic.slot_crc(stored, size)) + ok),
                  (b"\x2f\x20", struct.pack("<I", 7) + b"test-bl" + ok)]
    return SerialFixture(exchanges)


class BootloaderInspectionTests(unittest.TestCase):
    def setUp(self):
        self.image = struct.pack("<8I", 0x2401B628, *([0x080202A1] * 7)) + bytes(range(96))

    def test_crc_convention_matches_known_uncomplemented_vector(self):
        self.assertEqual(diagnostic.slot_crc(b"123456789", 9), 0x2DFD2D88)

    def test_matching_cold_flash_can_be_verified_without_boot_command(self):
        port = fixture(self.image)
        result = diagnostic.inspect(diagnostic.Reader(port), self.image)
        self.assertEqual(result["outcome"], "candidate_vectors_and_slot_crc_match")
        self.assertTrue(result["vectors_match"])
        self.assertTrue(result["slot_crc_matches"])
        self.assertEqual(result["bootloader_version_text"], "test-bl")
        self.assertFalse(port.exchanges)
        self.assertTrue(all(frame[0] in (0x21, 0x22, 0x28, 0x29, 0x2F) for frame in port.writes))

    def test_erased_lead_words_distinguish_unfinalized_image(self):
        stored = b"\xff" * 32 + self.image[32:]
        result = diagnostic.inspect(diagnostic.Reader(fixture(self.image, stored)), self.image)
        self.assertEqual(result["outcome"], "candidate_body_matches_but_first_32_bytes_are_erased")
        self.assertFalse(result["slot_crc_matches"])
        self.assertTrue(result["first_32_bytes_erased"])

    def test_changed_body_is_not_mistaken_for_unfinalized_image(self):
        stored = b"\xff" * 32 + b"wrong" + self.image[37:]
        result = diagnostic.inspect(diagnostic.Reader(fixture(self.image, stored)), self.image)
        self.assertEqual(result["outcome"], "first_32_bytes_are_erased_flash_contents_not_fully_matched")

    def test_unsupported_vector_query_keeps_crc_evidence_separate(self):
        result = diagnostic.inspect(diagnostic.Reader(fixture(self.image, reject_vectors=True)), self.image)
        self.assertEqual(result["outcome"], "slot_crc_matches_vectors_unavailable")
        self.assertIsNone(result["vectors_match"])

    def test_rejected_version_query_does_not_discard_flash_evidence(self):
        port = fixture(self.image)
        port.exchanges[-1] = (b"\x2f\x20", diagnostic.INVALID)
        result = diagnostic.inspect(diagnostic.Reader(port), self.image)
        self.assertEqual(result["outcome"], "candidate_vectors_and_slot_crc_match")
        self.assertIsNone(result["bootloader_version_text"])

    def test_matching_upload_crc_cannot_hide_erased_stored_vectors(self):
        stored = b"\xff" * 32 + self.image[32:]
        port = fixture(self.image, stored)
        port.exchanges[-2] = (b"\x29\x20", struct.pack("<I", diagnostic.slot_crc(self.image, 1966080)) + diagnostic.OK)
        result = diagnostic.inspect(diagnostic.Reader(port), self.image)
        self.assertTrue(result["slot_crc_matches"])
        self.assertTrue(result["first_32_bytes_erased"])
        self.assertNotEqual(result["outcome"], "candidate_vectors_and_slot_crc_match")

    def test_wrong_board_stops_before_flash_queries(self):
        port = fixture(self.image, board=9)
        with self.assertRaises(diagnostic.InspectionError):
            diagnostic.inspect(diagnostic.Reader(port), self.image)
        self.assertEqual([frame[0] for frame in port.writes], [0x21, 0x22, 0x22])

    def test_wrong_slot_stops_before_readback(self):
        port = fixture(self.image, size=1024)
        with self.assertRaises(diagnostic.InspectionError):
            diagnostic.inspect(diagnostic.Reader(port), self.image)
        self.assertFalse(any(frame[0] in (0x28, 0x29) for frame in port.writes))

    def test_mutating_commands_and_unsafe_read_lengths_cannot_be_sent(self):
        port = SerialFixture([])
        reader = diagnostic.Reader(port)
        for opcode in (0x23, 0x27, 0x30, 0x2D, 0x33, 0x40):
            with self.assertRaises(diagnostic.InspectionError):
                reader.send(opcode)
        with self.assertRaises(diagnostic.InspectionError):
            reader.send(0x28, b"\x40")
        self.assertFalse(port.writes)
        self.assertEqual(bytes([diagnostic.VECTOR_BYTES]), b"\x20")

    def test_bad_status_and_truncated_frames_fail(self):
        for reply in (b"\x12\x11", b"\x12"):
            with self.assertRaises(diagnostic.InspectionError):
                diagnostic.Reader(SerialFixture([(b"\x21\x20", reply)])).query(0x21)

    def test_application_usb_port_is_rejected(self):
        for vid, pid in ((0x3185, 0x0035), (0x1209, 0x5740)):
            with self.assertRaises(diagnostic.InspectionError):
                diagnostic.select_bootloader([SimpleNamespace(device="COM8", vid=vid, pid=pid)], "COM8")

    def test_wait_catches_bootloader_without_selecting_an_application(self):
        application = SimpleNamespace(device="COM14", vid=0x3185, pid=0x0035)
        bootloader = SimpleNamespace(device="COM14", vid=0x1209, pid=0x5741)
        scans = iter([[application], [], [bootloader]])
        elapsed = [0.0]

        def sleep(seconds):
            elapsed[0] += seconds

        actual = diagnostic.wait_for_bootloader(lambda: next(scans), "COM14", 1,
                                               clock=lambda: elapsed[0], sleep=sleep)
        self.assertIs(actual, bootloader)
        self.assertLess(elapsed[0], 1)

    def test_wait_timeout_does_not_open_a_port(self):
        elapsed = [0.0]

        def sleep(seconds):
            elapsed[0] += seconds

        with self.assertRaises(diagnostic.InspectionError):
            diagnostic.wait_for_bootloader(lambda: [], "COM14", 0.5,
                                           clock=lambda: elapsed[0], sleep=sleep)

    def test_transient_missing_port_is_closed_and_retried(self):
        device = SimpleNamespace(device="COM14", vid=0x1209, pid=0x5741)
        elapsed = [0.0]
        created = []

        class OpeningPort:
            def __init__(self, **kwargs):
                self.closed = False
                self.fail = not created
                created.append(self)

            def open(self):
                if self.fail:
                    raise FileNotFoundError("USB handoff")

            def close(self):
                self.closed = True

        def sleep(seconds):
            elapsed[0] += seconds

        actual, port, errors = diagnostic.open_bootloader(lambda: [device], OpeningPort, "COM14", 1,
                                                          clock=lambda: elapsed[0], sleep=sleep)
        self.assertIs(actual, device)
        self.assertIs(port, created[1])
        self.assertTrue(created[0].closed)
        self.assertEqual(errors, ["USB handoff"])
        self.assertFalse(port.dtr)
        self.assertFalse(port.rts)

    def test_package_decoding_validates_board_size_and_contents(self):
        package = {"board_id": 53, "image_maxsize": 1966080, "image_size": len(self.image),
                   "image": base64.b64encode(zlib.compress(self.image)).decode()}
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "candidate.px4"
            path.write_text(json.dumps(package))
            image, _ = diagnostic.load_candidate(path)
            self.assertEqual(image, self.image)
            package["image_size"] += 4
            path.write_text(json.dumps(package))
            with self.assertRaises(diagnostic.InspectionError):
                diagnostic.load_candidate(path)


if __name__ == "__main__":
    unittest.main()
