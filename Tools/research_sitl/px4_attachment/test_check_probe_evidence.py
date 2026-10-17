"""Synthetic oracle tests only: these fixtures are not measured PX4 evidence."""
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import tempfile
import unittest

from check_probe_evidence import check_campaign, check_run


def fixture_rows():
    # Native no-lockstep diagnostic bracket; this does not qualify a clock map.
    rows = []
    for index in range(3):
        rows.append({"type": "clock", "host_before_ns": 10_000_000_000 + index * 1_000_000_000,
                     "host_after_ns": 10_000_000_100 + index * 1_000_000_000,
                     "px4_hrt_us": 10_000_000 + index * 1_000_000})
        if index:
            base = {"timestamp_us": 9_999_000 + index * 1_000_000, "generation": index,
                    "generation_delta": 0 if index == 1 else 1, "fresh": True}
            rows.append({"type": "vehicle_status", **base, "arming_state": 1})
            rows.append({"type": "actuator_armed", **base, "armed": False})
    rows.append({"type": "summary", "complete": True, "pass": True,
                 "clock_samples": 3, "vehicle_status_fresh": 2, "actuator_armed_fresh": 2,
                 "all_disarmed": True, "clocks_valid": True, "observations_valid": True,
                 "host_elapsed_ns": 2_000_000_100})
    return rows


def write_fixture(runtime, rows=None, marker=True, record_changes=None, extra=""):
    runtime.mkdir(exist_ok=True)
    rows = fixture_rows() if rows is None else rows
    # Spell the wire schema independently; do not reuse the checker's mapping.
    renames = {"host_before_ns": "b", "px4_hrt_us": "h", "host_after_ns": "a",
               "timestamp_us": "us", "generation": "g", "generation_delta": "d",
               "fresh": "f", "arming_state": "s", "armed": "a",
               "clock_samples": "c", "vehicle_status_fresh": "v",
               "actuator_armed_fresh": "a", "host_elapsed_ns": "ns",
               "all_disarmed": "disarmed", "clocks_valid": "clocks",
               "observations_valid": "obs"}
    count_keys = {"clock_samples", "vehicle_status_fresh", "actuator_armed_fresh", "host_elapsed_ns"}
    wire = []
    for row in rows:
        kind = row.get("type")
        if kind == "summary":
            wire.append({"t": "counts", **{renames[key]: value for key, value in row.items() if key in count_keys}})
        compact = {renames.get(key, key): value for key, value in row.items()
                   if key != "type" and not (kind == "summary" and key in count_keys)}
        if kind is not None:
            compact["t"] = {"vehicle_status": "status", "actuator_armed": "armed"}.get(kind, kind)
        wire.append(compact)
    text = "\n".join("INFO [research_probe] C1A " + json.dumps(row) for row in wire) + "\n"
    text += ("C1A_TOPICS_BEGIN\nTOPIC NAME INST #SUB #Q SIZE PATH\n"
             "vehicle_status 0 1 1 100 /obj/vehicle_status0\n"
             "actuator_armed 0 1 1 20 /obj/actuator_armed0\nC1A_TOPICS_END\n")
    if marker:
        text += "C1A_STARTUP_COMPLETE\n"
    text += extra
    raw = text.encode()
    (runtime / "stdout.log").write_bytes(raw)
    record = {"runtime": str(runtime.resolve()), "pid": 123, "instance": 12345,
              "admission": "not_instantiated", "research_network": "not_instantiated",
              "start_monotonic_ns": 9_000_000_000, "end_monotonic_ns": 13_000_000_000,
              "exit_code": 0, "forced_shutdown": False,
              "stdout_sha256": hashlib.sha256(raw).hexdigest()}
    record.update(record_changes or {})
    (runtime / "run.json").write_text(json.dumps(record))
    return record


class EvidenceTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.runtime = self.root / "run-1"

    def test_synthetic_valid_native_clock_diagnostic(self):
        write_fixture(self.runtime)
        result = check_run(self.runtime)
        self.assertEqual(result["result"], "PASS")
        self.assertEqual(result["admission"], "not_instantiated")
        self.assertEqual(result["clock_map"], "not_qualified")

    def test_missing_and_duplicate_evidence(self):
        original = fixture_rows()
        variants = [[], original[:-1], original + [original[-1]],
                    [row for row in original if row["type"] != "actuator_armed"],
                    [original[0]] + original[2:], original[:3] + original[2:]]
        for rows in variants:
            with self.subTest(rows=rows):
                write_fixture(self.runtime, rows)
                with self.assertRaises(ValueError):
                    check_run(self.runtime)
        write_fixture(self.runtime, marker=False)
        with self.assertRaises(ValueError):
            check_run(self.runtime)
        write_fixture(self.runtime, extra="C1A_STARTUP_COMPLETE\n")
        with self.assertRaises(ValueError):
            check_run(self.runtime)

    def test_corrupt_measurement_or_summary(self):
        mutations = [
            (2, "arming_state", 2), (2, "arming_state", True),
            (3, "armed", True), (3, "armed", 0),
            (2, "timestamp_us", 0), (5, "timestamp_us", 10_999_000),
            (2, "timestamp_us", 12_000_000), (2, "fresh", False),
            (5, "generation_delta", 0), (5, "generation", 1),
            (1, "host_before_ns", 1), (1, "host_after_ns", 2),
            (4, "px4_hrt_us", 1), (1, "host_before_ns", True),
            (7, "clock_samples", 4), (7, "vehicle_status_fresh", 3),
            (7, "host_elapsed_ns", 2_000_000_101), (7, "pass", False),
            (7, "complete", 1), (7, "observations_valid", False),
            (0, "extra", 1),
        ]
        for index, key, value in mutations:
            with self.subTest(index=index, key=key, value=value):
                rows = fixture_rows()
                rows[index][key] = value
                write_fixture(self.runtime, rows)
                with self.assertRaises(ValueError):
                    check_run(self.runtime)

    def test_missing_or_illtyped_fields(self):
        for index, row in enumerate(fixture_rows()):
            for name in row:
                with self.subTest(index=index, missing=name):
                    rows = fixture_rows()
                    del rows[index][name]
                    write_fixture(self.runtime, rows)
                    with self.assertRaises((ValueError, KeyError)):
                        check_run(self.runtime)

    def test_native_clock_range_bracket_completion_and_order(self):
        variants = []
        for hrt in (10_999_999, 11_000_001, ((1 << 64) - 1 - 999) // 1000 + 1):
            rows = fixture_rows()
            rows[1]["px4_hrt_us"] = hrt
            variants.append(rows)
        rows = fixture_rows()
        rows[2], rows[3] = rows[3], rows[2]
        variants.append(rows)
        rows = fixture_rows()
        extra = {"type": "clock", "host_before_ns": 12_100_000_000,
                 "host_after_ns": 12_100_000_100, "px4_hrt_us": 12_100_000}
        rows.insert(-1, extra)
        rows[-1]["clock_samples"] = 4
        rows[-1]["host_elapsed_ns"] = 2_100_000_100
        variants.append(rows)
        for rows in variants:
            with self.subTest(rows=rows):
                write_fixture(self.runtime, rows)
                with self.assertRaises(ValueError):
                    check_run(self.runtime)

    def test_run_record_integrity(self):
        variants = [{"stdout_sha256": "0" * 64}, {"exit_code": 1},
                    {"exit_code": False}, {"forced_shutdown": True},
                    {"admission": "rejected"}, {"research_network": "none"},
                    {"start_monotonic_ns": 11_000_000_000},
                    {"end_monotonic_ns": 11_000_000_000}, {"pid": 0},
                    {"runtime": str(self.root)}]
        for change in variants:
            with self.subTest(change=change):
                write_fixture(self.runtime, record_changes=change)
                with self.assertRaises(ValueError):
                    check_run(self.runtime)

    def test_malformed_truncated_and_duplicate_json(self):
        for extra in ('C1A {"type":', 'C1A {"type":"summary","type":"clock"}',
                      'C1A {"type":"unknown"}'):
            with self.subTest(extra=extra):
                write_fixture(self.runtime, extra=extra)
                with self.assertRaises(ValueError):
                    check_run(self.runtime)

    def test_generation_wrap_is_publication_only(self):
        rows = fixture_rows()
        rows[2]["generation"] = (1 << 32) - 1
        rows[5]["generation"] = 0
        write_fixture(self.runtime, rows)
        self.assertEqual(check_run(self.runtime)["transition_history"], "not_proven")

    def test_missing_parameter_diagnostics_rejected(self):
        for text in ("ERROR [parameters] get: param 65535 invalid\n",
                     "ERROR [health_and_arming_checks] wrong type passed to param_get() for param (null)\n"):
            with self.subTest(text=text):
                write_fixture(self.runtime, extra=text)
                with self.assertRaisesRegex(ValueError, "parameter dependency"):
                    check_run(self.runtime)

    def test_topic_snapshot_mutations(self):
        variants = [("C1A_TOPICS_BEGIN\n", ""), ("C1A_TOPICS_END\n", ""),
                    ("TOPIC NAME INST #SUB #Q SIZE PATH", "TOPIC NAME"),
                    ("vehicle_status 0 1 1 100 /obj/vehicle_status0\n", ""),
                    ("C1A_TOPICS_END", "actuator_outputs 0 0 1 20 /obj/actuator_outputs0\nC1A_TOPICS_END"),
                    ("C1A_TOPICS_END", "vehicle_torque_setpoint_virtual_fw 0 0 1 20 /obj/torque0\nC1A_TOPICS_END"),
                    ("C1A_TOPICS_END", "malformed\nC1A_TOPICS_END"),
                    ("C1A_TOPICS_END", "C1A_TOPICS_END\nC1A_TOPICS_END")]
        for old, new in variants:
            with self.subTest(old=old, new=new):
                record = write_fixture(self.runtime)
                log = self.runtime / "stdout.log"
                raw = log.read_text().replace(old, new).encode()
                log.write_bytes(raw)
                record["stdout_sha256"] = hashlib.sha256(raw).hexdigest()
                (self.runtime / "run.json").write_text(json.dumps(record))
                with self.assertRaises(ValueError):
                    check_run(self.runtime)

    def test_two_run_campaign_and_mutations(self):
        first = write_fixture(self.runtime)
        second_rows = fixture_rows()
        for row in second_rows:
            if row["type"] == "clock":
                row["host_before_ns"] += 10_000_000_000
                row["host_after_ns"] += 10_000_000_000
                row["px4_hrt_us"] += 10_000_000
            elif row["type"] in {"vehicle_status", "actuator_armed"}:
                row["timestamp_us"] += 10_000_000
        second = write_fixture(self.root / "run-2", second_rows, record_changes={
            "instance": 12346, "start_monotonic_ns": 19_000_000_000,
            "end_monotonic_ns": 23_000_000_000})
        # Same PID is possible after process exit; disjoint lifetimes matter.
        manifest = {"schema": "research-c1a/v1", "scope": "passive_no_output_no_admission",
                    "status": "built_runs_unscored", "changed_source_inputs": [],
                    "runs": [first, second]}
        path = self.root / "manifest.json"
        path.write_text(json.dumps(manifest))
        self.assertEqual(check_campaign(path)["result"], "PASS")
        for name, value in (("runs", [first]), ("runs", [first, first]),
                            ("status", "fail"), ("changed_source_inputs", ["changed"])):
            bad = deepcopy(manifest)
            bad[name] = value
            path.write_text(json.dumps(bad))
            with self.assertRaises(ValueError):
                check_campaign(path)


if __name__ == "__main__":
    unittest.main()
