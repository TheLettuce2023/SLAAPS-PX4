#!/usr/bin/env python3
"""Focused corruption tests for the independent P2E trace checker."""

from copy import deepcopy
import importlib.util
from pathlib import Path
import unittest


HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("p2e_check", HERE / "check_committed_state.py")
p2e_check = importlib.util.module_from_spec(spec)
spec.loader.exec_module(p2e_check)


class CommittedTraceTest(unittest.TestCase):
    def setUp(self):
        self.sensor = dict(accelerometer_device_id=11, gyroscope_device_id=12,
                           accelerometer_calibration_count=2, gyroscope_calibration_count=3)
        self.merged = {"records": [{"sequence": 1, "source_us": 20, "sample_us": 10,
                                     "control_flags": 1, "fault_flags": 0, "validity": [1, 1, 1, 1]}]}
        self.trace = {
            "schema": "p2e-committed-state/v1", "run_id": "9", "fault": 0, "count": 1,
            "start_us": 5, "stop_intent_us": 40, "end_us": 50, "health_qualified": False,
            "positive_admission": False, "eligible_for_control": False,
            "records": [{
                "sequence": 1, "source_us": 30, "sample_us": 10, "update_committed": True,
                "sensor": [11, 12, 2, 3], "control_flags": 1, "fault_flags": 0,
                "validity": [1, 1, 1, 1, 1], "resets": [4, 5, 6, 7, 8],
                "attitude": {"quaternion_bits": [0, 1, 2, 3], "reset": 4},
                "local_position": {"position_bits": [0, 1, 2], "velocity_bits": [3, 4, 5],
                                   "acceleration_bits": [6, 7, 8], "validity": [1, 1, 1, 1],
                                   "resets": [4, 5, 6, 7, 8]}
            }]
        }

    def check(self, trace=None, merged=None):
        return p2e_check.committed_trace(trace or self.trace, "9", "normal",
                                         merged or self.merged, self.sensor)

    def test_accepts_bound_trace(self):
        result = self.check()
        self.assertEqual(result["committed_updates"], 1)
        self.assertEqual(result["attitude_publications"], 1)
        self.assertFalse(result["health_qualified"])

    def test_rejects_invented_authority(self):
        trace = deepcopy(self.trace)
        trace["eligible_for_control"] = True
        with self.assertRaisesRegex(ValueError, "invented committed-state authority"):
            self.check(trace)

    def test_rejects_sensor_rebinding(self):
        trace = deepcopy(self.trace)
        trace["records"][0]["sensor"][0] += 1
        with self.assertRaisesRegex(ValueError, "sensor provenance drift"):
            self.check(trace)

    def test_rejects_missing_committed_payload(self):
        trace = deepcopy(self.trace)
        trace["records"][0]["local_position"] = None
        with self.assertRaisesRegex(ValueError, "missing committed local-position publication"):
            self.check(trace)

    def test_rejects_reset_rebinding(self):
        trace = deepcopy(self.trace)
        trace["records"][0]["local_position"]["resets"][2] += 1
        with self.assertRaisesRegex(ValueError, "local-position commit binding"):
            self.check(trace)

    def test_rejects_p2d_payload_mismatch(self):
        trace = deepcopy(self.trace)
        trace["records"][0]["sample_us"] += 1
        with self.assertRaisesRegex(ValueError, "committed/P2D payload differs"):
            self.check(trace)

    def test_rejects_unpublished_attitude_payload(self):
        trace = deepcopy(self.trace)
        trace["records"][0]["validity"][0] = 0
        merged = deepcopy(self.merged)
        merged["records"][0]["validity"][0] = 0
        with self.assertRaisesRegex(ValueError, "invalid attitude was presented as published"):
            self.check(trace, merged)


if __name__ == "__main__":
    unittest.main()
