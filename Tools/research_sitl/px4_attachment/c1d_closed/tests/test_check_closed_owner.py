"""Synthetic checker guardrails only; these fixtures are NOT native PX4 evidence.

Fixture reports use the oracle's model, so these tests exercise corruption
rejection, not independent C++/Python agreement. Actual exports supply that gate.
"""
import copy
import importlib.util
import hashlib
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("closed_checker", Path(__file__).resolve().parents[1] / "check_closed_owner.py")
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)
IDS = dict(zip(checker.BINDINGS.split(), map(str, range(1, 7))))


def fixture():
    replay = checker.ClosedReplay(IDS)
    source = checker.empty_source()
    records = []
    for index, (stamp, nav) in enumerate(((100, 2), (200, 4), (300, 4))):
        state = {"arming_state": 1, "nav_state": nav}
        before = copy.deepcopy(source)
        generation = 1 if index == 0 else 2
        observation = {"kind": ("Initial", "Transition", "Sample")[index],
                       "before": state if index == 0 else copy.deepcopy(source["state"]),
                       "after": state, "source_time_us": str(stamp)}
        source = {"initialized": True, "state": state, "sequence": str(index + 1),
                  "transition_epoch": str(generation), "physical_generation": str(generation),
                  "committed_time_us": str(stamp), "checked_time_us": str(stamp)}
        sample = checker.physical(str(generation), str(stamp), IDS["controller_instance_id"], True)
        records.append({"observation": observation, "before": before, "after": copy.deepcopy(source),
                        "adapter_input": sample, "result": replay.turn(sample, generation)})
    stop = 2000200
    source["checked_time_us"] = str(stop)
    sample = checker.physical("2", str(stop), IDS["controller_instance_id"], True)
    closure = {"fault": "None", "attempted": {"kind": "Stop", "before": source["state"], "after": source["state"],
                                               "source_time_us": str(stop)}, "retained_source": source,
               "adapter_input": sample, "result": replay.turn(sample, 2, True)}
    return {"schema": "research-c1d-native-owner/v1", "header": {
        "profile_id": checker.PROFILE, "execution_origin": checker.ORIGIN,
        "configuration": checker.closed_configuration(IDS), "normal_record_capacity": 256,
        "clock_source": "px4_hrt_absolute_time_native_posix", "owner_size_bytes": "934496",
        "observation_window_us": "2000000"}, "records": records, "closure": closure,
        "summary": {"state": "Stopped", "fault": "None", "source": source, "record_count": 3, "has_closure": True},
        "lifecycle": {"bootstrap_count": 1, "claim_count": 1, "normal_source_done": True, "retirement": "Retired",
                      "commander_stop_result": 0, "bridge_fault": "None", "stack_measured": True,
                      "stack_size_bytes": "86728", "stack_guard_bytes": "4096", "stack_marker_offset_bytes": "60000",
                      "export_after_retirement": True},
        "footer": {"normal_record_count": 3, "adapter_report_count": 6, "last_report_sequence": "6"}}


class CheckerTests(unittest.TestCase):
    def reject(self, data):
        with self.assertRaises(checker.EvidenceError):
            checker.check_trace(data, IDS)

    def test_synthetic_structure_and_explicit_counts(self):
        result = checker.check_trace(fixture(), IDS)
        self.assertEqual((result["source_record_count"], result["transition_count"], result["adapter_report_count"]), (3, 1, 6))
        self.assertEqual(result["authority"], "none")

    def test_duplicate_truncated_nonfinite_and_trailing_json(self):
        for text in ('{"a":1,"a":2}', '{', '[NaN]', '[Infinity]', '{} {}'):
            with self.subTest(text=text), self.assertRaises(checker.EvidenceError):
                checker.decode(text)

    def test_typed_uint64(self):
        for value in (True, 1, 1.0, "01", "-1", "+1", "", str(1 << 64)):
            with self.subTest(value=value), self.assertRaises(checker.EvidenceError):
                checker.u64(value, "test")

    def test_missing_unknown_fields_and_origin(self):
        for key in fixture():
            data = fixture()
            del data[key]
            self.reject(data)
        for origin in ("SyntheticOffline", "SyntheticNativeContract", "native", None):
            data = fixture()
            data["header"]["execution_origin"] = origin
            self.reject(data)
        data = fixture()
        data["records"][0]["result"]["extra"] = 0
        self.reject(data)

    def test_deleted_duplicated_reordered_and_tail(self):
        for indices in ((0, 2), (0, 1, 1, 2), (0, 2, 1), (0, 1)):
            data = fixture()
            data["records"] = [data["records"][i] for i in indices]
            data["footer"]["normal_record_count"] = len(indices)
            self.reject(data)

    def test_false_authority_or_readiness(self):
        for key in ("clock_mapping_valid", "transport_connected", "required_state_valid"):
            data = fixture()
            data["records"][0]["adapter_input"]["request_guard_context"][key] = True
            self.reject(data)
        for key in ("operator_attached", "has_last_accepted_sequence", "last_accepted_authenticated", "latched"):
            data = fixture()
            data["records"][0]["result"]["snapshot"][key] = True
            self.reject(data)

    def test_result_and_unused_slot_corruption(self):
        for index in range(4):
            data = fixture()
            data["records"][0]["result"]["records"][index]["eligible_for_control"] = True
            self.reject(data)
        data = fixture()
        data["records"][0]["result"]["records"][1]["common_decision"]["rejection_count"] = "99"
        self.reject(data)

    def test_unchanged_assignment_checked_floor(self):
        data = fixture()
        data["records"][2]["before"]["checked_time_us"] = "250"
        checker.check_trace(data, IDS)
        for stamp in ("0", "199", "301"):
            data["records"][2]["before"]["checked_time_us"] = stamp
            self.reject(data)

    def test_source_generation_time_and_arming(self):
        for path, value in (("physical_generation", "3"), ("sequence", "0"), ("transition_epoch", "0")):
            data = fixture()
            data["records"][1]["after"][path] = value
            self.reject(data)
        for stamp in ("0", "99"):
            data = fixture()
            data["records"][1]["observation"]["source_time_us"] = stamp
            self.reject(data)
        data = fixture()
        data["records"][1]["observation"]["after"]["arming_state"] = 2
        self.reject(data)

    def test_incomplete_or_forced_lifetime(self):
        for key, value in (("bootstrap_count", 2), ("claim_count", 2), ("normal_source_done", False),
                           ("retirement", "Abandoned"), ("commander_stop_result", -1), ("bridge_fault", "ClaimRejected"),
                           ("stack_measured", False), ("stack_marker_offset_bytes", "49151"), ("export_after_retirement", False)):
            data = fixture()
            data["lifecycle"][key] = value
            self.reject(data)

    def test_closure_summary_and_footer(self):
        data = fixture()
        data["lifecycle"].update(stack_size_bytes="65536", stack_guard_bytes="32768", stack_marker_offset_bytes="49152")
        self.reject(data)
        for key, value in (("fault", "TimeRegressed"), ("attempted", None), ("adapter_input", None)):
            data = fixture()
            data["closure"][key] = value
            self.reject(data)
        data = fixture()
        data["summary"]["record_count"] = 2
        self.reject(data)
        data = fixture()
        data["footer"]["last_report_sequence"] = "5"
        self.reject(data)

    def test_advertised_only_topics(self):
        lines = ["C1D_SINK_FAILURES_REJECTED", "C1D_TOPICS_BEGIN", "TOPIC NAME INST #SUB #Q SIZE PATH",
                 "C1D_TOPICS_END", "C1D_STARTUP_COMPLETE"]
        self.assertEqual(checker.check_topics("\n".join(lines).encode()), [])
        for row in ("actuator_motors 0 0 1 64 /obj", "research_state_receipt 0 0 1 64 /obj", "garbage"):
            with self.subTest(row=row), self.assertRaises(checker.EvidenceError):
                checker.check_topics("\n".join(lines[:3] + [row] + lines[3:]).encode())


class RunBindingTests(unittest.TestCase):
    """Synthetic wrapper corruption tests, not a claimed PX4 process run."""
    def setUp(self):
        self.area = tempfile.TemporaryDirectory(prefix="c1d_checker_")
        self.addCleanup(self.area.cleanup)
        self.runtime = Path(self.area.name) / "run-1"
        self.runtime.mkdir()
        startup = self.runtime / "startup.sh"
        startup.write_bytes((Path(__file__).resolve().parents[1] / "startup.sh").read_bytes())
        self.trace = fixture()
        self.run = {"schema": "research-c1d-run/v1", "command": [str(self.runtime.parent / "build/bin/px4"),
                    "-i", "123", "-w", str(self.runtime), "-s", str(startup)], "instance": 123,
                    "runtime": str(self.runtime), "bindings": IDS.copy(), "scope": "closed_diagnostic_no_ingress_no_output",
                    "status": "captured_unscored", "start_monotonic_ns": 1, "end_monotonic_ns": 3000000000,
                    "pid": 123, "forced_shutdown": False, "exit_code": 0}
        self.log = "C1D_SINK_FAILURES_REJECTED\nC1D_TOPICS_BEGIN\nTOPIC NAME INST #SUB #Q SIZE PATH\nC1D_TOPICS_END\nC1D_STARTUP_COMPLETE\n"
        self.save()

    def save(self):
        raw = json.dumps(self.trace)
        (self.runtime / "trace.json").write_text(raw)
        (self.runtime / "trace-repeat.json").write_text(raw)
        (self.runtime / "stdout.log").write_text(self.log)
        for name, key in (("startup.sh", "startup_sha256"), ("trace.json", "trace_sha256"),
                          ("trace-repeat.json", "repeat_trace_sha256"), ("stdout.log", "stdout_sha256")):
            self.run[key] = hashlib.sha256((self.runtime / name).read_bytes()).hexdigest()
        (self.runtime / "run.json").write_text(json.dumps(self.run))

    def reject(self):
        with self.assertRaises(checker.EvidenceError):
            checker.check_run(self.runtime, IDS)

    def test_synthetic_wrapper(self):
        self.assertEqual(checker.check_run(self.runtime, IDS)["result"], "PASS")

    def test_forced_failed_or_unbound_run(self):
        original = copy.deepcopy(self.run)
        for key, value in (("forced_shutdown", True), ("exit_code", 1), ("pid", 0), ("end_monotonic_ns", 500),
                           ("status", "incomplete"), ("command", [])):
            self.run = copy.deepcopy(original)
            self.run[key] = value
            self.save()
            self.reject()

    def test_rehashed_semantic_corruption(self):
        self.trace["records"][0]["result"]["records"][1]["eligible_for_control"] = True
        self.save()
        self.reject()

    def test_startup_and_topic_changes_even_when_rehashed(self):
        startup = self.runtime / "startup.sh"
        original = startup.read_bytes()
        startup.write_bytes(original + b"\n")
        self.save()
        self.reject()
        startup.write_bytes(original)
        self.log = self.log.replace("C1D_TOPICS_END", "actuator_motors 0 0 1 64 /obj\nC1D_TOPICS_END")
        self.save()
        self.reject()

    def test_stale_hash_and_different_repeat(self):
        path = self.runtime / "trace-repeat.json"
        path.write_text("{}")
        self.reject()
        self.run["repeat_trace_sha256"] = hashlib.sha256(path.read_bytes()).hexdigest()
        (self.runtime / "run.json").write_text(json.dumps(self.run))
        self.reject()


if __name__ == "__main__":
    unittest.main()
