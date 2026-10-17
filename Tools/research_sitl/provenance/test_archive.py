"""Finite corruption and ordering checks; fixtures confer no native provenance."""

import copy
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

import numpy as np

import check_archive
import collect_archive
from contract import digest, encode, load, loads


class ArchiveTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / "fixture.ulg"
        self.source.write_bytes(b"synthetic parser fixture; not a native ULog")
        self.sha = digest(self.source)
        # Preserve equal and backwards publication time; sample stalls while
        # publication progresses; a reset counter wraps. Never sort these away.
        attitude = SimpleNamespace(name="vehicle_attitude", multi_id=0, data={
            "timestamp": np.array([100, 120, 120, 110], dtype=np.uint64),
            "timestamp_sample": np.array([90, 90, 115, 112], dtype=np.uint64),
            "q[0]": np.array([1, 1, np.nan, np.inf], dtype=np.float32),
            "quat_reset_counter": np.array([254, 255, 0, 0], dtype=np.uint8)})
        flags = SimpleNamespace(name="estimator_status_flags", multi_id=2, data={
            "timestamp": np.array([101, 121], dtype=np.uint64),
            "timestamp_sample": np.array([90, 100], dtype=np.uint64),
            "cs_tilt_align": np.array([True, False]), "fs_bad_hdg": np.array([False, True])})
        self.log = SimpleNamespace(data_list=[attitude, flags], dropouts=[object()], file_corruption=False)
        for module in (collect_archive, check_archive):
            context = patch.object(module, "ULog", return_value=self.log)
            context.start()
            self.addCleanup(context.stop)
        self.output = self.root / "out"

    def collect(self):
        return collect_archive.collect(self.source, self.sha, self.output)

    def verify(self):
        return check_archive.verify(self.source, self.sha, self.output)

    def test_order_age_reset_nonfinite_and_missing_coverage(self):
        report = self.collect()
        attitude = next(s for s in report["streams"] if s["topic"] == "vehicle_attitude")
        self.assertEqual(attitude["observations"]["publication"]["backward_steps"], 1)
        self.assertEqual(attitude["observations"]["publication"]["equal_steps"], 1)
        self.assertEqual(attitude["observations"]["sample_age_at_publication_us"],
                         {"minimum": -2, "maximum": 30, "negative_count": 1})
        self.assertEqual(attitude["observations"]["field_changes"]["quat_reset_counter"], 2)
        self.assertEqual(attitude["observations"]["nonfinite_values"], {"q[0]": 2})
        self.assertIn("q[1]", attitude["missing_fields"])
        self.assertIn("estimator_selector_status", report["missing_topics"])
        self.assertEqual(report["logger_dropout_count"], 1)
        self.assertEqual(self.verify()["receipt_count"], 6)

    def test_wrong_hash_and_existing_destination_rejected(self):
        with self.assertRaises(ValueError):
            collect_archive.collect(self.source, "0" * 64, self.output)
        self.collect()
        before = (self.output / "manifest.json").read_bytes()
        with self.assertRaises(ValueError):
            self.collect()
        self.assertEqual(before, (self.output / "manifest.json").read_bytes())

    def test_summary_or_authority_forgery_rejected(self):
        original = self.collect()
        variants = []
        changed = copy.deepcopy(original)
        changed["collector_capabilities"]["can_select_diagnostics"] = True
        variants.append(changed)
        changed = copy.deepcopy(original)
        changed["streams"][1]["observations"]["publication"]["backward_steps"] = 0
        variants.append(changed)
        changed = copy.deepcopy(original)
        changed["limitations"] = []
        variants.append(changed)
        for changed in variants:
            with self.subTest(changed=changed):
                (self.output / "manifest.json").write_text(encode(changed))
                with self.assertRaises(ValueError):
                    self.verify()

    def test_reordered_or_modified_rows_reject_even_after_rehash(self):
        self.collect()
        journal = self.output / "receipts.jsonl"
        original = journal.read_text().splitlines()
        for kind in ("reorder", "bool", "extra", "truncate"):
            lines = list(original)
            if kind == "reorder":
                lines[-1], lines[-2] = lines[-2], lines[-1]
            elif kind == "bool":
                row = loads(lines[-1]); row["values"]["timestamp"] = True; lines[-1] = encode(row)
            elif kind == "extra":
                lines.append(lines[-1])
            else:
                lines.pop()
            journal.write_text("\n".join(lines) + "\n")
            manifest = load(self.output / "manifest.json")
            manifest["receipts"].update(sha256=digest(journal), bytes=journal.stat().st_size)
            (self.output / "manifest.json").write_text(encode(manifest))
            with self.subTest(kind=kind), self.assertRaises(ValueError):
                self.verify()

    def test_strict_json(self):
        for bad in ('{"x":1,"x":2}', '{"x":NaN}', '{"x":1e999}'):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                loads(bad)

    def test_resource_bound_and_midread_input_change(self):
        self.log.file_corruption = True
        with self.assertRaises(ValueError):
            self.collect()
        self.log.file_corruption = False
        with patch.object(collect_archive, "MAX_RECEIPTS", 5), self.assertRaises(ValueError):
            self.collect()
        self.assertFalse(self.output.exists())
        original = collect_archive.scalar
        def mutate(value):
            self.source.write_bytes(b"modified during extraction")
            return original(value)
        with patch.object(collect_archive, "scalar", side_effect=mutate), self.assertRaises(ValueError):
            self.collect()
        self.assertFalse((self.output / "manifest.json").exists())


if __name__ == "__main__":
    unittest.main()
