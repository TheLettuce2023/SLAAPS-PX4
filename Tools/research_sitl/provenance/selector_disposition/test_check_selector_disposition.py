#!/usr/bin/env python3
"""Focused corruption tests for the independent P2H disposition checker."""

from copy import deepcopy
import importlib.util
from pathlib import Path
import unittest


HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("checker", HERE / "check_selector_disposition.py")
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)


class SelectorDispositionCheckerTest(unittest.TestCase):
    def setUp(self):
        self.selector = {
            "attitudes": [],
            "local_positions": [{"sample_us": 50, "source_acceleration_bits": [1, 2, 3]}],
        }
        self.trace = {
            "schema": "p2h-selector-disposition/v1", "run_id": "7", "fault": 0,
            "counts": {"ekf_attitude": 1, "attitude": 1, "local_position": 1},
            "lifecycle": {"selector_start_us": 10, "estimator_start_us": 20,
                          "stop_intent_us": 90, "selector_end_us": 100,
                          "estimator_end_us": 110},
            "binding": [0, 0], "health_policy_configured": False,
            "health_qualified": False, "failover_qualified": False,
            "positive_admission": False, "eligible_for_control": False,
            "ekf_attitudes": [{"sequence": 1, "observed_us": 60, "sample_us": 40,
                               "estimator_instance": 0, "attitude_valid": False,
                               "publication_attempted": False,
                               "publication_succeeded": False}],
            "attitude_dispositions": [{"sequence": 1, "observed_us": 70,
                                       "sample_us": 0, "source_publication_us": 0,
                                       "previous_sample_us": 0, "age_us": 0,
                                       "selected_instance": 0, "source_instance": 0,
                                       "candidate_available": False, "monotonic": False,
                                       "fresh": False, "publish_requested": False}],
            "local_position_dispositions": [{"sequence": 1, "observed_us": 80,
                                             "sample_us": 50, "source_publication_us": 60,
                                             "previous_sample_us": 0, "age_us": 5000,
                                             "source_acceleration_bits": [1, 2, 3],
                                             "selected_instance": 0, "source_instance": 0,
                                             "candidate_available": True, "monotonic": True,
                                             "fresh": True, "publish_requested": True}],
        }

    def check(self, trace=None, selector=None):
        return checker.disposition_trace(
            trace or self.trace, "7", "normal", selector or self.selector)

    def test_accepts_upstream_invalid_attitude_and_published_acceleration(self):
        result = self.check()
        self.assertEqual(result["ekf_attitude_valid"], 0)
        self.assertEqual(result["attitude_candidates"], 0)
        self.assertEqual(result["local_position_publications"], 1)

    def test_rejects_invented_authority(self):
        trace = deepcopy(self.trace)
        trace["positive_admission"] = True
        with self.assertRaisesRegex(ValueError, "invented selector authority"):
            self.check(trace)

    def test_rejects_gate_reason_without_candidate(self):
        trace = deepcopy(self.trace)
        trace["attitude_dispositions"][0]["fresh"] = True
        with self.assertRaisesRegex(ValueError, "invented absent attitude candidate"):
            self.check(trace)

    def test_rejects_publication_from_invalid_attitude(self):
        trace = deepcopy(self.trace)
        trace["ekf_attitudes"][0]["publication_attempted"] = True
        with self.assertRaisesRegex(ValueError, "EKF attitude-attempt contract"):
            self.check(trace)

    def test_rejects_unobserved_valid_attitude(self):
        trace = deepcopy(self.trace)
        trace["ekf_attitudes"][0]["attitude_valid"] = True
        with self.assertRaisesRegex(ValueError, "unexpected attitude-valid source"):
            self.check(trace)

    def test_rejects_local_freshness_mismatch(self):
        trace = deepcopy(self.trace)
        trace["local_position_dispositions"][0]["fresh"] = False
        with self.assertRaisesRegex(ValueError, "local-position gate disposition"):
            self.check(trace)

    def test_rejects_local_publication_mismatch(self):
        trace = deepcopy(self.trace)
        trace["local_position_dispositions"][0]["publish_requested"] = False
        with self.assertRaisesRegex(ValueError, "local-position gate disposition"):
            self.check(trace)

    def test_rejects_acceleration_mismatch(self):
        selector = deepcopy(self.selector)
        selector["local_positions"][0]["source_acceleration_bits"][2] = 4
        with self.assertRaisesRegex(ValueError, "selected acceleration disposition mismatch"):
            self.check(selector=selector)

    def test_rejects_wrong_lifecycle_fault(self):
        trace = deepcopy(self.trace)
        trace["fault"] = 4
        with self.assertRaisesRegex(ValueError, "wrong disposition lifecycle fault"):
            self.check(trace)


if __name__ == "__main__":
    unittest.main()
