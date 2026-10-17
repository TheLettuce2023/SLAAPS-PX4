#!/usr/bin/env python3
"""Finite E01-E04 and artifact/incompleteness rejection tests.

With --executable/--build-dir/--source-root, the positive baseline is obtained
once from the real campaign through the independent runner/checker. Negative
documents are then rehashed so semantic failures cannot pass on hash mismatch
alone. Without those options, standard-library unit tests use synthetic files;
that mode claims no execution of the C++ owner or its exporter.
"""

import argparse
import copy
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

import check_campaign as checker

INTEGRATION = None


def encode(document):
    return (json.dumps(document, sort_keys=True, separators=(",", ":"), allow_nan=False) + "\n").encode("utf-8")


def save_campaign(root, campaign, manifest, raw=None):
    data = encode(campaign) if raw is None else raw
    (root / "campaign.json").write_bytes(data)
    manifest["campaign"]["sha256"] = hashlib.sha256(data).hexdigest()


def synthetic_bundle(root):
    """Artifacts for checker unit tests only; never executable provenance."""
    source = root / "source"
    artifacts = root / "artifacts"
    artifacts.mkdir()
    for name in checker.REQUIRED_SOURCES:
        path = source / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(("synthetic unittest source fixture: " + name + "\n").encode())
    (artifacts / "diagnostic_campaign").write_bytes(b"synthetic executable hash fixture; never executed\n")
    sources = [{"path": name, "sha256": checker.sha256_file(source / name)} for name in checker.source_paths(source)]
    manifest = {"schema": checker.MANIFEST_SCHEMA, "profile_id": checker.PROFILE, "sources": sources,
        "executable": {"path": "diagnostic_campaign", "sha256": checker.sha256_file(artifacts / "diagnostic_campaign")},
        "campaign": {"path": "campaign.json", "sha256": "0" * 64}, "build": {"configuration": "Debug",
        "compiler": "synthetic unit-test fixture; not compiler execution evidence", "defines": list(checker.DEFINES)}, "complete": True}
    bindings = {name: manifest[name] for name in ("sources", "executable", "build")}
    campaign = checker.reference_campaign(bindings)
    save_campaign(artifacts, campaign, manifest)
    return source, artifacts, campaign, manifest


def set_path(document, path, value):
    target = document
    for key in path[:-1]:
        target = target[key]
    target[path[-1]] = value


def delete_path(document, path):
    target = document
    for key in path[:-1]:
        target = target[key]
    del target[path[-1]]


def result_path(call, *tail):
    return ("trace", "calls", call - 1, "adapter_result", *tail)


def report_path(call, record=0, *tail):
    return result_path(call, "records", record, *tail)


class CampaignMutations(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="c1b-checker-tests-")
        cls.addClassCleanup(cls.temporary.cleanup)
        root = Path(cls.temporary.name)
        if INTEGRATION:
            cls.source = INTEGRATION.source_root.resolve()
            cls.artifacts = root / "real-campaign"
            command = [sys.executable, "-B", str(Path(__file__).with_name("run_campaign.py")),
                "--executable", str(INTEGRATION.executable.resolve()), "--build-dir", str(INTEGRATION.build_dir.resolve()),
                "--source-root", str(cls.source), "--output-dir", str(cls.artifacts)]
            completed = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=90, check=False)
            if completed.returncode != 0:
                raise RuntimeError("real baseline campaign failed: " + completed.stdout + completed.stderr)
            cls.baseline = checker.read_json(cls.artifacts / "campaign.json")
            cls.manifest_baseline = checker.read_json(cls.artifacts / "manifest.json")
        else:
            cls.source, cls.artifacts, cls.baseline, cls.manifest_baseline = synthetic_bundle(root)

    def setUp(self):
        self.campaign = copy.deepcopy(self.baseline)
        self.manifest = copy.deepcopy(self.manifest_baseline)
        save_campaign(self.artifacts, self.campaign, self.manifest)

    def reject(self, mutate):
        changed = copy.deepcopy(self.campaign)
        mutate(changed)
        manifest = copy.deepcopy(self.manifest)
        save_campaign(self.artifacts, changed, manifest)
        with self.assertRaises(checker.CampaignError):
            checker.check_campaign(changed, manifest, self.artifacts, self.source)

    def test_positive_complete_profile_and_arithmetic(self):
        summary = checker.check_campaign(self.campaign, self.manifest, self.artifacts, self.source)
        self.assertEqual(summary["validated_request_count"], 1)
        self.assertEqual(summary["report_count"], 19)
        self.assertEqual(summary["scope"], "finite_owner_reference_only")
        calls = self.campaign["trace"]["calls"]
        admission = calls[8]["adapter_result"]["snapshot"]["admission"]
        self.assertEqual(int(admission["earliest_acknowledgement_time_us"]), 8960 + 1000 - 2)
        self.assertEqual(int(admission["latest_acknowledgement_time_us"]), 8960 + 1000 + 2)
        decision = calls[10]["adapter_result"]["records"][0]["common_decision"]
        self.assertEqual(int(decision["conservative_age_us"]), 10010 - (9000 + 1000 - 2))
        regression = calls[14]["adapter_result"]
        self.assertEqual(10011 - (8999 + 1000 - 2), 14)
        self.assertFalse(regression["records"][0]["common_decision"]["conservative_age_valid"])
        self.assertEqual(regression["operation_record_index"], 0)
        self.assertEqual([record["kind"] for record in regression["records"]], ["Operation", "SelectorBarrier"])

    def test_E01_domain_eligibility_history_and_actual_mode(self):
        variants = [
            ("remove_domain", report_path(11, 0, "domain"), None, True),
            ("change_domain", report_path(11, 0, "domain"), "Flight", False),
            ("eligible", report_path(11, 0, "eligible_for_control"), True, False),
            ("Flight_history", report_path(11, 0, "snapshot", "has_last_accepted_sequence"), True, False),
            ("actual_mode", report_path(11, 0, "physical", "request_guard_context", "mode_state"), "Selected", False),
        ]
        for name, path, value, delete in variants:
            with self.subTest(case=name):
                self.reject(lambda document: delete_path(document, path) if delete else set_path(document, path, value))

    def test_E02_order_presence_truncation_and_immutable_snapshots(self):
        def remove(document):
            result = document["trace"]["calls"][14]["adapter_result"]
            result["records"].pop()
            result["record_count"] -= 1
        def reorder(document):
            document["trace"]["calls"][14]["adapter_result"]["records"].reverse()
        def duplicate(document):
            result = document["trace"]["calls"][14]["adapter_result"]
            result["records"].append(copy.deepcopy(result["records"][-1]))
            result["record_count"] += 1
        def replace_history(document):
            result = document["trace"]["calls"][14]["adapter_result"]
            result["records"][0]["snapshot"] = copy.deepcopy(result["snapshot"])
        variants = [
            ("remove_report", remove), ("reorder_reports", reorder), ("duplicate_report", duplicate),
            ("truncate_input", lambda doc: delete_path(doc, ("trace", "calls", 10, "input", "packet", "wire", "producer_monotonic_us"))),
            ("truncate_output", lambda doc: delete_path(doc, result_path(11, "snapshot"))),
            ("substitute_final_for_earlier", replace_history),
        ]
        for name, mutate in variants:
            with self.subTest(case=name):
                self.reject(mutate)

    def test_E03_index_counters_identity_ACK_and_false_success(self):
        variants = [
            ("wrong_operation_index", result_path(15, "operation_record_index"), 1),
            ("wrong_barrier_delta", report_path(15, 1, "transition_epoch"), "5"),
            ("replay_identity", report_path(11, 0, "identity", "fixture_run_id"), "16"),
            ("stale_ACK", ("trace", "calls", 8, "input", "packet", "acknowledged_status_sequence"), "4"),
            ("poll_false_positive", result_path(10, "code"), "RequestValidatedDisarmed"),
            ("admin_false_positive", result_path(7, "code"), "RequestValidatedDisarmed"),
        ]
        for name, path, value in variants:
            with self.subTest(case=name):
                self.reject(lambda doc: set_path(doc, path, value))

    def test_E04_unknown_types_ranges_and_provenance(self):
        variants = [
            ("unknown_version", ("schema",), "research-c1b-offline/v2"),
            ("unknown_key", ("header", "operationally_approved"), False),
            ("unknown_enum", report_path(11, 0, "physical", "request_guard_context", "arm_state"), "ImaginaryArm"),
            ("float_for_u64", ("trace", "calls", 10, "input", "packet", "wire", "sequence"), 1.0),
            ("u64_overflow", ("header", "identity", "owner_lifetime_id"), str(1 << 64)),
            ("false_measured_provenance", ("header", "provenance"), "measured_live"),
            ("unknown_profile", ("header", "profile_id"), "arbitrary/v1"),
            ("float_for_count", ("trace", "end", "call_count"), 20.0),
            ("bool_for_count", result_path(11, "record_count"), True),
            ("negative_u64", ("header", "identity", "fixture_run_id"), "-17"),
            ("int64_overflow", ("trace", "calls", 1, "input", "receipt", "sample", "map", "validator_minus_producer_us"), str(1 << 63)),
            ("int64_underflow", ("trace", "calls", 1, "input", "receipt", "sample", "map", "validator_minus_producer_us"), str(-(1 << 63) - 1)),
            ("float_string", ("header", "identity", "owner_lifetime_id"), "13.0"),
            ("unknown_cause_type", report_path(11, 0, "cause", "type"), "UnknownDecisionDomain"),
            ("unknown_owner_outcome", ("trace", "calls", 3, "owner_outcome"), "ProbablyCompleted"),
            ("derived_measured_claim", ("trace", "calls", 3, "owner_derived_turn", "provenance"), "measured"),
        ]
        for name, path, value in variants:
            with self.subTest(case=name):
                self.reject(lambda doc: set_path(doc, path, value))
        for text in ("013", "+13", " 13", "13 ", "1e1", "-0", "\u0661\u0663", "9" * 100):
            with self.subTest(noncanonical=text):
                self.reject(lambda doc: set_path(doc, ("header", "identity", "owner_lifetime_id"), text))

    def test_E04_duplicate_keys_and_nonfinite_JSON(self):
        valid = encode(self.campaign)
        variants = [
            b'{"schema":"' + checker.SCHEMA.encode() + b'",' + valid[1:],
            valid.replace(b'"operator_instance":"19"', b'"operator_instance":"19","operator_instance":"19"', 1),
            valid.replace(b'"call_count":20', b'"call_count":NaN', 1),
            valid.replace(b'"call_count":20', b'"call_count":Infinity', 1),
            valid[:-20],
        ]
        for index, raw in enumerate(variants):
            with self.subTest(raw_variant=index):
                manifest = copy.deepcopy(self.manifest)
                save_campaign(self.artifacts, self.campaign, manifest, raw)
                with self.assertRaises(checker.CampaignError):
                    checker.check_campaign(self.campaign, manifest, self.artifacts, self.source)

    def test_full_call_removal_cannot_be_hidden_by_rewritten_footer(self):
        def remove(document):
            document["trace"]["calls"].pop(11)  # Remove the no-demand Access call.
            for index, call in enumerate(document["trace"]["calls"], 1):
                call["call_id"] = str(index)
            document["trace"]["end"]["call_count"] = len(document["trace"]["calls"])
            document["trace"]["end"]["report_count"] -= 1
        self.reject(remove)

    def test_full_fields_and_sentinels_are_binding(self):
        variants = [
            (report_path(11, 0, "common_decision", "conservative_age_us"), "11"),
            (result_path(9, "snapshot", "admission", "latest_acknowledgement_time_us"), "9963"),
            (report_path(15, 0, "common_decision", "conservative_age_valid"), True),
            (("trace", "calls", 19, "outcome", "value", "request_after", "sequence"), "778"),
            (("trace", "calls", 19, "outcome", "value", "adapter_after", "snapshot", "state_generation"), "702"),
            (("trace", "calls", 19, "outcome", "value", "request_bytes_unchanged"), False),
            (("trace", "calls", 19, "outcome", "value", "adapter_before", "reports", 1, "evidence", "captured"), True),
            (("trace", "calls", 3, "owner_submission", "expected_view", "can_enable"), False),
            (("trace", "calls", 6, "owner_derived_turn", "operation", "value", "grant", "event", "event_id"), "3"),
        ]
        for path, value in variants:
            with self.subTest(path=path):
                self.reject(lambda doc: set_path(doc, path, value))

    def test_C08_missing_and_incomplete_delivery_never_count_as_campaign(self):
        self.reject(lambda doc: delete_path(doc, ("trace", "end")))
        self.reject(lambda doc: doc.update(schema="research-c1b-offline/sink-test/v1"))
        self.reject(lambda doc: doc.clear())
        manifest = copy.deepcopy(self.manifest)
        manifest["complete"] = False
        with self.assertRaises(checker.CampaignError):
            checker.check_campaign(self.campaign, manifest, self.artifacts, self.source)


class ArtifactMutations(unittest.TestCase):
    def test_missing_mismatched_or_unbound_artifacts(self):
        # Every filesystem mutation is confined to disposable synthetic fixtures,
        # including when the semantic suite runs against a real repository.
        for variant in ("executable_missing", "executable_hash", "campaign_missing", "campaign_hash", "source_missing",
                        "source_hash", "registry_missing", "registry_duplicate", "registry_foreign", "manifest_unknown_key",
                        "manifest_missing_hash", "path_escape", "header_unbound_hash", "unknown_build", "missing_define"):
            with self.subTest(case=variant), tempfile.TemporaryDirectory(prefix="c1b-artifact-negative-") as temporary:
                source, root, campaign, manifest = synthetic_bundle(Path(temporary))
                if variant == "executable_missing":
                    (root / "diagnostic_campaign").unlink()
                elif variant == "executable_hash":
                    (root / "diagnostic_campaign").write_bytes(b"changed executable")
                elif variant == "campaign_missing":
                    (root / "campaign.json").unlink()
                elif variant == "campaign_hash":
                    (root / "campaign.json").write_bytes(b"{}")
                elif variant == "source_missing":
                    (source / checker.REQUIRED_SOURCES[0]).unlink()
                elif variant == "source_hash":
                    (source / checker.REQUIRED_SOURCES[0]).write_bytes(b"changed source")
                elif variant == "registry_missing":
                    manifest["sources"].pop()
                elif variant == "registry_duplicate":
                    manifest["sources"][1] = copy.deepcopy(manifest["sources"][0])
                elif variant == "registry_foreign":
                    manifest["sources"][0]["path"] = "unexpected.cpp"
                elif variant == "manifest_unknown_key":
                    manifest["all_tests_passed"] = True
                elif variant == "manifest_missing_hash":
                    del manifest["executable"]["sha256"]
                elif variant == "path_escape":
                    manifest["executable"]["path"] = "../diagnostic_campaign"
                elif variant == "header_unbound_hash":
                    campaign["header"]["artifacts"]["executable"]["sha256"] = "0" * 64
                    save_campaign(root, campaign, manifest)
                elif variant == "unknown_build":
                    manifest["build"]["configuration"] = "Flight"
                elif variant == "missing_define":
                    manifest["build"]["defines"].pop()
                with self.assertRaises(checker.CampaignError):
                    checker.check_campaign(campaign, manifest, root, source)

    def test_supplied_object_must_equal_hashed_campaign_bytes(self):
        with tempfile.TemporaryDirectory(prefix="c1b-bound-input-") as temporary:
            source, root, campaign, manifest = synthetic_bundle(Path(temporary))
            campaign["trace"]["end"]["call_count"] = 19
            with self.assertRaises(checker.CampaignError):
                checker.check_campaign(campaign, manifest, root, source)


def main():
    global INTEGRATION
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path)
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--source-root", type=Path)
    args, unittest_args = parser.parse_known_args()
    supplied = (args.executable is not None, args.build_dir is not None, args.source_root is not None)
    if any(supplied) and not all(supplied):
        parser.error("--executable, --build-dir and --source-root must be supplied together")
    INTEGRATION = args if all(supplied) else None
    unittest.main(argv=[sys.argv[0], *unittest_args])


if __name__ == "__main__":
    main()
