#!/usr/bin/env python3
"""Synthetic evidence tests, not physical bench results."""

import copy
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from check_bench_acceptance import REQUIREMENTS, audit, load, template


class AcceptanceTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.artifact = self.root / "synthetic.txt"
        self.artifact.write_bytes(b"SYNTHETIC TEST ONLY\n")
        reference = {"path": "synthetic.txt", "sha256": hashlib.sha256(
            self.artifact.read_bytes()).hexdigest()}
        self.record = template()
        self.record["baseline"] = reference
        for entry in self.record["requirements"].values():
            entry.update(status="reviewed", reviewer="Synthetic test reviewer",
                         reviewed_utc="2026-10-01T00:00:00Z", evidence=[reference])

    def check_blocked(self, record):
        result = audit(record, self.root)
        self.assertFalse(result["evidence_complete"])
        self.assertTrue(result["blockers"])
        self.assertFalse(result["outputs_authorized"])
        self.assertFalse(result["flash_authorized"])
        self.assertFalse(result["physical_acceptance_granted"])

    def test_complete_is_not_authority(self):
        result = audit(self.record, self.root)
        self.assertTrue(result["evidence_complete"])
        for field in ("outputs_authorized", "flash_authorized", "physical_acceptance_granted"):
            self.assertFalse(result[field])

    def test_blank(self):
        self.check_blocked(template())

    def test_every_requirement_mandatory(self):
        for key in REQUIREMENTS:
            with self.subTest(key=key):
                record = copy.deepcopy(self.record)
                del record["requirements"][key]
                self.check_blocked(record)

    def test_assumptions_are_not_results(self):
        for state in ("assumed", "missing", "passed", True, None):
            with self.subTest(state=state):
                record = copy.deepcopy(self.record)
                record["requirements"]["power"]["status"] = state
                self.check_blocked(record)

    def test_corruption(self):
        self.artifact.write_bytes(b"changed")
        self.check_blocked(self.record)

    def test_missing_artifact(self):
        self.artifact.unlink()
        self.check_blocked(self.record)

    def test_paths(self):
        for name in ("../escape", "/absolute", "C:/absolute", "sub\\file", ""):
            with self.subTest(name=name):
                record = copy.deepcopy(self.record)
                record["baseline"]["path"] = name
                self.check_blocked(record)

    def test_symlink_escape(self):
        with tempfile.TemporaryDirectory() as outside:
            path = Path(outside) / "outside.txt"
            path.write_bytes(self.artifact.read_bytes())
            (self.root / "escape").symlink_to(path)
            self.record["baseline"]["path"] = "escape"
            self.check_blocked(self.record)

    def test_empty_evidence(self):
        self.record["requirements"]["timing"]["evidence"] = []
        self.check_blocked(self.record)

    def test_review_metadata(self):
        for field, value in (("reviewer", " "), ("summary", ""),
                             ("reviewed_utc", "2026-02-30T00:00:00Z"),
                             ("reviewed_utc", "yesterday")):
            with self.subTest(field=field, value=value):
                record = copy.deepcopy(self.record)
                record["requirements"]["timing"][field] = value
                self.check_blocked(record)

    def test_unknown_fields(self):
        self.record["outputs_authorized"] = True
        self.check_blocked(self.record)

    def test_wrong_scope(self):
        self.record["scope"] = "flight"
        self.check_blocked(self.record)

    def test_bad_schema_version(self):
        for value in (True, 1.0, "1", 2):
            self.record["schema_version"] = value
            self.check_blocked(self.record)

    def test_malformed_records(self):
        for value in (None, [], 4, {"requirements": None}):
            self.check_blocked(value)

    def test_duplicate_json(self):
        self.artifact.write_text('{"scope":"flight","scope":"servo_only_wind_tunnel"}')
        with self.assertRaises(ValueError):
            load(self.artifact)

    def test_nonfinite_json(self):
        self.artifact.write_text('{"value":NaN}')
        with self.assertRaises(ValueError):
            load(self.artifact)

    def cli(self, content):
        record_path = self.root / "record.json"
        record_path.write_text(content)
        return subprocess.run([sys.executable, "-B", str(Path(__file__).with_name(
            "check_bench_acceptance.py")), str(record_path), "--evidence-root",
            str(self.root)], capture_output=True, text=True, check=False)

    def test_cli_incomplete(self):
        result = self.cli(json.dumps(template()))
        self.assertEqual(result.returncode, 1)
        self.assertFalse(json.loads(result.stdout)["evidence_complete"])

    def test_cli_malformed(self):
        result = self.cli("{invalid")
        self.assertEqual(result.returncode, 2)
        self.assertFalse(json.loads(result.stdout)["outputs_authorized"])

    def test_cli_complete_not_accepted(self):
        result = self.cli(json.dumps(self.record))
        self.assertEqual(result.returncode, 0)
        self.assertFalse(json.loads(result.stdout)["physical_acceptance_granted"])


if __name__ == "__main__":
    unittest.main()
