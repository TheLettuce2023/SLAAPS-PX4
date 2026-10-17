"""Regression checks for diagnostic integrity and non-acceptance."""

import hashlib
import json
import tempfile
import unittest
from pathlib import Path

from audit_diagnostic import AuditError, audit


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


class DiagnosticAuditTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.evidence = self.root / "evidence"
        self.evidence.mkdir()
        self.ulogs = {}
        runs = []
        inventories = []
        for mode, suffix in (("GUI", "gui"), ("headless", "headless")):
            path = self.root / "build" / (suffix + ".ulg")
            path.parent.mkdir(exist_ok=True)
            path.write_bytes((suffix + " ULog").encode())
            self.ulogs[suffix] = path
            inventories.append({"mode": mode, "path": str(path), "bytes": path.stat().st_size,
                                "sha256": sha(path)})
            runs.append({"mode": mode, "ulog_path": str(path), "ulog_bytes": path.stat().st_size,
                         "ulog_sha256": sha(path), "ulog_dropouts": 0,
                         "selected_topics_present": 14, "attitude_zero_timestamp_steps": 0,
                         "attitude_negative_timestamp_steps": 0,
                         "attitude_equal_timestamp_same_sample": 0,
                         "attitude_equal_timestamp_same_quaternion": 0})
            topics = {"topic_" + str(index): [] for index in range(13)}
            topics["vehicle_attitude"] = [{"timestamp": {"zero_steps": 0, "negative_steps": 0},
                                           "equal_timestamp_pairs": {
                                               "same_timestamp_sample": 0,
                                               "same_quaternion": 0}}]
            self.write_json("ulog-measurements-" + suffix + ".json", {
                "schema": "sitl-baseline-ulog-measurements/v0",
                "ulog_sha256": sha(path), "ulog_bytes": path.stat().st_size,
                "dropouts": 0, "missing_topics": [], "topics": topics})
        self.write_json("plan.json", {
            "schema": "sitl-baseline-diagnostic-plan/v0", "run_id": "sample",
            "source": {"revision": "source"},
            "stock_model": {"submodule_revision": "model"},
            "phase1_numeric_acceptance": "unapproved"})
        self.write_json("build-result.json", {
            "schema": "sitl-baseline-build-result/v0", "run_id": "sample",
            "executable_sha256": "executable", "build_log": "build.log",
            "build_log_sha256": "placeholder"})
        (self.evidence / "build.log").write_text("build transcript\n")
        build = self.read_json("build-result.json")
        build["build_log_sha256"] = sha(self.evidence / "build.log")
        self.write_json("build-result.json", build)
        self.write_json("campaign-result.json", {
            "schema": "sitl-baseline-diagnostic-result/v0", "run_id": "sample",
            "phase1_result": "incomplete", "source_revision": "source",
            "gz_submodule_revision": "model", "executable_sha256": "executable",
            "runs": runs})
        self.write_json("qgc-observation.json", {
            "schema": "sitl-qgc-observation/v0", "run_id": "sample-gui",
            "human_visual_observation": "pending"})
        for name in ("git-status-before.txt", "git-status-after.txt"):
            (self.evidence / name).write_text("untracked research\n")
        self.refresh_inventory(inventories)

    def write_json(self, name, value):
        (self.evidence / name).write_text(json.dumps(value), encoding="utf-8")

    def read_json(self, name):
        return json.loads((self.evidence / name).read_text(encoding="utf-8"))

    def refresh_inventory(self, ulogs):
        files = sorted(path for path in self.evidence.iterdir()
                       if path.name != "artifact-inventory.json")
        self.write_json("artifact-inventory.json", {
            "schema": "sitl-baseline-artifact-inventory/v0", "run_id": "sample",
            "evidence_files": [{"name": path.name, "bytes": path.stat().st_size,
                                "sha256": sha(path)} for path in files],
            "ulogs": ulogs})

    def test_intact_package_is_incomplete(self):
        result = audit(self.root)
        self.assertEqual(result["integrity"], "verified")
        self.assertEqual(result["result"], "incomplete")

    def test_tampered_inventory_file_fails(self):
        (self.evidence / "build.log").write_text("changed transcript\n")
        with self.assertRaisesRegex(AuditError, "mismatch"):
            audit(self.root)

    def test_false_phase_one_pass_fails_even_when_rehashed(self):
        campaign = self.read_json("campaign-result.json")
        campaign["phase1_result"] = "pass"
        self.write_json("campaign-result.json", campaign)
        self.refresh_inventory(self.read_json("artifact-inventory.json")["ulogs"])
        with self.assertRaisesRegex(AuditError, "cannot claim Phase 1 pass"):
            audit(self.root)

    def test_relinked_ulog_fails_even_when_inventory_rehashed(self):
        campaign = self.read_json("campaign-result.json")
        campaign["runs"][0]["ulog_sha256"] = "0" * 64
        self.write_json("campaign-result.json", campaign)
        self.refresh_inventory(self.read_json("artifact-inventory.json")["ulogs"])
        with self.assertRaisesRegex(AuditError, "ULog link mismatch"):
            audit(self.root)


if __name__ == "__main__":
    unittest.main()
