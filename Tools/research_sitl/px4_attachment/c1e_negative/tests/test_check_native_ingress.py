"""Synthetic checker guardrails only; never native PX4 execution evidence.

The positive fixture uses the checker-side oracle. It verifies corruption
rejection, not independent agreement with C++ export or a source-bound run.
"""

import copy
import importlib.util
import json
from pathlib import Path
import unittest


SPEC = importlib.util.spec_from_file_location(
    "native_ingress_checker", Path(__file__).resolve().parents[1] / "check_native_ingress.py")
checker = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(checker)
old = checker.prior_closed_oracle()
IDS = {"permitted_source": "7", "controller_instance_id": "11",
       "owner_lifetime_id": "13", "native_run_id": "17",
       "producer_lifetime_id": "23", "clock_epoch": "29"}


def candidate():
    return dict(token="1", native_run_id="17", owner_lifetime_id="13",
                controller_instance_id="11", source_id="7", transport_binding_id="19",
                request_sequence="1", expected_transition_epoch="1",
                producer_monotonic_us="1000", producer_expiry_monotonic_us="1800",
                clock_mapping_epoch="0", arrival_native_us="104", signing_timestamp_48="10000100",
                acceleration_x_frd_m_s2=1.5, acceleration_y_frd_m_s2=-2.25,
                acceleration_z_frd_m_s2=0.5, protocol_version=65536, domain=1,
                profile=1, target_system=1, target_component=1, frame=1, quantity=1,
                reference_point=1, flags=0, reserved=[0, 0, 0, 0], channel=1,
                header_system=42, header_component=191, incoming_link_id=7,
                mavlink_sequence=17, payload_length=103, parser_stage=1, frame_crc=123,
                signature=[7] + list((10000100).to_bytes(6, "little")) + [0] * 6,
                peer_port=14600)


def fixture():
    replay = old.ClosedReplay(IDS)
    raw = {"arming_state": 1, "nav_state": 2}
    first_source = {"initialized": True, "state": raw, "sequence": "1",
                    "transition_epoch": "1", "physical_generation": "1",
                    "committed_time_us": "100", "checked_time_us": "100"}
    initial_input = old.physical("1", "100", "11", True)
    initial = {"observation": {"kind": "Initial", "before": raw, "after": raw,
                               "source_time_us": "100"},
               "before": old.empty_source(), "after": copy.deepcopy(first_source),
               "adapter_input": initial_input, "result": replay.turn(initial_input, 1)}
    second_source = copy.deepcopy(first_source)
    second_source.update(sequence="2", committed_time_us="101", checked_time_us="101")
    first_sample_input = old.physical("1", "101", "11", True)
    first_sample = {"observation": {"kind": "Sample", "before": raw, "after": raw,
                                    "source_time_us": "101"},
                    "before": copy.deepcopy(first_source), "after": copy.deepcopy(second_source),
                    "adapter_input": first_sample_input,
                    "result": replay.turn(first_sample_input, 1)}
    third_source = copy.deepcopy(second_source)
    third_source.update(sequence="3", committed_time_us="103", checked_time_us="103")
    second_sample_input = old.physical("1", "103", "11", True)
    second_sample = {"observation": {"kind": "Sample", "before": raw, "after": raw,
                                     "source_time_us": "103"},
                     "before": copy.deepcopy(second_source), "after": copy.deepcopy(third_source),
                     "adapter_input": second_sample_input,
                     "result": replay.turn(second_sample_input, 1)}
    packet = candidate()
    poll_input = old.physical("1", "104", "11", True)
    poll_result = replay.turn(poll_input, 1)
    request_result = checker.request_result(old, replay, poll_input, packet)
    status = checker.expected_status(packet, third_source, 104, 1)
    disposition = {"token": "1", "kind": "Completed", "has_actual_request": True,
                   "has_status": True, "status": status}
    transaction = {"kind": "Completed", "candidate": packet,
                   "source": copy.deepcopy(third_source), "poll_input": poll_input,
                   "poll_result": poll_result, "has_request": True,
                   "request_input": copy.deepcopy(poll_input), "request_result": request_result,
                   "disposition": disposition}
    before_sample = copy.deepcopy(third_source)
    before_sample["checked_time_us"] = "104"
    after_sample = copy.deepcopy(third_source)
    after_sample.update(sequence="4", committed_time_us="105", checked_time_us="105")
    sample_input = old.physical("1", "105", "11", True)
    sample = {"observation": {"kind": "Sample", "before": raw, "after": raw,
                              "source_time_us": "105"},
              "before": before_sample, "after": copy.deepcopy(after_sample),
              "adapter_input": sample_input, "result": replay.turn(sample_input, 1)}
    stop_time = 2000200
    closed_source = copy.deepcopy(after_sample)
    closed_source["checked_time_us"] = str(stop_time)
    stop_input = old.physical("1", str(stop_time), "11", True)
    closure = {"cause": "OwnerStop", "fault": "None",
               "attempted": {"kind": "Stop", "before": raw, "after": raw,
                             "source_time_us": str(stop_time)},
               "retained_source": closed_source, "adapter_input": stop_input,
               "result": replay.turn(stop_input, 1, True)}
    config = old.closed_configuration(IDS)
    del config["clock_epoch"]
    config.update(transport_binding_id="19", source_clock_id="29", channel=1, peer_port=14600)
    worker_summary = {"local_port": 14500, "peer_port": 14600, "channel": 1,
                      "reject_counts": ["0"] * 16, "busy_drop_count": "0", "wrong_peer_count": "0",
                      "arrival_clock_count": "0", "completed_replay_count": "1",
                      "discarded_replay_count": "0", "status_attempt_count": "1",
                      "status_sent_count": "1", "status_failed_count": "0",
                      "last_arrival_native_us": "104", "last_token": "1",
                      "reject_record_count": 0, "delivery_record_count": 1,
                      "last_send_state": "Sent", "retired": True}
    return {"schema": checker.SCHEMA,
            "header": {"profile_id": checker.PROFILE, "execution_origin": checker.ORIGIN,
                       "clock_source": "px4_hrt_absolute_time_native_posix",
                       "configuration": config, "source_capacity": 256,
                       "transaction_capacity": 32, "preauth_reject_capacity": 64,
                       "owner_size_bytes": "2000000", "worker_size_bytes": "4096"},
            "source_records": [initial, first_sample, second_sample, sample],
            "transactions": [transaction],
            "closure": closure,
            "summary": {"state": "Stopped", "fault": "None", "source": closed_source,
                        "source_count": 4, "transaction_count": 1,
                        "last_status_sequence": "1", "has_closure": True,
                        "evidence_complete": True},
            "worker": {"summary": worker_summary, "rejections": [],
                       "deliveries": [{"token": "1", "status_sequence": "1",
                                       "signing_timestamp_48": "10000105",
                                       "disposition": "Completed", "send_state": "Sent",
                                       "signed_size": 157, "replay_committed": True,
                                       "has_status": True}]},
            "lifecycle": {"bootstrap_count": 1, "claim_count": 1,
                          "stack_size_bytes": "86728", "stack_guard_bytes": "4096",
                          "stack_marker_offset_bytes": "60000", "commander_stop_result": "0",
                          "bridge_fault": 0, "source_done": True, "worker_joined": True,
                          "stack_measured": True, "observation_window_us": "2000000",
                          "waited": True, "wait_completed": True},
            "footer": {"source_count": 4, "transaction_count": 1,
                       "adapter_report_count": replay.report_count,
                       "last_adapter_report_sequence": str(replay.report_count),
                       "last_status_sequence": "1", "completed_replay_count": "1",
                       "status_sent_count": "1"}}


class SyntheticCheckerTests(unittest.TestCase):
    def reject(self, data):
        with self.assertRaises(checker.EvidenceError):
            checker.check_archive(data)

    def test_one_completed_negative_transaction(self):
        archive = fixture()
        self.assertEqual(archive["transactions"][0]["poll_result"]["snapshot"]["rejection_count"], "4")
        self.assertEqual(archive["transactions"][0]["request_result"]["snapshot"]["rejection_count"], "5")
        result = checker.check_archive(archive)
        self.assertEqual((result["source_count"], result["transaction_count"]), (4, 1))
        self.assertEqual(result["authority"], "none")

    def test_source_checkpoint_binds_observed_epoch_before_request(self):
        marker = b"C1E_SOURCE_CHECKPOINT epoch=1 arm=1 nav=2 sequence=3 checked_hrt_us=103\n"
        point = checker.check_checkpoint(marker, fixture())
        self.assertEqual(point["epoch"], "1")
        for altered in (marker.replace(b"epoch=1", b"epoch=2"),
                        marker.replace(b"arm=1", b"arm=2"),
                        marker + marker,
                        marker.replace(b"checked_hrt_us=103", b"checked_hrt_us=105")):
            with self.subTest(altered=altered), self.assertRaises(checker.EvidenceError):
                checker.check_checkpoint(altered, fixture())
        self.assertIsNone(checker.read_checkpoint(b"C1E_TOPICS_BEGIN\n"))

    def test_duplicate_truncated_nonfinite_and_noncanonical_scalar(self):
        for raw in ('{"a":1,"a":2}', '{', '[NaN]', '{} {}'):
            with self.subTest(raw=raw), self.assertRaises(checker.EvidenceError):
                checker.decode(raw)
        for value in (True, 1, 1.0, "01", "-1", str(1 << 64)):
            with self.subTest(value=value), self.assertRaises(checker.EvidenceError):
                checker.u64(value, "test")

    def test_candidate_binding_and_shape_corruption(self):
        for key, value in (("native_run_id", "18"), ("transport_binding_id", "20"),
                           ("clock_mapping_epoch", "1"), ("parser_stage", 0),
                           ("payload_length", 102), ("reserved", [1, 0, 0, 0]),
                           ("arrival_native_us", "103"), ("signing_timestamp_48", str(1 << 48))):
            data = fixture()
            data["transactions"][0]["candidate"][key] = value
            self.reject(data)

    def test_full_result_status_and_authority_corruption(self):
        for path, value in (("poll_result", "SelectionRequired"),
                            ("request_result", "ClockMappingInvalid")):
            data = fixture()
            data["transactions"][0][path]["records"][0]["cause"]["value"] = value
            self.reject(data)
        for key, value in (("reason", 1), ("eligible_for_control", 1),
                           ("source_sequence", "99"), ("evaluation_native_us", "100")):
            data = fixture()
            data["transactions"][0]["disposition"]["status"][key] = value
            self.reject(data)
        data = fixture()
        data["transactions"][0]["request_result"]["snapshot"]["operator_attached"] = True
        self.reject(data)

    def test_source_replay_and_closure_corruption(self):
        for key, value in (("sequence", "9"), ("physical_generation", "2"),
                           ("checked_time_us", "99")):
            data = fixture()
            data["source_records"][1]["after"][key] = value
            self.reject(data)
        data = fixture()
        data["source_records"][1]["observation"]["after"]["arming_state"] = 2
        self.reject(data)
        for key, value in (("cause", "TransportFault"), ("fault", "TimeRegressed")):
            data = fixture()
            data["closure"][key] = value
            self.reject(data)

    def test_replay_send_lifecycle_and_footer_corruption(self):
        for key, value in (("replay_committed", False), ("send_state", "SendFailed"),
                           ("status_sequence", "2"), ("signed_size", 0)):
            data = fixture()
            data["worker"]["deliveries"][0][key] = value
            self.reject(data)
        for key, value in (("status_sent_count", "0"), ("status_failed_count", "1"),
                           ("completed_replay_count", "0")):
            data = fixture()
            data["worker"]["summary"][key] = value
            self.reject(data)
        for key, value in (("source_done", False), ("worker_joined", False),
                           ("commander_stop_result", "1"), ("bridge_fault", 1),
                           ("waited", False), ("wait_completed", False),
                           ("observation_window_us", "1")):
            data = fixture()
            data["lifecycle"][key] = value
            self.reject(data)
        data = fixture()
        data["footer"]["adapter_report_count"] -= 1
        self.reject(data)


if __name__ == "__main__":
    unittest.main()
