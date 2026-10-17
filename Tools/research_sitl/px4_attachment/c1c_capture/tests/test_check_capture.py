#!/usr/bin/env python3
"""Synthetic independent-oracle mutations; optionally validate retained real runs."""
from __future__ import annotations

import argparse
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location("capture_checker", HERE.parent / "check_capture.py")
checker = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(checker)


def fixtures(runtime):
    """Hand-written synthetic expectations, never exported producer goldens."""
    ids = {"producer_lifetime_id": "11", "observer_lifetime_id": "13", "clock_epoch": "17"}
    initial = {"arming_state": 1, "nav_state": 0}
    changed = {"arming_state": 1, "nav_state": 1}
    rows = [{"type": "bootstrap", "schema": "research-c1c-capture/v1", **ids,
             "receipt_queue_depth": 32, "collector_capacity": 4096, "window_us": "2000000",
             "clock_source": "px4_hrt_absolute_time_native_posix"}]
    for sequence, epoch, stamp, kind, before, after in (
            (1, 1, 1000, "Initial", initial, initial),
            (2, 1, 1000100, "Sample", initial, initial),
            (3, 2, 1500100, "Transition", initial, changed),
            (4, 2, 2001100, "Sample", changed, changed)):
        rows.append({"type": "receipt", "observer_lifetime_id": "13", "receive_time_us": str(stamp + 10),
                     "receipt": {"producer_lifetime_id": "11", "clock_epoch": "17", "sequence": str(sequence),
                                 "transition_epoch": str(epoch), "producer_time_us": str(stamp), "kind": kind,
                                 "before": copy.deepcopy(before), "after": copy.deepcopy(after)}})
    rows += [{"type": "summary", "receive_time_us": "2001220", "summary": {
        "identities": ids, "attempted_sequence": "4", "emitted_sequence": "4", "transition_epoch": "2",
        "producer_time_us": "2001100", "final_time_us": "2001200", "retained_state": changed,
        "actual_state": copy.deepcopy(changed), "has_state": True, "finished": True, "first_fault": "None",
        "emitted_state": copy.deepcopy(changed), "emitted_transition_epoch": "2", "emitted_producer_time_us": "2001100"}},
        {"type": "completion", "collector_fault": "None", "receipt_count": 4,
         "summary_count": 1, "final_drain_complete": True}]
    raw = ("C1C_TOPICS_BEGIN\nTOPIC NAME INST #SUB #Q SIZE PATH\n"
           "vehicle_status 0 1 1 100 /obj/vehicle_status0\n"
           "actuator_armed 0 1 1 40 /obj/actuator_armed0\n"
           "research_state_receipt 0 1 32 80 /obj/research_state_receipt0\n"
           "research_state_summary 0 1 2 120 /obj/research_state_summary0\n"
           "C1C_TOPICS_END\nC1C_STARTUP_COMPLETE\n").encode()
    record = {"schema": "research-c1c-run/v1", "command": [str(runtime.parent / "build/bin/px4"), "-i", "10001", "-w", str(runtime),
               "-s", str(runtime / "startup.sh")], "instance": 10001, "runtime": str(runtime), "pid": 10003,
              **ids, "start_monotonic_ns": 500000, "end_monotonic_ns": 3000000000, "exit_code": 0,
              "forced_shutdown": False, "stdout_sha256": "", "capture_sha256": "", "startup_sha256": "",
              "admission": "not_instantiated", "research_network": "not_instantiated", "status": "captured_unscored"}
    return rows, raw, record


def encoded(rows):
    return ("\n".join(json.dumps(row, separators=(",", ":")) for row in rows) + "\n").encode()


class CaptureMutations(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="c1c_checker_synthetic_")
        self.addCleanup(self.temporary.cleanup)
        self.runtime = Path(self.temporary.name).resolve()
        self.rows, self.raw, self.record = fixtures(self.runtime)

    def retain(self, rows=None, raw=None, record=None, capture=None):
        rows = self.rows if rows is None else rows
        raw = self.raw if raw is None else raw
        record = copy.deepcopy(self.record if record is None else record)
        capture = encoded(rows) if capture is None else capture
        # Semantic mutations deliberately update BOTH digests before replay.
        record["capture_sha256"] = hashlib.sha256(capture).hexdigest()
        record["stdout_sha256"] = hashlib.sha256(raw).hexdigest()
        startup = (HERE.parent / "startup.sh").read_bytes()
        record["startup_sha256"] = hashlib.sha256(startup).hexdigest()
        (self.runtime / "startup.sh").write_bytes(startup)
        (self.runtime / "capture.jsonl").write_bytes(capture)
        (self.runtime / "stdout.log").write_bytes(raw)
        (self.runtime / "run.json").write_text(json.dumps(record), encoding="utf-8")

    def reject(self, mutate):
        rows = copy.deepcopy(self.rows)
        mutate(rows)
        self.retain(rows=rows)
        with self.assertRaises(ValueError):
            checker.check_run(self.runtime)

    def test_independent_arithmetic_and_full_replay(self):
        self.retain()
        result = checker.check_run(self.runtime)
        self.assertEqual(result["receipt_count"], 4)
        self.assertEqual(result["transition_count"], 1)
        self.assertEqual(result["source_span_us"], 2000200)
        self.assertEqual((result["min_receipt_age_us"], result["max_receipt_age_us"]), (10, 10))
        self.assertEqual(result["summary_age_us"], 20)
        self.assertEqual(result["admission"], "not_instantiated")

    def test_no_transition_trace_is_not_claimed_as_live_transition(self):
        rows = copy.deepcopy(self.rows)
        for row in rows[1:-2]:
            row["receipt"]["kind"] = "Initial" if row["receipt"]["sequence"] == "1" else "Sample"
            row["receipt"]["transition_epoch"] = "1"
            row["receipt"]["before"]["nav_state"] = row["receipt"]["after"]["nav_state"] = 0
        for key in ("retained_state", "actual_state", "emitted_state"):
            rows[-2]["summary"][key]["nav_state"] = 0
        rows[-2]["summary"]["transition_epoch"] = rows[-2]["summary"]["emitted_transition_epoch"] = "1"
        self.retain(rows)
        self.assertEqual(checker.check_run(self.runtime)["transition_count"], 0)

    def test_missing_initial(self):
        def mutate(rows):
            rows.pop(1)
            rows[-1]["receipt_count"] = 3
        self.reject(mutate)

    def test_duplicate_initial(self):
        self.reject(lambda rows: rows[2]["receipt"].update(kind="Initial"))

    def test_missing_summary(self):
        self.reject(lambda rows: rows.pop(-2))

    def test_duplicate_summary(self):
        self.reject(lambda rows: rows.insert(-1, copy.deepcopy(rows[-2])))

    def test_lost_interior_rewritten_footer(self):
        def mutate(rows):
            rows.pop(2)
            rows[-1]["receipt_count"] = 3
        self.reject(mutate)

    def test_lost_final_tail_rewritten_footer(self):
        def mutate(rows):
            rows.pop(-3)
            rows[-1]["receipt_count"] = 3
        self.reject(mutate)

    def test_duplicate_reordered_replayed(self):
        for source, target in ((1, 2), (3, 2), (1, 4)):
            with self.subTest(source=source, target=target):
                self.reject(lambda rows: rows.__setitem__(target, copy.deepcopy(rows[source])))

    def test_every_lifetime_binding(self):
        changes = [lambda rows: rows[0].update(producer_lifetime_id="12"),
                   lambda rows: rows[2].update(observer_lifetime_id="14"),
                   lambda rows: rows[2]["receipt"].update(producer_lifetime_id="12"),
                   lambda rows: rows[2]["receipt"].update(clock_epoch="18"),
                   lambda rows: rows[-2]["summary"]["identities"].update(observer_lifetime_id="14")]
        for mutate in changes:
            with self.subTest(mutation=changes.index(mutate)):
                self.reject(mutate)

    def test_before_epoch_and_omitted_transition(self):
        changes = [lambda rows: rows[3]["receipt"]["before"].update(nav_state=2),
                   lambda rows: rows[3]["receipt"].update(transition_epoch="1"),
                   lambda rows: rows[4]["receipt"].update(transition_epoch="3"),
                   lambda rows: rows[3]["receipt"].update(kind="Sample"),
                   lambda rows: rows[2]["receipt"].update(kind="Transition")]
        for mutate in changes:
            with self.subTest(mutation=changes.index(mutate)):
                self.reject(mutate)

    def test_every_final_summary_fact(self):
        changes = {"attempted_sequence": "5", "emitted_sequence": "3", "transition_epoch": "3",
                   "emitted_transition_epoch": "1", "producer_time_us": "2001099", "emitted_producer_time_us": "2001099",
                   "has_state": False, "finished": False, "first_fault": "PublishFailed", "final_time_us": "2001099"}
        for key, value in changes.items():
            with self.subTest(field=key):
                self.reject(lambda rows: rows[-2]["summary"].__setitem__(key, value))
        for key in ("retained_state", "actual_state", "emitted_state"):
            with self.subTest(field=key):
                self.reject(lambda rows: rows[-2]["summary"][key].update(nav_state=0))

    def test_fabricated_healthy_summary_does_not_hide_failed_delivery(self):
        def mutate(rows):
            rows[-2]["summary"].update(first_fault="None", attempted_sequence="5", emitted_sequence="4")
        self.reject(mutate)

    def test_source_receive_time_regressions_and_future_source(self):
        changes = [lambda rows: rows[2]["receipt"].update(producer_time_us="999"),
                   lambda rows: rows[2].update(receive_time_us="1009"),
                   lambda rows: rows[2].update(receive_time_us="1000099"),
                   lambda rows: rows[-2].update(receive_time_us="2001199"),
                   lambda rows: rows[1]["receipt"].update(producer_time_us="0"),
                   lambda rows: rows[1]["receipt"].update(producer_time_us="499")]
        for mutate in changes:
            with self.subTest(mutation=changes.index(mutate)):
                self.reject(mutate)

    def test_receive_regression_even_when_every_source_precedes_receive(self):
        self.reject(lambda rows: rows[1].update(receive_time_us="1500000"))

    def test_armed_actual_evidence_and_reserved_navigation(self):
        self.reject(lambda rows: rows[3]["receipt"]["after"].update(arming_state=2))
        self.reject(lambda rows: rows[-2]["summary"]["actual_state"].update(arming_state=2))
        self.reject(lambda rows: rows[3]["receipt"]["after"].update(nav_state=9))

    def test_strict_types_keys_enums_decimals(self):
        changes = [lambda rows: rows[2]["receipt"].update(sequence=2),
                   lambda rows: rows[2]["receipt"].update(sequence="02"),
                   lambda rows: rows[2]["receipt"].update(sequence="18446744073709551616"),
                   lambda rows: rows[2]["receipt"].update(kind="Unknown"),
                   lambda rows: rows[2]["receipt"].update(extra=True),
                   lambda rows: rows[2]["receipt"].pop("before"),
                   lambda rows: rows[2]["receipt"]["before"].update(arming_state=True),
                   lambda rows: rows[-2]["summary"].update(finished=1),
                   lambda rows: rows[-1].update(receipt_count=4.0),
                   lambda rows: rows[0].update(schema="research-c1c-capture/v2")]
        for mutate in changes:
            with self.subTest(mutation=changes.index(mutate)):
                self.reject(mutate)

    def test_raw_nonfinite_duplicate_keys_truncation(self):
        original = encoded(self.rows)
        payloads = [original.replace(b'"receipt_count":4', b'"receipt_count":NaN'),
                    original.replace(b'"receipt_count":4', b'"receipt_count":Infinity'),
                    original.replace(b'"receipt_count":4', b'"receipt_count":4,"receipt_count":4'),
                    original[:-8], original[:-1], original + b"\n"]
        for index, payload in enumerate(payloads):
            with self.subTest(mutation=index):
                self.retain(capture=payload)
                with self.assertRaises(ValueError):
                    checker.check_run(self.runtime)

    def test_incomplete_collector(self):
        for key, value in (("collector_fault", "BufferOverflow"), ("summary_count", 0),
                           ("final_drain_complete", False), ("receipt_count", 3)):
            with self.subTest(field=key):
                self.reject(lambda rows: rows[-1].__setitem__(key, value))
        self.reject(lambda rows: rows.pop())

    def test_stdout_marker_and_topic_corruptions(self):
        payloads = [self.raw.replace(b"C1C_STARTUP_COMPLETE\n", b""),
                    self.raw + b"C1C_STARTUP_COMPLETE\n",
                    self.raw.replace(b"vehicle_status 0", b"unapproved_module_topic 0"),
                    self.raw.replace(b"vehicle_status 0", b"vehicle_status 1"),
                    self.raw.replace(b"vehicle_status 0", b"actuator_outputs 0"),
                    self.raw.replace(b"C1C_TOPICS_END", b"vehicle_status 0 1 1 100 /obj/vehicle_status0\nC1C_TOPICS_END")]
        for index, payload in enumerate(payloads):
            with self.subTest(mutation=index):
                self.retain(raw=payload)
                with self.assertRaises(ValueError):
                    checker.check_run(self.runtime)

    def test_run_failures(self):
        for key, value in (("forced_shutdown", True), ("exit_code", 1), ("status", "incomplete"),
                           ("admission", "enabled"), ("research_network", "enabled"), ("pid", True)):
            with self.subTest(field=key):
                record = copy.deepcopy(self.record)
                record[key] = value
                self.retain(record=record)
                with self.assertRaises(ValueError):
                    checker.check_run(self.runtime)

    def test_capture_topic_queue_depths_match_contract(self):
        for source, altered in ((b"research_state_receipt 0 1 32", b"research_state_receipt 0 1 1"),
                                (b"research_state_summary 0 1 2", b"research_state_summary 0 1 1")):
            with self.subTest(topic=source):
                self.retain(raw=self.raw.replace(source, altered))
                with self.assertRaisesRegex(ValueError, "queue depth"):
                    checker.check_run(self.runtime)

    def test_post_stop_standard_state_topics_are_optional(self):
        raw = b"\n".join(line for line in self.raw.splitlines()
                          if not line.startswith((b"vehicle_status ", b"actuator_armed "))) + b"\n"
        self.retain(raw=raw)
        result = checker.check_run(self.runtime)
        self.assertEqual(result["result"], "PASS")
        self.assertEqual(result["receipt_count"], 4)
        self.assertEqual(result["transition_count"], 1)
        self.assertEqual(result["source_span_us"], 2000200)
        self.assertEqual(result["advertised_topics"], ["research_state_receipt", "research_state_summary"])
        self.assertEqual(result["topic_scope"], "post_stop_advertised_only_snapshot")

    def test_each_capture_topic_remains_required_after_stop(self):
        for topic in (b"research_state_receipt ", b"research_state_summary "):
            with self.subTest(topic=topic):
                raw = b"\n".join(line for line in self.raw.splitlines() if not line.startswith(topic)) + b"\n"
                self.retain(raw=raw)
                with self.assertRaisesRegex(ValueError, "Missing required capture topics"):
                    checker.check_run(self.runtime)

    def test_stale_artifact_hash_and_missing_file(self):
        for name in ("capture.jsonl", "stdout.log"):
            with self.subTest(file=name):
                self.retain()
                with (self.runtime / name).open("ab") as stream:
                    stream.write(b"tamper")
                with self.assertRaises(ValueError):
                    checker.check_run(self.runtime)
                self.retain()
                (self.runtime / name).unlink()
                with self.assertRaises(OSError):
                    checker.check_run(self.runtime)

    def test_command_executable_and_startup_boundaries(self):
        for index in (0, 6):
            with self.subTest(argument=index):
                record = copy.deepcopy(self.record)
                record["command"][index] = str(self.runtime / "foreign-command")
                self.retain(record=record)
                with self.assertRaises(ValueError):
                    checker.check_run(self.runtime)

    def test_missing_staged_startup(self):
        self.retain()
        (self.runtime / "startup.sh").unlink()
        with self.assertRaisesRegex(ValueError, "startup"):
            checker.check_run(self.runtime)

    def test_altered_staged_startup_with_and_without_rehash(self):
        for rehash in (False, True):
            with self.subTest(rehashed=rehash):
                self.retain()
                startup = self.runtime / "startup.sh"
                startup.write_bytes(startup.read_bytes() + b"\n# synthetic tamper\n")
                if rehash:
                    record = json.loads((self.runtime / "run.json").read_text())
                    record["startup_sha256"] = hashlib.sha256(startup.read_bytes()).hexdigest()
                    (self.runtime / "run.json").write_text(json.dumps(record), encoding="utf-8")
                with self.assertRaisesRegex(ValueError, "reviewed source" if rehash else "startup_sha256"):
                    checker.check_run(self.runtime)

    def test_staged_startup_symlink_rejected_even_with_reviewed_bytes(self):
        self.retain()
        startup = self.runtime / "startup.sh"
        startup.unlink()
        startup.symlink_to(HERE.parent / "startup.sh")
        with self.assertRaisesRegex(ValueError, "regular owned file"):
            checker.check_run(self.runtime)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime", action="append", type=Path, default=[], help="Retained actual run, read-only")
    arguments, remaining = parser.parse_known_args()
    for runtime in arguments.runtime:
        print(json.dumps(checker.check_run(runtime), sort_keys=True))
    unittest.main(argv=[__file__, *remaining])
