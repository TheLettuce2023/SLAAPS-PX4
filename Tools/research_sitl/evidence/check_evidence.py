#!/usr/bin/env python3
"""Fail-closed checker for the software_contract evidence slice only."""

import argparse
import hashlib
import json
import math
import sys
from pathlib import Path, PurePosixPath, PureWindowsPath


VERSION = "1.0.0"
SCHEMA_NAME = "sitl_software_contract_evidence"
REGISTRY_NAME = "sitl_software_contract_registry"
PASS_EXIT, FAIL_EXIT, INCOMPLETE_EXIT, USAGE_EXIT = 0, 2, 3, 4


class EvidenceError(Exception):
    pass


def _no_duplicates(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise EvidenceError("duplicate JSON key: " + key)
        result[key] = value
    return result


def _bad_constant(value):
    raise EvidenceError("non-finite JSON constant: " + value)


def load_json(path):
    try:
        with path.open("r", encoding="utf-8") as stream:
            value = json.load(stream, object_pairs_hook=_no_duplicates,
                              parse_constant=_bad_constant)
        _require_finite(value)
        return value
    except (OSError, UnicodeError, ValueError, RecursionError) as error:
        raise EvidenceError("invalid JSON " + str(path) + ": " + str(error)) from error


def _require_finite(value):
    if type(value) is float and not math.isfinite(value):
        raise EvidenceError("non-finite JSON number")
    if type(value) is list:
        for item in value:
            _require_finite(item)
    elif type(value) is dict:
        for item in value.values():
            _require_finite(item)


def json_equal(left, right):
    if type(left) is not type(right):
        return False
    if type(left) is dict:
        return set(left) == set(right) and all(json_equal(left[key], right[key]) for key in left)
    if type(left) is list:
        return len(left) == len(right) and all(json_equal(a, b) for a, b in zip(left, right))
    return left == right


def sha256_file(path):
    digest = hashlib.sha256()
    try:
        with path.open("rb") as stream:
            for block in iter(lambda: stream.read(65536), b""):
                digest.update(block)
    except OSError as error:
        raise EvidenceError("unreadable file " + str(path) + ": " + str(error)) from error
    return digest.hexdigest()


def require_object(value, name, allowed, required):
    if type(value) is not dict:
        raise EvidenceError(name + " must be an object")
    unknown = set(value) - set(allowed)
    missing = set(required) - set(value)
    if unknown:
        raise EvidenceError(name + " has unknown field(s): " + ", ".join(sorted(unknown)))
    if missing:
        raise EvidenceError(name + " lacks field(s): " + ", ".join(sorted(missing)))
    return value


def require_string(value, name):
    if type(value) is not str or not value:
        raise EvidenceError(name + " must be a nonempty string")
    return value


def safe_path(root, relative):
    require_string(relative, "path")
    posix = PurePosixPath(relative)
    windows = PureWindowsPath(relative)
    parts = relative.split("/")
    if ":" in relative or windows.drive or windows.root or any(part in ("", ".", "..") for part in parts):
        raise EvidenceError("unsafe artifact path: " + relative)
    if ("\\" in relative or posix.is_absolute() or windows.is_absolute() or
            any(part in ("", ".", "..") for part in posix.parts)):
        raise EvidenceError("unsafe artifact path: " + relative)
    candidate = root.joinpath(*parts)
    try:
        resolved_root = root.resolve(strict=True)
        resolved = candidate.resolve(strict=True)
        resolved.relative_to(resolved_root)
    except (OSError, ValueError, RuntimeError) as error:
        raise EvidenceError("artifact is missing or escapes package: " + relative) from error
    if not resolved.is_file():
        raise EvidenceError("artifact is not a regular file: " + relative)
    return resolved


def expect_hash(path, expected, label):
    if type(expected) is not str or len(expected) != 64 or any(c not in "0123456789abcdef" for c in expected):
        raise EvidenceError(label + " must be a lowercase SHA-256")
    actual = sha256_file(path)
    if actual != expected:
        raise EvidenceError(label + " hash mismatch")


def check_artifacts(root, artifacts):
    if type(artifacts) is not list or not artifacts:
        raise EvidenceError("artifacts must be a nonempty list")
    seen, by_path, kinds = set(), {}, set()
    for index, item in enumerate(artifacts):
        item = require_object(item, "artifact[{}]".format(index),
                              ("path", "kind", "sha256", "byte_count", "complete"),
                              ("path", "kind", "sha256", "byte_count", "complete"))
        path = require_string(item["path"], "artifact.path")
        if path in seen:
            raise EvidenceError("duplicate artifact path: " + path)
        seen.add(path)
        if item["kind"] not in ("raw_input", "raw_output", "fixture_expected"):
            raise EvidenceError("unsupported artifact kind")
        if type(item["byte_count"]) is not int or item["byte_count"] <= 0:
            raise EvidenceError("artifact byte_count must be a positive integer")
        if type(item["complete"]) is not bool or not item["complete"]:
            raise EvidenceError("artifact must be complete")
        actual_path = safe_path(root, path)
        if actual_path.stat().st_size != item["byte_count"]:
            raise EvidenceError("artifact byte_count mismatch: " + path)
        expect_hash(actual_path, item["sha256"], "artifact " + path)
        # This profile declares JSON artifacts only. Integrity errors remain
        # failures even for not-run cases or unresolved operational criteria.
        load_json(actual_path)
        by_path[path] = (actual_path, item["kind"])
        kinds.add(item["kind"])
    if not {"raw_input", "raw_output", "fixture_expected"}.issubset(kinds):
        raise EvidenceError("raw input, raw output, and fixture expected artifacts are required")
    return by_path


def check_identity(root, identity):
    identity = require_object(identity, "identity", ("schema_sha256", "checker_sha256", "host_sources"),
                              ("schema_sha256", "checker_sha256", "host_sources"))
    schema = Path(__file__).with_name("manifest.schema.json")
    checker = Path(__file__).resolve()
    expect_hash(schema, identity["schema_sha256"], "schema")
    expect_hash(checker, identity["checker_sha256"], "checker")
    sources = identity["host_sources"]
    if type(sources) is not list or not sources:
        raise EvidenceError("host_sources must be a nonempty list")
    source_paths = set()
    for index, source in enumerate(sources):
        source = require_object(source, "host_sources[{}]".format(index),
                                ("path", "sha256", "dirty", "dirty_files"),
                                ("path", "sha256", "dirty", "dirty_files"))
        path = require_string(source["path"], "host source path")
        if path in source_paths:
            raise EvidenceError("duplicate host source path")
        source_paths.add(path)
        actual = safe_path(root, path)
        expect_hash(actual, source["sha256"], "host source " + path)
        if type(source["dirty"]) is not bool or type(source["dirty_files"]) is not list:
            raise EvidenceError("invalid dirty source attribution")
        if source["dirty"] != bool(source["dirty_files"]):
            raise EvidenceError("dirty source attribution is inconsistent")
        for dirty in source["dirty_files"]:
            dirty = require_object(dirty, "dirty file", ("path", "sha256"), ("path", "sha256"))
            dirty_path = safe_path(root, dirty["path"])
            expect_hash(dirty_path, dirty["sha256"], "dirty file " + dirty["path"])


def load_registry(root, reference):
    reference = require_object(reference, "registries.requirements", ("path", "sha256"), ("path", "sha256"))
    path = safe_path(root, reference["path"])
    expect_hash(path, reference["sha256"], "requirements registry")
    registry = load_json(path)
    registry = require_object(registry, "registry",
                              ("schema", "fixture_profile", "fixture_assertions", "requirements", "required_cases"),
                              ("schema", "fixture_profile", "fixture_assertions", "requirements", "required_cases"))
    schema = require_object(registry["schema"], "registry.schema", ("name", "version"), ("name", "version"))
    if schema["name"] != REGISTRY_NAME or schema["version"] != VERSION:
        raise EvidenceError("unsupported requirements registry version")
    fixture = require_object(registry["fixture_profile"], "fixture_profile", ("profile", "non_operational", "exemptions"),
                             ("profile", "non_operational", "exemptions"))
    if fixture["profile"] != "software_contract" or fixture["non_operational"] is not True:
        raise EvidenceError("fixture profile must be non_operational software_contract")
    exemptions = fixture["exemptions"]
    expected_exemptions = {"integrated_sitl", "hardware", "simulator_runner", "ulog", "flight_metrics"}
    if (type(exemptions) is not list or any(type(item) is not str for item in exemptions) or
            set(exemptions) != expected_exemptions or len(exemptions) != len(expected_exemptions)):
        raise EvidenceError("fixture exemptions must exactly declare this slice's limits")
    assertions = registry["fixture_assertions"]
    if type(assertions) is not list or not assertions:
        raise EvidenceError("fixture_assertions must be nonempty")
    assertion_ids = set()
    for assertion in assertions:
        assertion = require_object(assertion, "fixture assertion", ("id", "non_operational"), ("id", "non_operational"))
        ident = require_string(assertion["id"], "fixture assertion id")
        if not ident.startswith("fixture_") or assertion["non_operational"] is not True or ident in assertion_ids:
            raise EvidenceError("invalid fixture assertion")
        assertion_ids.add(ident)
    if type(registry["requirements"]) is not list:
        raise EvidenceError("requirements must be a list")
    requirement_ids = set()
    for requirement in registry["requirements"]:
        requirement = require_object(requirement, "requirement", ("id", "approved"), ("id", "approved"))
        requirement_id = require_string(requirement["id"], "requirement id")
        if not requirement_id.startswith("REQ_") or requirement["approved"] is not False or requirement_id in requirement_ids:
            raise EvidenceError("software-contract registry may list only unapproved REQ_* requirements")
        requirement_ids.add(requirement_id)
    required = registry["required_cases"]
    if type(required) is not list or not required:
        raise EvidenceError("required_cases must be nonempty")
    cases = {}
    for item in required:
        item = require_object(item, "required case", ("id", "criteria"), ("id", "criteria"))
        ident = require_string(item["id"], "case id")
        if ident in cases or type(item["criteria"]) is not list or not item["criteria"]:
            raise EvidenceError("duplicate case id or empty criteria")
        criteria = []
        for criterion in item["criteria"]:
            criterion = require_object(criterion, "criterion", ("assertion", "type", "input_artifact", "input_sha256", "expected_artifact", "expected_sha256", "observed_artifact"),
                                       ("assertion", "type", "input_artifact", "input_sha256", "expected_artifact", "expected_sha256", "observed_artifact"))
            assertion = require_string(criterion["assertion"], "criterion assertion")
            for field in ("input_artifact", "expected_artifact", "observed_artifact"):
                require_string(criterion[field], "criterion " + field)
            _pinned_registry_digest(criterion["input_sha256"])
            _pinned_registry_digest(criterion["expected_sha256"])
            if assertion.startswith("REQ_"):
                if assertion not in requirement_ids:
                    raise EvidenceError("criterion references undeclared REQ_* requirement")
            elif assertion not in assertion_ids:
                raise EvidenceError("criterion does not use a predeclared fixture assertion")
            if criterion["type"] != "exact_json_equal":
                raise EvidenceError("unsupported criterion type")
            criteria.append(criterion)
        cases[ident] = criteria
    return cases


def _pinned_registry_digest(value):
    if type(value) is not str or len(value) != 64 or any(c not in "0123456789abcdef" for c in value):
        raise EvidenceError("--registry-sha256 must be a lowercase SHA-256")


def evaluate(manifest_path, pinned_registry_sha256=None):
    root = manifest_path.parent.resolve()
    manifest = load_json(safe_path(root, manifest_path.name))
    manifest = require_object(manifest, "manifest", ("schema", "run", "registries", "identity", "cases", "artifacts"),
                              ("schema", "run", "registries", "identity", "cases", "artifacts"))
    schema = require_object(manifest["schema"], "schema", ("name", "version", "sha256"), ("name", "version", "sha256"))
    if schema["name"] != SCHEMA_NAME or schema["version"] != VERSION:
        raise EvidenceError("unsupported manifest schema version")
    expect_hash(Path(__file__).with_name("manifest.schema.json"), schema["sha256"], "manifest schema")
    run = require_object(manifest["run"], "run", ("id", "profile", "result"), ("id", "profile", "result"))
    require_string(run["id"], "run id")
    if run["profile"] != "software_contract":
        raise EvidenceError("unsupported profile: only software_contract is implemented")
    if run["result"] not in ("pass", "fail", "incomplete", "not_run"):
        raise EvidenceError("invalid run result")
    registries = require_object(manifest["registries"], "registries", ("requirements",), ("requirements",))
    check_identity(root, manifest["identity"])
    artifacts = check_artifacts(root, manifest["artifacts"])
    required = load_registry(root, registries["requirements"])
    pin_missing = pinned_registry_sha256 is None
    if not pin_missing:
        _pinned_registry_digest(pinned_registry_sha256)
        if pinned_registry_sha256 != registries["requirements"]["sha256"]:
            raise EvidenceError("external registry digest does not match manifest")
    evidence = manifest["cases"]
    if type(evidence) is not list:
        raise EvidenceError("cases must be a list")
    supplied = {}
    for item in evidence:
        item = require_object(item, "case evidence", ("id", "execution", "claimed_result"), ("id", "execution", "claimed_result"))
        ident = require_string(item["id"], "case evidence id")
        if ident in supplied or ident not in required:
            raise EvidenceError("duplicate or unknown case evidence id")
        if item["execution"] not in ("executed", "not_run") or item["claimed_result"] not in ("pass", "fail", "incomplete", "not_run"):
            raise EvidenceError("invalid case terminal state")
        supplied[ident] = item
    statuses, errors = [], []
    if pin_missing:
        statuses.append("incomplete")
        errors.append("external --registry-sha256 pin is required")
    for ident, criteria in required.items():
        entry = supplied.get(ident)
        if entry is None or entry["execution"] == "not_run":
            if entry is not None and entry["claimed_result"] != "not_run":
                statuses.append("fail")
                errors.append(ident + ": not_run case has contradictory claimed_result")
            else:
                statuses.append("incomplete")
                errors.append(ident + ": required case not run")
            continue
        case_failed = False
        case_incomplete = False
        for criterion in criteria:
            if criterion["assertion"].startswith("REQ_"):
                case_incomplete = True
                errors.append(ident + ": unresolved operational REQ_* requirement")
                continue
            try:
                input_path, input_kind = artifacts[criterion["input_artifact"]]
                expected_path, expected_kind = artifacts[criterion["expected_artifact"]]
                observed_path, observed_kind = artifacts[criterion["observed_artifact"]]
            except KeyError:
                raise EvidenceError("criterion references undeclared artifact")
            if (input_kind, expected_kind, observed_kind) != ("raw_input", "fixture_expected", "raw_output"):
                raise EvidenceError("criterion artifact kinds are invalid")
            if len({criterion["input_artifact"], criterion["expected_artifact"], criterion["observed_artifact"]}) != 3:
                raise EvidenceError("criterion artifacts must be distinct")
            if (len({input_path, expected_path, observed_path}) != 3 or
                    input_path.samefile(expected_path) or input_path.samefile(observed_path) or
                    expected_path.samefile(observed_path)):
                raise EvidenceError("criterion artifact roles resolve to the same file")
            if sha256_file(input_path) != criterion["input_sha256"] or sha256_file(expected_path) != criterion["expected_sha256"]:
                raise EvidenceError("criterion input or expectation differs from pinned registry")
            load_json(input_path)
            expected = load_json(expected_path)
            observed = load_json(observed_path)
            for name, document in (("expected", expected), ("observed", observed)):
                document = require_object(document, name + " assertion", ("case_id", "input_sha256", "value"),
                                          ("case_id", "input_sha256", "value"))
                if document["case_id"] != ident or document["input_sha256"] != sha256_file(input_path):
                    raise EvidenceError(name + " assertion has wrong case or input hash")
            if not json_equal(expected, observed):
                case_failed = True
                errors.append(ident + ": exact_json_equal failed")
        case_result = "fail" if case_failed else "incomplete" if case_incomplete else "pass"
        if entry["claimed_result"] != case_result:
            case_result = "fail"
            errors.append(ident + ": claimed_result does not match recomputed result")
        statuses.append(case_result)
    result = "fail" if "fail" in statuses else "incomplete" if "incomplete" in statuses else "pass"
    if not pin_missing and run["result"] != result:
        result = "fail"
        errors.append("manifest run.result does not match recomputed result")
    return {"version": VERSION, "run_id": run["id"], "result": result, "errors": errors}


class JsonArgumentParser(argparse.ArgumentParser):
    def error(self, message):
        print(json.dumps({"version": VERSION, "result": "fail", "errors": [message]},
                         sort_keys=True, separators=(",", ":")))
        self.exit(USAGE_EXIT)


def main(argv=None):
    parser = JsonArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--registry-sha256", help="externally pinned immutable requirements registry SHA-256")
    arguments = parser.parse_args(argv)
    try:
        report = evaluate(arguments.manifest, arguments.registry_sha256)
    except (EvidenceError, OSError, RecursionError) as error:
        report = {"version": VERSION, "result": "fail", "errors": [str(error)]}
    print(json.dumps(report, sort_keys=True, separators=(",", ":")))
    return {"pass": PASS_EXIT, "fail": FAIL_EXIT, "incomplete": INCOMPLETE_EXIT}[report["result"]]


if __name__ == "__main__":
    sys.exit(main())
