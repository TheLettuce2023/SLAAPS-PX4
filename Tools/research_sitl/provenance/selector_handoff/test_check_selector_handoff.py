#!/usr/bin/env python3

from copy import deepcopy
import importlib.util
from pathlib import Path
import struct
import unittest


HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("checker", HERE / "check_selector_handoff.py")
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)


def fbits(value):
    return struct.unpack("<I", struct.pack("<f", value))[0]


class SelectorHandoffCheckerTest(unittest.TestCase):
    def setUp(self):
        self.sensor = {"accelerometer_device_id": 7, "gyroscope_device_id": 7}
        quat = [fbits(1.0), fbits(0.0), fbits(0.0), fbits(0.0)]
        accel = [fbits(0.1), fbits(0.2), fbits(0.3)]
        self.trace = {
            "schema": "p2j-selector-handoff/v1", "run_id": "9", "fault": 0,
            "counts": {"selection": 2, "status": 2, "attitude": 2,
                       "local_position": 2},
            "lifecycle": {"selector_start_us": 10, "stop_intent_us": 80,
                          "selector_end_us": 90, "estimator_start_us": [11, 12],
                          "estimator_end_us": [30, 100]},
            "bindings": [[0, 0, 0, 0, 0], [1, 1, 1, 1, 1]],
            "health_policy_configured": False, "health_qualified": False,
            "failover_qualified": False, "complete_mutation_coverage": False,
            "deployment_sensors_qualified": False, "positive_admission": False,
            "eligible_for_control": False, "actuator_publications": False,
            "selections": [
                {"sequence": 1, "observed_us": 21, "selection_us": 20,
                 "instance_changed_count": 1, "accelerometer_device_id": 7,
                 "gyroscope_device_id": 7, "old_instance": 255, "new_instance": 0,
                 "available_instances": 2, "requested_instance": 255,
                 "old_healthy": False, "old_timeout": False, "new_healthy": False,
                 "new_timeout": False, "publication_succeeded": True},
                {"sequence": 2, "observed_us": 41, "selection_us": 40,
                 "instance_changed_count": 2, "accelerometer_device_id": 7,
                 "gyroscope_device_id": 7, "old_instance": 0, "new_instance": 1,
                 "available_instances": 2, "requested_instance": 255,
                 "old_healthy": False, "old_timeout": True, "new_healthy": True,
                 "new_timeout": False, "publication_succeeded": True},
            ],
            "statuses": [
                {"sequence": 1, "observed_us": 26, "publication_us": 25,
                 "last_instance_change_us": 20, "instance_changed_count": 1,
                 "accelerometer_device_id": 7, "gyroscope_device_id": 7,
                 "combined_test_ratio_bits": [fbits(1.0), fbits(1.0)],
                 "primary_instance": 0, "available_instances": 2,
                 "healthy": [True, True], "publication_succeeded": True},
                {"sequence": 2, "observed_us": 46, "publication_us": 45,
                 "last_instance_change_us": 40, "instance_changed_count": 2,
                 "accelerometer_device_id": 7, "gyroscope_device_id": 7,
                 "combined_test_ratio_bits": [fbits(1.0), fbits(1.0)],
                 "primary_instance": 1, "available_instances": 2,
                 "healthy": [False, True], "publication_succeeded": True},
            ],
            "attitudes": [
                {"sequence": 1, "observed_us": 25, "sample_us": 21,
                 "estimator_publication_us": 22, "selector_publication_us": 23,
                 "source_quaternion_bits": list(quat), "selected_quaternion_bits": list(quat),
                 "source_delta_quaternion_bits": list(quat),
                 "selected_delta_quaternion_bits": list(quat), "selected_instance": 0,
                 "source_instance": 0, "source_reset_counter": 0,
                 "selected_reset_counter": 0, "publication_succeeded": True},
                {"sequence": 2, "observed_us": 45, "sample_us": 41,
                 "estimator_publication_us": 42, "selector_publication_us": 43,
                 "source_quaternion_bits": list(quat), "selected_quaternion_bits": list(quat),
                 "source_delta_quaternion_bits": list(quat),
                 "selected_delta_quaternion_bits": list(quat), "selected_instance": 1,
                 "source_instance": 1, "source_reset_counter": 0,
                 "selected_reset_counter": 1, "publication_succeeded": True},
            ],
            "local_positions": [
                {"sequence": 1, "observed_us": 25, "sample_us": 21,
                 "estimator_publication_us": 22, "selector_publication_us": 23,
                 "source_acceleration_bits": list(accel), "selected_acceleration_bits": list(accel),
                 "source_reset_counters": [1, 1, 1, 1, 0, 1],
                 "selected_reset_counters": [1, 1, 1, 1, 0, 1],
                 "selected_instance": 0, "source_instance": 0,
                 "publication_succeeded": True},
                {"sequence": 2, "observed_us": 45, "sample_us": 41,
                 "estimator_publication_us": 42, "selector_publication_us": 43,
                 "source_acceleration_bits": list(accel), "selected_acceleration_bits": list(accel),
                 "source_reset_counters": [1, 1, 1, 1, 0, 1],
                 "selected_reset_counters": [2, 2, 2, 2, 1, 2],
                 "selected_instance": 1, "source_instance": 1,
                 "publication_succeeded": True},
            ],
        }

    def check(self, trace=None):
        return checker.handoff_trace(trace or self.trace, "9", "normal", self.sensor)

    def test_accepts_actual_timeout_handoff(self):
        result = self.check()
        self.assertEqual(result["automatic_timeout_handoffs"], 1)

    def test_rejects_invented_authority(self):
        trace = deepcopy(self.trace)
        trace["health_qualified"] = True
        with self.assertRaisesRegex(ValueError, "invented P2J authority"):
            self.check(trace)

    def test_rejects_deployment_sensor_claim(self):
        trace = deepcopy(self.trace)
        trace["deployment_sensors_qualified"] = True
        with self.assertRaisesRegex(ValueError, "invented P2J authority"):
            self.check(trace)

    def test_rejects_requested_switch(self):
        trace = deepcopy(self.trace)
        trace["selections"][1]["requested_instance"] = 1
        with self.assertRaisesRegex(ValueError, "automatic timeout handoff"):
            self.check(trace)

    def test_rejects_switch_without_timeout(self):
        trace = deepcopy(self.trace)
        trace["selections"][1]["old_timeout"] = False
        with self.assertRaisesRegex(ValueError, "automatic timeout handoff"):
            self.check(trace)

    def test_rejects_instance_binding_alias(self):
        trace = deepcopy(self.trace)
        trace["bindings"][1][4] = 0
        with self.assertRaisesRegex(ValueError, "instance binding"):
            self.check(trace)

    def test_rejects_attitude_reset_barrier_loss(self):
        trace = deepcopy(self.trace)
        trace["attitudes"][1]["selected_reset_counter"] = 0
        with self.assertRaisesRegex(ValueError, "attitude handoff reset barrier"):
            self.check(trace)

    def test_rejects_local_reset_barrier_loss(self):
        trace = deepcopy(self.trace)
        trace["local_positions"][1]["selected_reset_counters"][4] = 0
        with self.assertRaisesRegex(ValueError, "local-position handoff reset barrier"):
            self.check(trace)

    def test_rejects_acceleration_mutation(self):
        trace = deepcopy(self.trace)
        trace["local_positions"][1]["selected_acceleration_bits"][0] = fbits(0.4)
        with self.assertRaisesRegex(ValueError, "selected acceleration payload"):
            self.check(trace)

    def test_rejects_missing_post_handoff_attitude(self):
        trace = deepcopy(self.trace)
        trace["attitudes"][1]["selected_instance"] = 0
        trace["attitudes"][1]["source_instance"] = 0
        with self.assertRaisesRegex(ValueError, "attitude handoff coverage"):
            self.check(trace)

    def test_rejects_status_without_pair_health(self):
        trace = deepcopy(self.trace)
        trace["statuses"][0]["healthy"] = [True, False]
        with self.assertRaisesRegex(ValueError, "missing raw-health"):
            self.check(trace)


if __name__ == "__main__":
    unittest.main()
