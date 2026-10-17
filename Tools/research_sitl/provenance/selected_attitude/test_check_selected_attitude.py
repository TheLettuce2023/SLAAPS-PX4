#!/usr/bin/env python3
"""Focused corruption tests for the independent P2I checker."""

from copy import deepcopy
import importlib.util
from pathlib import Path
import unittest


HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("checker", HERE / "check_selected_attitude.py")
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)


class SelectedAttitudeCheckerTest(unittest.TestCase):
    def setUp(self):
        self.selector = {"attitudes": [{
            "sample_us": 30, "estimator_publication_us": 40,
            "selector_publication_us": 50, "selected_instance": 0, "source_instance": 0,
            "source_reset_counter": 0, "selected_reset_counter": 0,
            "publication_succeeded": True,
        }]}
        quaternion = [1065353216, 0, 0, 0]
        self.trace = {
            "schema": "p2i-selected-attitude/v1", "run_id": "7", "fault": 0, "count": 1,
            "lifecycle": {"selector_start_us": 10, "stop_intent_us": 80,
                          "selector_end_us": 90},
            "binding": [0, 0], "health_policy_configured": False,
            "health_qualified": False, "failover_qualified": False,
            "positive_admission": False, "eligible_for_control": False,
            "actuator_publications": False,
            "records": [{"sequence": 1, "observed_us": 60, "sample_us": 30,
                         "estimator_publication_us": 40, "selector_publication_us": 50,
                         "selected_instance": 0, "source_instance": 0,
                         "source_quaternion_bits": list(quaternion),
                         "selected_quaternion_bits": list(quaternion),
                         "source_delta_quaternion_bits": list(quaternion),
                         "selected_delta_quaternion_bits": list(quaternion),
                         "source_reset_counter": 0, "selected_reset_counter": 0,
                         "publication_succeeded": True}],
        }
        self.rows = [(5, "S", (1,)), (6, "L", (1,))]
        self.state = {
            "schema": "p2i-recorded-system-state/v1", "fault": 0,
            "first_imu_source_us": 10, "last_imu_source_us": 4796010,
            "step_us": 4000, "counts": {"total": 2, "status": 1, "land": 1},
            "published_count": 2, "actuator_publications": False,
            "events": [
                {"sequence": 1, "kind": "vehicle_status", "source_us": 5,
                 "publication_us": 100},
                {"sequence": 2, "kind": "vehicle_land_detected", "source_us": 6,
                 "publication_us": 101},
            ],
        }

    def selected(self, trace=None, selector=None):
        return checker.selected_trace(trace or self.trace, "7", "normal", selector or self.selector)

    def test_accepts_bit_exact_selected_attitude(self):
        result = self.selected()
        self.assertEqual(result["quaternion_payloads_preserved"], 1)

    def test_rejects_mutated_quaternion(self):
        trace = deepcopy(self.trace)
        trace["records"][0]["selected_quaternion_bits"][0] = 0
        with self.assertRaisesRegex(ValueError, "mutated attitude payload"):
            self.selected(trace)

    def test_rejects_mutated_reset_delta(self):
        trace = deepcopy(self.trace)
        trace["records"][0]["selected_delta_quaternion_bits"][1] = 1065353216
        with self.assertRaisesRegex(ValueError, "mutated attitude payload"):
            self.selected(trace)

    def test_rejects_nonfinite_quaternion(self):
        trace = deepcopy(self.trace)
        trace["records"][0]["source_quaternion_bits"][0] = 0x7FC00000
        with self.assertRaisesRegex(ValueError, "nonfinite"):
            self.selected(trace)

    def test_rejects_reset_counter_mismatch(self):
        trace = deepcopy(self.trace)
        trace["records"][0]["selected_reset_counter"] = 1
        with self.assertRaisesRegex(ValueError, "selected-attitude/inherited"):
            self.selected(trace)

    def test_rejects_invented_authority(self):
        trace = deepcopy(self.trace)
        trace["positive_admission"] = True
        with self.assertRaisesRegex(ValueError, "invented selector authority"):
            self.selected(trace)

    def test_rejects_actuator_publication(self):
        trace = deepcopy(self.trace)
        trace["actuator_publications"] = True
        with self.assertRaisesRegex(ValueError, "authority/binding"):
            self.selected(trace)

    def test_rejects_unbound_selector_timestamp(self):
        trace = deepcopy(self.trace)
        trace["records"][0]["selector_publication_us"] = 95
        with self.assertRaisesRegex(ValueError, "selected-output chronology"):
            self.selected(trace)

    def test_accepts_complete_recorded_state_trace(self):
        result = checker.system_state_trace(self.state, self.rows)
        self.assertEqual(result["events"], 2)

    def test_rejects_recorded_state_source_mismatch(self):
        state = deepcopy(self.state)
        state["events"][1]["source_us"] = 7
        with self.assertRaisesRegex(ValueError, "event binding"):
            checker.system_state_trace(state, self.rows)

    def test_rejects_recorded_state_actuator_publication(self):
        state = deepcopy(self.state)
        state["actuator_publications"] = True
        with self.assertRaisesRegex(ValueError, "replay fault/authority"):
            checker.system_state_trace(state, self.rows)


if __name__ == "__main__":
    unittest.main()
