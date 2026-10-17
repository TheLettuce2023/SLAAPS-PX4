#!/usr/bin/env python3
"""Focused corruption tests for the independent P2F selected-state checker."""

from copy import deepcopy
import importlib.util
from pathlib import Path
import unittest


HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("checker", HERE / "check_selected_state.py")
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)


class SelectedStateCheckerTest(unittest.TestCase):
    def setUp(self):
        self.committed = {"records": [
            {"sample_us": 100, "update_committed": False, "control_flags": 128, "fault_flags": 0},
            {"sample_us": 200, "update_committed": True, "control_flags": 256, "fault_flags": 0},
        ]}
        self.trace = {
            "schema": "p2f-selected-state/v1", "run_id": "7", "fault": 0, "count": 2,
            "start_us": 90, "stop_intent_us": 240, "end_us": 250, "binding": [0, 0, 0, 0],
            "health_policy_configured": False, "health_qualified": False,
            "positive_admission": False, "eligible_for_control": False,
            "records": [
                {"sequence": 1, "source_us": 110, "sample_us": 100, "update_committed": False,
                 "instance": 0, "control_flags": 128, "fault_flags": 0,
                 "published_control_flags": 0, "published_fault_flags": 0,
                 "control_status_changes": 0, "fault_status_changes": 0, "status": None},
                {"sequence": 2, "source_us": 230, "sample_us": 200, "update_committed": True,
                 "instance": 0, "control_flags": 256, "fault_flags": 0,
                 "published_control_flags": 256, "published_fault_flags": 0,
                 "control_status_changes": 1, "fault_status_changes": 0,
                 "status": {"sample_us": 190, "publication_us": 220, "control_flags": 256,
                            "fault_flags": 0, "control_status_changes": 1, "fault_status_changes": 0,
                            "instance": 0, "publication_instance": 0}},
            ]}

    def check(self, trace=None, committed=None):
        return checker.selected_trace(trace or self.trace, "7", "normal", committed or self.committed)

    def test_accepts_bound_trace(self):
        result = self.check()
        self.assertEqual(result["status_publications"], 1)
        self.assertFalse(result["health_qualified"])

    def test_rejects_invented_authority(self):
        trace = deepcopy(self.trace)
        trace["health_policy_configured"] = True
        with self.assertRaisesRegex(ValueError, "invented selected-state authority"):
            self.check(trace)

    def test_rejects_instance_rebinding(self):
        trace = deepcopy(self.trace)
        trace["records"][1]["instance"] = 1
        with self.assertRaisesRegex(ValueError, "sequence/instance"):
            self.check(trace)

    def test_rejects_boolean_binding_alias(self):
        trace = deepcopy(self.trace)
        trace["binding"][0] = False
        with self.assertRaisesRegex(ValueError, "single-instance publisher binding"):
            self.check(trace)

    def test_rejects_counter_gap(self):
        trace = deepcopy(self.trace)
        trace["records"][1]["control_status_changes"] = 2
        trace["records"][1]["status"]["control_status_changes"] = 2
        with self.assertRaisesRegex(ValueError, "change-counter continuity"):
            self.check(trace)

    def test_rejects_missing_change_publication(self):
        trace = deepcopy(self.trace)
        trace["records"][1]["status"] = None
        with self.assertRaisesRegex(ValueError, "missing status publication"):
            self.check(trace)

    def test_rejects_status_cache_mismatch(self):
        trace = deepcopy(self.trace)
        trace["records"][1]["published_control_flags"] = 128
        with self.assertRaisesRegex(ValueError, "status cache differs"):
            self.check(trace)

    def test_rejects_p2e_mismatch(self):
        committed = deepcopy(self.committed)
        committed["records"][1]["sample_us"] += 1
        with self.assertRaisesRegex(ValueError, "selected-state/P2E payload differs"):
            self.check(committed=committed)


if __name__ == "__main__":
    unittest.main()
