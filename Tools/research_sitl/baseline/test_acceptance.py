"""Focused false-pass guards for the approved interval and evidence binding."""

from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import numpy as np

from acceptance import artifact, clock_bracket, utc
from capture_run import Console
from inspect_raw_ulog import EvidenceError
from score_interval import registry, score_datasets, STATE_RULES


START, END = 20000000, 80000000


def fixture():
    limits = registry()
    datasets = []
    for name in limits["required_raw_topics_instance_0"]:
        step = limits["maximum_logged_gap_us"].get(name, 1000000) // 2
        t = np.arange(START - step, END + step + 1, step, dtype=np.int64)
        data = {"timestamp": t}
        if name in limits["maximum_publication_minus_sample_age_us"] or name == "differential_pressure":
            data["timestamp_sample"] = t.copy()
        for field in limits["validity"]["required_finite_fields"].get(name, []):
            data[field] = np.ones(t.size)
        for field, expected in STATE_RULES.get(name, {}).items():
            data[field] = np.full(t.size, expected)
        if name == "sensor_gps":
            data["fix_type"] = np.full(t.size, 3)
        if name == "vehicle_status":
            data["gcs_connection_lost"] = np.zeros(t.size)
        if name == "telemetry_status":
            data["rx_message_count"] = np.arange(t.size, dtype=np.uint32)
            data["heartbeat_type_gcs"] = np.ones(t.size)
        datasets.append(SimpleNamespace(name=name, multi_id=0, data=data))
    return datasets


class AcceptanceTest(unittest.TestCase):
    def test_wall_utc_step_is_reported_without_reordering_px4_time(self):
        pair = [{"px4_us": 20828000, "utc": "2026-09-28T00:33:35.368856Z"},
                {"px4_us": 21836000, "utc": "2026-09-28T00:33:35.157718Z"}]
        ready, stopped = utc("2026-09-28T00:33:20Z"), utc("2026-09-28T00:35:00Z")
        self.assertTrue(clock_bracket(pair, 21756000, ready, stopped))
        with self.assertRaises(EvidenceError):
            clock_bracket(list(reversed(pair)), 21756000, ready, stopped)

    def test_async_startup_warning_after_prompt_does_not_timeout(self):
        console = object.__new__(Console)
        console.text = "pxh> commander check\nINFO preflight\npxh> WARN startup message\n"
        console.drain = lambda seconds: None
        console.process = SimpleNamespace(poll=lambda: None)
        self.assertIn("WARN startup", console.wait_prompt(command="commander check", timeout=0.1))

    def test_nominal_and_missing_topic(self):
        streams = fixture()
        self.assertEqual(score_datasets(streams, START, END, True)["status"], "pass")
        self.assertEqual(score_datasets(streams[1:], START, END, True)["status"], "incomplete")

    def test_duplicate_publication_requires_distinct_samples_and_finite_quaternion(self):
        streams = fixture()
        attitude = next(s.data for s in streams if s.name == "vehicle_attitude")
        attitude["timestamp"][2] = attitude["timestamp"][3]
        attitude["timestamp_sample"][2] = attitude["timestamp"][3] - 4000
        # A 20 ms logged gap meets the inclusive 20 ms limit.
        result = score_datasets(streams, START, END, True)
        self.assertEqual(result["status"], "pass", result["failures"])
        self.assertEqual(len(result["topics"]["vehicle_attitude"]["equal_publication_pairs"]), 1)
        attitude["timestamp_sample"][3] = attitude["timestamp_sample"][2]
        self.assertEqual(score_datasets(streams, START, END, True)["status"], "fail")
        attitude["q[0]"][2] = np.nan
        self.assertTrue(any(item["check"].endswith("finite_q[0]") for item in
                            score_datasets(streams, START, END, True)["failures"]))

    def test_tail_gap_and_counter_stall_cannot_hide_at_interval_boundary(self):
        streams = fixture()
        attitude = next(s for s in streams if s.name == "vehicle_attitude")
        keep = (attitude.data["timestamp"] < END - 50000) | (attitude.data["timestamp"] > END)
        attitude.data = {k: v[keep] for k, v in attitude.data.items()}
        telemetry = next(s.data for s in streams if s.name == "telemetry_status")
        telemetry["rx_message_count"][:] = 1
        result = score_datasets(streams, START, END, True)
        checks = {item["check"] for item in result["failures"]}
        self.assertIn("vehicle_attitude.logged_gap", checks)
        self.assertIn("telemetry_status.rx_advance_gap", checks)

    def test_changed_registry_and_escaping_artifact_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            changed = root / "registry.json"
            changed.write_text('{}')
            with patch("score_interval.REGISTRY_PATH", changed), self.assertRaises(EvidenceError):
                registry()
            package = root / "package"
            package.mkdir()
            with self.assertRaises(EvidenceError):
                artifact(package, {"path": "../registry.json", "sha256": "0" * 64, "bytes": 2})

    def test_pressure_exception_requires_source_identity_and_all_zero_stream(self):
        streams = fixture()
        pressure = next(s.data for s in streams if s.name == "differential_pressure")
        pressure["timestamp_sample"] = np.zeros(pressure["timestamp"].size, dtype=np.int64)
        pressure["device_id"] = np.full(pressure["timestamp"].size, 1377548)
        self.assertEqual(score_datasets(streams, START, END, True)["status"], "fail")
        self.assertEqual(score_datasets(streams, START, END, True, synthetic_pressure_verified=True)["status"], "pass")
        pressure["timestamp_sample"][3] = pressure["timestamp"][3]
        self.assertEqual(score_datasets(streams, START, END, True, synthetic_pressure_verified=True)["status"], "fail")


if __name__ == "__main__":
    unittest.main()
