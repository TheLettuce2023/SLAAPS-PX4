"""Offline gate and loopback primitives; these never start a PX4 process."""

import importlib.util
import json
import os
from pathlib import Path
import socket
import stat
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("c1e_runtime_capture", ROOT / "capture_native_ingress.py")
import sys
sys.path.insert(0, str(ROOT))
capture = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(capture)


class OfflineCaptureTests(unittest.TestCase):
    def test_missing_or_unapproved_review_blocks_before_build(self):
        with tempfile.TemporaryDirectory(prefix="c1e_px4_", dir="/var/tmp") as area:
            root = Path(area)
            review = root / "technical_review.json"
            with self.assertRaises(capture.checker.EvidenceError):
                capture.reviewed_build(root, review)
            record = {name: "0" * 64 for name in
                      ("build_manifest_sha256", "source_registry_sha256", "stack_registry_sha256",
                       "generated_registry_sha256", "executable_sha256", "startup_sha256")}
            record.update(schema=capture.REVIEW_SCHEMA, decision="pending", reviewer="synthetic",
                          scope="negative_diagnostic_disarmed_output_disabled")
            review.write_text(json.dumps(record))
            with self.assertRaises(capture.checker.EvidenceError):
                capture.reviewed_build(root, review)
            record["decision"] = "approved_for_disarmed_runs"
            review.write_text(json.dumps(record))
            with self.assertRaises(capture.checker.EvidenceError):
                capture.reviewed_build(root, review)

    def test_distinct_identities_and_peer_socket_owner(self):
        ids, ports = set(), set()
        first = capture._bindings(ids, ports)
        second = capture._bindings(ids, ports)
        self.assertTrue(all(first[name] != second[name] for name in capture.IDS))
        self.assertEqual(len(ports), 4)
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as endpoint:
            endpoint.bind(("127.0.0.1", 0))
            port = endpoint.getsockname()[1]
            self.assertTrue(capture._udp_socket_in_pid(os.getpid(), port),
                            (os.readlink(f"/proc/self/fd/{endpoint.fileno()}"),
                             Path("/proc/net/udp").read_text()))
        self.assertFalse(capture._udp_socket_in_pid(os.getpid(), port))

    def test_one_time_key_permissions_and_unlink(self):
        with tempfile.TemporaryDirectory(prefix="c1e_capture_test_", dir="/var/tmp") as area:
            path = capture._key_file(Path(area), bytes(range(32)))
            self.assertEqual(path.read_bytes(), bytes(range(32)))
            self.assertEqual(stat.S_IMODE(path.stat().st_mode), 0o600)
            with self.assertRaises(FileExistsError):
                capture._key_file(Path(area), b"x" * 32)
            path.unlink()


if __name__ == "__main__":
    unittest.main()
