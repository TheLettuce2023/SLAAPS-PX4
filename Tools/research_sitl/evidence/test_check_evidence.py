import hashlib
import importlib.util
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


HERE = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location("check_evidence", HERE / "check_evidence.py")
CHECKER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CHECKER)


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def write_json(path, value):
    Path(path).write_text(json.dumps(value, sort_keys=True), encoding="utf-8")


class EvidenceCheckerTest(unittest.TestCase):
    def make_package(self, change=None):
        directory = tempfile.TemporaryDirectory()
        root = Path(directory.name)
        (root / "host.py").write_text("fixture host source\n", encoding="utf-8")
        write_json(root / "input.json", {"fixture": "input"})
        input_hash = digest(root / "input.json")
        assertion = {"case_id": "case_a", "input_sha256": input_hash, "value": {"fixture": True}}
        write_json(root / "expected.json", assertion)
        write_json(root / "observed.json", assertion)
        registry = {
            "schema": {"name": "sitl_software_contract_registry", "version": "1.0.0"},
            "fixture_profile": {
                "profile": "software_contract", "non_operational": True,
                "exemptions": ["integrated_sitl", "hardware", "simulator_runner", "ulog", "flight_metrics"],
            },
            "fixture_assertions": [{"id": "fixture_equal", "non_operational": True}],
            "requirements": [],
            "required_cases": [{
                "id": "case_a",
                "criteria": [{
                    "assertion": "fixture_equal", "type": "exact_json_equal",
                    "input_artifact": "input.json", "expected_artifact": "expected.json",
                    "observed_artifact": "observed.json",
                    "input_sha256": digest(root / "input.json"),
                    "expected_sha256": digest(root / "expected.json"),
                }],
            }],
        }
        write_json(root / "requirements.json", registry)
        artifacts = []
        for path, kind in (("input.json", "raw_input"), ("expected.json", "fixture_expected"),
                           ("observed.json", "raw_output")):
            artifacts.append({
                "path": path, "kind": kind, "sha256": digest(root / path),
                "byte_count": (root / path).stat().st_size, "complete": True,
            })
        manifest = {
            "schema": {"name": "sitl_software_contract_evidence", "version": "1.0.0",
                       "sha256": digest(HERE / "manifest.schema.json")},
            "run": {"id": "fixture-run", "profile": "software_contract", "result": "pass"},
            "registries": {"requirements": {"path": "requirements.json", "sha256": digest(root / "requirements.json")}},
            "identity": {
                "schema_sha256": digest(HERE / "manifest.schema.json"),
                "checker_sha256": digest(HERE / "check_evidence.py"),
                "host_sources": [{"path": "host.py", "sha256": digest(root / "host.py"),
                                  "dirty": False, "dirty_files": []}],
            },
            "cases": [{"id": "case_a", "execution": "executed", "claimed_result": "pass"}],
            "artifacts": artifacts,
        }
        if change:
            change(root, manifest, registry)
        write_json(root / "manifest.json", manifest)
        return directory, root, manifest, registry

    def report(self, root, registry):
        try:
            return CHECKER.evaluate(root / "manifest.json", digest(root / "requirements.json"))
        except CHECKER.EvidenceError as error:
            return {"result": "fail", "errors": [str(error)]}

    def test_valid_package_and_schema(self):
        directory, root, manifest, registry = self.make_package()
        self.addCleanup(directory.cleanup)
        self.assertEqual(self.report(root, registry)["result"], "pass")
        try:
            import jsonschema
        except ImportError:
            self.skipTest("optional jsonschema is not installed")
        jsonschema.Draft202012Validator(CHECKER.load_json(HERE / "manifest.schema.json")).validate(
            CHECKER.load_json(root / "manifest.json"))

    def test_cli_json_stdout_and_exit_codes(self):
        directory, root, manifest, registry = self.make_package()
        self.addCleanup(directory.cleanup)
        command = [sys.executable, "-B", str(HERE / "check_evidence.py"), str(root / "manifest.json"),
                   "--registry-sha256", digest(root / "requirements.json")]
        completed = subprocess.run(command, text=True, capture_output=True, check=False)
        self.assertEqual(completed.returncode, CHECKER.PASS_EXIT)
        self.assertEqual(json.loads(completed.stdout)["result"], "pass")
        incomplete = CHECKER.evaluate(root / "manifest.json")
        self.assertEqual(incomplete["result"], "incomplete")

    def test_missing_corrupt_truncated_and_wrong_size_fail(self):
        for defect in ("missing", "corrupt", "truncated", "wrong_size"):
            with self.subTest(defect=defect):
                def change(root, manifest, registry):
                    if defect == "missing":
                        manifest["artifacts"][0]["path"] = "missing.json"
                    elif defect == "corrupt":
                        (root / "observed.json").write_text("{", encoding="utf-8")
                        manifest["artifacts"][2]["sha256"] = digest(root / "observed.json")
                        manifest["artifacts"][2]["byte_count"] = 1
                    elif defect == "truncated":
                        manifest["artifacts"][2]["complete"] = False
                    else:
                        manifest["artifacts"][2]["byte_count"] += 1
                directory, root, manifest, registry = self.make_package(change)
                self.addCleanup(directory.cleanup)
                self.assertEqual(self.report(root, registry)["result"], "fail")

    def test_nonfinite_duplicate_ids_and_changed_hash_fail(self):
        for defect in ("nonfinite", "duplicate", "changed_hash"):
            with self.subTest(defect=defect):
                def change(root, manifest, registry):
                    if defect == "nonfinite":
                        (root / "observed.json").write_text('{"case_id":"case_a","input_sha256":"%s","value":1e999}' %
                                                           digest(root / "input.json"), encoding="utf-8")
                        manifest["artifacts"][2]["sha256"] = digest(root / "observed.json")
                        manifest["artifacts"][2]["byte_count"] = (root / "observed.json").stat().st_size
                    elif defect == "duplicate":
                        manifest["cases"].append(dict(manifest["cases"][0]))
                    else:
                        (root / "host.py").write_text("changed\n", encoding="utf-8")
                directory, root, manifest, registry = self.make_package(change)
                self.addCleanup(directory.cleanup)
                self.assertEqual(self.report(root, registry)["result"], "fail")

    def test_duplicate_json_key_fails(self):
        def change(root, manifest, registry):
            source_hash = digest(root / "input.json")
            (root / "observed.json").write_text(
                '{"case_id":"case_a","case_id":"case_a","input_sha256":"' + source_hash +
                '","value":{"fixture":true}}', encoding="utf-8")
            manifest["artifacts"][2]["sha256"] = digest(root / "observed.json")
            manifest["artifacts"][2]["byte_count"] = (root / "observed.json").stat().st_size
        directory, root, manifest, registry = self.make_package(change)
        self.addCleanup(directory.cleanup)
        self.assertEqual(self.report(root, registry)["result"], "fail")

    def test_unsupported_dirty_registry_mismatch_and_not_run(self):
        def unsupported(root, manifest, registry):
            manifest["run"]["profile"] = "integrated_sitl"
        directory, root, manifest, registry = self.make_package(unsupported)
        self.addCleanup(directory.cleanup)
        self.assertEqual(self.report(root, registry)["result"], "fail")

        def dirty(root, manifest, registry):
            (root / "dirty.py").write_text("dirty fixture\n", encoding="utf-8")
            source = manifest["identity"]["host_sources"][0]
            source["dirty"] = True
            source["dirty_files"] = [{"path": "dirty.py", "sha256": digest(root / "dirty.py")}]
        directory, root, manifest, registry = self.make_package(dirty)
        self.addCleanup(directory.cleanup)
        self.assertEqual(self.report(root, registry)["result"], "pass")
        with self.assertRaises(CHECKER.EvidenceError):
            CHECKER.evaluate(root / "manifest.json", "0" * 64)

        def not_run(root, manifest, registry):
            manifest["run"]["result"] = "incomplete"
            manifest["cases"][0]["execution"] = "not_run"
            manifest["cases"][0]["claimed_result"] = "not_run"
        directory, root, manifest, registry = self.make_package(not_run)
        self.addCleanup(directory.cleanup)
        self.assertEqual(self.report(root, registry)["result"], "incomplete")

    def test_unapproved_requirement_and_fail_precedence(self):
        def change(root, manifest, registry):
            registry["requirements"] = [{"id": "REQ_FIXTURE_LIMIT", "approved": False}]
            registry["required_cases"][0]["criteria"].append({
                "assertion": "REQ_FIXTURE_LIMIT", "type": "exact_json_equal",
                "input_artifact": "input.json", "expected_artifact": "expected.json",
                "observed_artifact": "observed.json",
                "input_sha256": digest(root / "input.json"),
                "expected_sha256": digest(root / "expected.json"),
            })
            manifest["run"]["result"] = "incomplete"
            manifest["cases"][0]["claimed_result"] = "incomplete"
            write_json(root / "requirements.json", registry)
            manifest["registries"]["requirements"]["sha256"] = digest(root / "requirements.json")
        directory, root, manifest, registry = self.make_package(change)
        self.addCleanup(directory.cleanup)
        self.assertEqual(self.report(root, registry)["result"], "incomplete")

        def failure_wins(root, manifest, registry):
            registry["requirements"] = [{"id": "REQ_FIXTURE_LIMIT", "approved": False}]
            registry["required_cases"][0]["criteria"].append({
                "assertion": "REQ_FIXTURE_LIMIT", "type": "exact_json_equal",
                "input_artifact": "input.json", "expected_artifact": "expected.json",
                "observed_artifact": "observed.json",
                "input_sha256": digest(root / "input.json"),
                "expected_sha256": digest(root / "expected.json"),
            })
            observed = CHECKER.load_json(root / "observed.json")
            observed["value"] = {"fixture": False}
            write_json(root / "observed.json", observed)
            manifest["artifacts"][2]["sha256"] = digest(root / "observed.json")
            manifest["artifacts"][2]["byte_count"] = (root / "observed.json").stat().st_size
            manifest["run"]["result"] = "fail"
            manifest["cases"][0]["claimed_result"] = "fail"
            write_json(root / "requirements.json", registry)
            manifest["registries"]["requirements"]["sha256"] = digest(root / "requirements.json")
        directory, root, manifest, registry = self.make_package(failure_wins)
        self.addCleanup(directory.cleanup)
        self.assertEqual(self.report(root, registry)["result"], "fail")

    def test_registry_pin_rejects_rewritten_input_or_expectation(self):
        def rewrite(root, manifest, registry):
            write_json(root / "input.json", {"fixture": "rewritten"})
            input_hash = digest(root / "input.json")
            replacement = {"case_id": "case_a", "input_sha256": input_hash, "value": {"fixture": "rewritten"}}
            write_json(root / "expected.json", replacement)
            write_json(root / "observed.json", replacement)
            for index, path in ((0, "input.json"), (1, "expected.json"), (2, "observed.json")):
                manifest["artifacts"][index]["sha256"] = digest(root / path)
                manifest["artifacts"][index]["byte_count"] = (root / path).stat().st_size
        directory, root, manifest, registry = self.make_package(rewrite)
        self.addCleanup(directory.cleanup)
        self.assertEqual(self.report(root, registry)["result"], "fail")


    def test_cli_all_result_codes_and_usage(self):
        directory, root, manifest, registry = self.make_package()
        self.addCleanup(directory.cleanup)
        base = [sys.executable, "-B", str(HERE / "check_evidence.py")]
        cases = [
            ([], CHECKER.USAGE_EXIT, "fail"),
            ([str(root / "missing.json")], CHECKER.FAIL_EXIT, "fail"),
            ([str(root / "manifest.json")], CHECKER.INCOMPLETE_EXIT, "incomplete"),
            ([str(root / "manifest.json"), "--registry-sha256", "0" * 64], CHECKER.FAIL_EXIT, "fail"),
        ]
        for arguments, code, result in cases:
            with self.subTest(arguments=arguments):
                completed = subprocess.run(base + arguments, text=True, capture_output=True, timeout=10)
                self.assertEqual(completed.returncode, code, completed.stderr)
                self.assertEqual(json.loads(completed.stdout)["result"], result)
                self.assertEqual(completed.stderr, "")

    def test_missing_pin_does_not_hide_registry_or_artifact_corruption(self):
        for defect in ("registry", "observed"):
            with self.subTest(defect=defect):
                directory, root, manifest, registry = self.make_package()
                self.addCleanup(directory.cleanup)
                if defect == "registry":
                    manifest["registries"]["requirements"] = []
                else:
                    (root / "observed.json").write_text("{", encoding="utf-8")
                    manifest["artifacts"][2]["sha256"] = digest(root / "observed.json")
                    manifest["artifacts"][2]["byte_count"] = 1
                write_json(root / "manifest.json", manifest)
                with self.assertRaises(CHECKER.EvidenceError):
                    CHECKER.evaluate(root / "manifest.json")

    def test_type_aware_equality(self):
        for value in (1, 1.0):
            with self.subTest(value=value):
                directory, root, manifest, registry = self.make_package()
                self.addCleanup(directory.cleanup)
                observed = CHECKER.load_json(root / "observed.json")
                observed["value"]["fixture"] = value
                write_json(root / "observed.json", observed)
                manifest["artifacts"][2]["sha256"] = digest(root / "observed.json")
                manifest["artifacts"][2]["byte_count"] = (root / "observed.json").stat().st_size
                write_json(root / "manifest.json", manifest)
                report = self.report(root, registry)
                self.assertEqual(report["result"], "fail")
                self.assertIn("case_a: exact_json_equal failed", report["errors"])

    def test_closed_manifest_and_registry_validation(self):
        for defect in ("version", "unknown_field", "boolean_size", "empty_required",
                       "malformed_exemptions", "malformed_reference", "fixture_namespace",
                       "duplicate_required", "duplicate_assertion", "hardware"):
            with self.subTest(defect=defect):
                directory, root, manifest, registry = self.make_package()
                self.addCleanup(directory.cleanup)
                if defect == "version":
                    manifest["schema"]["version"] = "unsupported"
                elif defect == "unknown_field":
                    manifest["extra"] = None
                elif defect == "boolean_size":
                    manifest["artifacts"][0]["byte_count"] = True
                elif defect == "empty_required":
                    registry["required_cases"] = []
                elif defect == "malformed_exemptions":
                    registry["fixture_profile"]["exemptions"] = [{}]
                elif defect == "malformed_reference":
                    registry["required_cases"][0]["criteria"][0]["observed_artifact"] = []
                elif defect == "fixture_namespace":
                    registry["fixture_assertions"][0]["id"] = "not_fixture"
                elif defect == "duplicate_required":
                    registry["required_cases"].append(registry["required_cases"][0])
                elif defect == "duplicate_assertion":
                    registry["fixture_assertions"].append(registry["fixture_assertions"][0])
                else:
                    manifest["run"]["profile"] = "hardware"
                write_json(root / "requirements.json", registry)
                manifest["registries"]["requirements"]["sha256"] = digest(root / "requirements.json")
                write_json(root / "manifest.json", manifest)
                with self.assertRaises(CHECKER.EvidenceError):
                    CHECKER.evaluate(root / "manifest.json", digest(root / "requirements.json"))

    def test_case_coverage_and_not_run_claims(self):
        directory, root, manifest, registry = self.make_package()
        self.addCleanup(directory.cleanup)
        manifest["cases"] = []
        manifest["run"]["result"] = "incomplete"
        write_json(root / "manifest.json", manifest)
        self.assertEqual(self.report(root, registry)["result"], "incomplete")
        manifest["cases"] = [{"id": "case_a", "execution": "not_run", "claimed_result": "pass"}]
        write_json(root / "manifest.json", manifest)
        self.assertEqual(self.report(root, registry)["result"], "fail")

    def test_malformed_json_remains_failure_for_unexecuted_case(self):
        for text in ("{", "NaN", "1e999", "[" * 2000 + "]" * 2000, "9" * 10000):
            with self.subTest(prefix=text[:12]):
                directory, root, manifest, registry = self.make_package()
                self.addCleanup(directory.cleanup)
                (root / "input.json").write_text(text, encoding="utf-8")
                manifest["artifacts"][0]["sha256"] = digest(root / "input.json")
                manifest["artifacts"][0]["byte_count"] = (root / "input.json").stat().st_size
                manifest["cases"][0]["execution"] = "not_run"
                manifest["cases"][0]["claimed_result"] = "not_run"
                manifest["run"]["result"] = "incomplete"
                write_json(root / "manifest.json", manifest)
                with self.assertRaises(CHECKER.EvidenceError):
                    CHECKER.evaluate(root / "manifest.json", digest(root / "requirements.json"))

    def test_portable_path_rejections(self):
        directory, root, manifest, registry = self.make_package()
        self.addCleanup(directory.cleanup)
        for path in ("../input.json", "/input.json", "C:input.json",
                     "./input.json", "a//input.json", "a/../input.json"):
            with self.subTest(path=path):
                with self.assertRaises(CHECKER.EvidenceError):
                    CHECKER.safe_path(root, path)

    def test_symlink_boundaries_and_role_aliases(self):
        directory, root, manifest, registry = self.make_package()
        self.addCleanup(directory.cleanup)
        outside = tempfile.TemporaryDirectory()
        self.addCleanup(outside.cleanup)
        external = Path(outside.name) / "external.json"
        write_json(external, {})
        try:
            (root / "external.json").symlink_to(external)
            (root / "internal.json").symlink_to(root / "input.json")
        except (OSError, NotImplementedError) as error:
            self.skipTest("symlink creation unavailable: " + str(error))
        with self.assertRaises(CHECKER.EvidenceError):
            CHECKER.safe_path(root, "external.json")
        self.assertEqual(CHECKER.safe_path(root, "internal.json"), (root / "input.json").resolve())
        with self.assertRaises(CHECKER.EvidenceError):
            CHECKER.evaluate(root / "external.json")
        (root / "observed.json").unlink()
        (root / "observed.json").symlink_to(root / "expected.json")
        with self.assertRaisesRegex(CHECKER.EvidenceError, "same file"):
            CHECKER.evaluate(root / "manifest.json", digest(root / "requirements.json"))

    def test_hard_link_role_alias(self):
        directory, root, manifest, registry = self.make_package()
        self.addCleanup(directory.cleanup)
        (root / "observed.json").unlink()
        try:
            os.link(root / "expected.json", root / "observed.json")
        except (OSError, NotImplementedError) as error:
            self.skipTest("hard link creation unavailable: " + str(error))
        with self.assertRaisesRegex(CHECKER.EvidenceError, "same file"):
            CHECKER.evaluate(root / "manifest.json", digest(root / "requirements.json"))


if __name__ == "__main__":
    unittest.main()
