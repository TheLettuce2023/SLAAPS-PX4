"""Raw ULog measurement edge cases and archived-report consistency checks."""

import copy
import contextlib
import hashlib
import io
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

import numpy as np

from inspect_raw_ulog import (ARCHIVED_TOPICS, NUMERIC_FIELDS, EvidenceError,
                              compare_archived, inspect_dataset, inspect_log,
                              load_json, main, stamps)


class RawInspectionTest(unittest.TestCase):
    def test_same_publication_time_preserves_distinct_samples(self):
        dataset = SimpleNamespace(
            name="vehicle_attitude", multi_id=0,
            data={"timestamp": np.array([8000, 8000, 12000], dtype=np.uint64),
                  "timestamp_sample": np.array([4000, 8000, 12000], dtype=np.uint64),
                  "q[0]": np.array([1.0, 0.9, 0.8]),
                  "q[1]": np.array([0.0, 0.1, 0.2]),
                  "q[2]": np.zeros(3), "q[3]": np.zeros(3)})
        result = inspect_dataset(dataset)
        self.assertEqual(result["timestamp"]["zero_steps"], 1)
        self.assertEqual(result["timestamp_sample"]["zero_steps"], 0)
        self.assertEqual(result["equal_publication_pairs"]["sample_gap_counts_us"], {"4000": 1})
        self.assertEqual(result["equal_publication_pairs"]["first_age_counts_us"], {"4000": 1})
        self.assertEqual(result["equal_publication_pairs"]["second_age_counts_us"], {"0": 1})
        self.assertEqual(result["equal_publication_pairs"]["same_quaternion"], 0)

    def test_bad_numeric_and_backward_sample_are_measured(self):
        data = {"timestamp": np.array([8000, 12000, 16000], dtype=np.uint64),
                "timestamp_sample": np.array([4000, 12000, 8000], dtype=np.uint64)}
        for field in NUMERIC_FIELDS["vehicle_attitude"]:
            data[field] = np.ones(3)
        data["q[2]"][1] = np.nan
        result = inspect_dataset(SimpleNamespace(name="vehicle_attitude", multi_id=0, data=data))
        self.assertEqual(result["timestamp_sample"]["negative_steps"], 1)
        self.assertEqual(result["numeric_nonfinite"]["q[2]"], 1)

    def test_negative_timestamp_rejected(self):
        with self.assertRaisesRegex(EvidenceError, "out-of-range"):
            stamps({"timestamp": np.array([-1, 2], dtype=np.int64)}, "timestamp", "example")

    def test_expected_hash_checked_before_parser(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "fake.ulg"
            path.write_bytes(b"not a ULog")
            with self.assertRaisesRegex(EvidenceError, "SHA-256 mismatch"):
                inspect_log(path, "0" * 64)

    def test_duplicate_and_nonfinite_json_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "report.json"
            path.write_text('{"x": 1, "x": 2}')
            with self.assertRaisesRegex(EvidenceError, "duplicate JSON key"):
                load_json(path)
            path.write_text('{"x": NaN}')
            with self.assertRaisesRegex(EvidenceError, "non-finite JSON constant"):
                load_json(path)

    def test_existing_report_is_never_replaced(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "existing.json"
            output.write_text("preserve this evidence")
            fake = {"ulog_sha256": "a" * 64, "ulog_bytes": 1,
                    "dropouts": 0, "missing_topics": [], "topics": {}}
            argv = ["inspect_raw_ulog.py", "unused.ulg", "--expected-sha256",
                    "a" * 64, "--output", str(output)]
            with mock.patch("sys.argv", argv), mock.patch("inspect_raw_ulog.inspect_log", return_value=fake):
                with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                    self.assertEqual(main(), 2)
            self.assertEqual(output.read_text(), "preserve this evidence")

    def test_archived_measurement_mismatch_is_rejected(self):
        timing = {"samples": 2, "first_us": 1, "last_us": 2,
                  "zero_steps": 0, "negative_steps": 0, "max_gap_us": 1}
        current_topics = {}
        archived_topics = {}
        for topic in ARCHIVED_TOPICS:
            finite = {field: 0 for field in NUMERIC_FIELDS.get(topic, ())}
            current_topics[topic] = {"timestamp": timing, "numeric_nonfinite": finite}
            old = {"instance": 0, "timestamp": timing,
                   "required_numeric": {field: {"nonfinite": 0} for field in finite}}
            if topic == "vehicle_attitude":
                current_topics[topic]["equal_publication_pairs"] = {
                    "count": 0, "same_sample": 0, "same_quaternion": 0}
                old["equal_timestamp_pairs"] = {
                    "count": 0, "same_timestamp_sample": 0, "same_quaternion": 0}
            archived_topics[topic] = [old]
        digest = hashlib.sha256(b"example").hexdigest()
        current = {"ulog_sha256": digest, "ulog_bytes": 7,
                   "dropouts": 0, "topics": current_topics}
        archived = {"schema": "sitl-baseline-ulog-measurements/v0",
                    "ulog_sha256": digest, "ulog_bytes": 7, "dropouts": 0,
                    "missing_topics": [], "topics": archived_topics}
        compare_archived(current, archived)
        corrupt = copy.deepcopy(archived)
        corrupt["topics"]["sensor_gps"][0]["timestamp"]["max_gap_us"] = 9
        with self.assertRaisesRegex(EvidenceError, "archived timing mismatch"):
            compare_archived(current, corrupt)
        corrupt = copy.deepcopy(archived)
        corrupt["dropouts"] = False
        with self.assertRaisesRegex(EvidenceError, "archived dropout/topic mismatch"):
            compare_archived(current, corrupt)


if __name__ == "__main__":
    unittest.main()
