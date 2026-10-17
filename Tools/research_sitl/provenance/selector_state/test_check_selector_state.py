#!/usr/bin/env python3
"""Focused corruption tests for the independent P2G selector checker."""

from copy import deepcopy
import importlib.util
from pathlib import Path
import unittest


HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("checker", HERE / "check_selector_state.py")
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)


class SelectorStateCheckerTest(unittest.TestCase):
    def setUp(self):
        self.sensor = {"accelerometer_device_id": 11, "gyroscope_device_id": 22}
        self.trace = {
            "schema": "p2g-selector-state/v1", "run_id": "7", "fault": 0,
            "counts": {"status": 1, "attitude": 1, "local_position": 1},
            "lifecycle": {"selector_start_us": 10, "estimator_start_us": 20,
                          "stop_intent_us": 90, "selector_end_us": 100,
                          "estimator_end_us": 110},
            "binding": [0, 0, 0, 0, 0], "health_policy_configured": False,
            "health_qualified": False, "failover_qualified": False,
            "positive_admission": False, "eligible_for_control": False,
            "selection": {"old_instance": 255, "selected_instance": 0,
                          "instances_available": 1, "instance_changed_count": 1,
                          "selection_us": 30, "last_instance_change_us": 30,
                          "accel_device_id": 11, "gyro_device_id": 22,
                          "publication_succeeded": True},
            "statuses": [{"sequence": 1, "observed_us": 42, "publication_us": 40,
                          "selected_instance": 0, "instances_available": 1,
                          "instance_changed_count": 1, "last_instance_change_us": 30,
                          "accel_device_id": 11, "gyro_device_id": 22,
                          "raw_selected_healthy": True, "publication_succeeded": True}],
            "attitudes": [{"sequence": 1, "observed_us": 60, "sample_us": 45,
                           "estimator_publication_us": 50, "selector_publication_us": 55,
                           "selected_instance": 0, "source_instance": 0,
                           "source_reset_counter": 0, "selected_reset_counter": 0,
                           "publication_succeeded": True}],
            "local_positions": [{"sequence": 1, "observed_us": 70, "sample_us": 56,
                                 "estimator_publication_us": 60, "selector_publication_us": 65,
                                 "selected_instance": 0, "source_instance": 0,
                                 "source_acceleration_bits": [1, 2, 3],
                                 "selected_acceleration_bits": [1, 2, 3],
                                 "source_reset_counters": [0, 0, 0, 0],
                                 "selected_reset_counters": [0, 0, 0, 0],
                                 "publication_succeeded": True}],
        }

    def check(self, trace=None):
        return checker.selector_trace(trace or self.trace, "7", "normal", self.sensor)

    def test_accepts_bound_trace_without_authority(self):
        result = self.check()
        self.assertEqual(result["selection_transition"], [255, 0])
        self.assertEqual(result["acceleration_payloads_preserved"], 1)
        self.assertFalse(result["health_qualified"])

    def test_accepts_unexercised_attitude_path(self):
        trace = deepcopy(self.trace)
        trace["attitudes"] = []
        trace["counts"]["attitude"] = 0
        result = self.check(trace)
        self.assertEqual(result["attitudes"], 0)
        self.assertEqual(result["acceleration_payloads_preserved"], 1)

    def test_rejects_raw_health_as_authority(self):
        trace = deepcopy(self.trace)
        trace["health_qualified"] = True
        with self.assertRaisesRegex(ValueError, "invented selector authority"):
            self.check(trace)

    def test_rejects_sensor_rebinding(self):
        trace = deepcopy(self.trace)
        trace["selection"]["accel_device_id"] = 12
        with self.assertRaisesRegex(ValueError, "selected sensor binding"):
            self.check(trace)

    def test_rejects_noninitial_selection(self):
        trace = deepcopy(self.trace)
        trace["selection"]["old_instance"] = 0
        with self.assertRaisesRegex(ValueError, "selected-instance transition"):
            self.check(trace)

    def test_rejects_status_publish_failure(self):
        trace = deepcopy(self.trace)
        trace["statuses"][0]["publication_succeeded"] = False
        with self.assertRaisesRegex(ValueError, "selector status contract"):
            self.check(trace)

    def test_rejects_output_chronology(self):
        trace = deepcopy(self.trace)
        trace["attitudes"][0]["selector_publication_us"] = 49
        with self.assertRaisesRegex(ValueError, "selected-output chronology"):
            self.check(trace)

    def test_rejects_acceleration_mutation(self):
        trace = deepcopy(self.trace)
        trace["local_positions"][0]["selected_acceleration_bits"][2] = 4
        with self.assertRaisesRegex(ValueError, "mutated acceleration"):
            self.check(trace)


if __name__ == "__main__":
    unittest.main()
