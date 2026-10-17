#!/usr/bin/env python3
"""Audit integrity and cross-links of a stock SITL diagnostic v0 package.

This is deliberately not a Phase 1 acceptance checker. The v0 package has no
approved timing registry, complete source archive, or second-operator evidence.
"""

import argparse
import hashlib
import json
import math
import re
import sys
from pathlib import Path


SHA256 = re.compile(r"[0-9a-f]{64}\Z")
MODES = {"GUI": "gui", "headless": "headless"}
REQUIRED_FILES = {
    "plan.json", "build-result.json", "campaign-result.json",
    "qgc-observation.json", "git-status-before.txt", "git-status-after.txt",
    "ulog-measurements-gui.json", "ulog-measurements-headless.json",
}


class AuditError(Exception):
    pass


def unique_pairs(pairs):
    value = {}
    for key, item in pairs:
        if key in value:
            raise AuditError("duplicate JSON key: " + key)
        value[key] = item
    return value


def finite_constant(value):
    raise AuditError("non-finite JSON constant: " + value)


def load_json(path):
    try:
        # The historical v0 files were written by PowerShell with a UTF-8 BOM.
        with path.open(encoding="utf-8-sig") as stream:
            return json.load(stream, object_pairs_hook=unique_pairs,
                             parse_constant=finite_constant,
                             parse_float=lambda value: checked_float(value))
    except (OSError, UnicodeError, ValueError, RecursionError) as error:
        raise AuditError("invalid JSON " + str(path) + ": " + str(error)) from error


def checked_float(value):
    number = float(value)
    if not math.isfinite(number):
        raise AuditError("non-finite JSON number")
    return number


def require(condition, reason):
    if not condition:
        raise AuditError(reason)


def digest(path):
    value = hashlib.sha256()
    try:
        with path.open("rb") as stream:
            for block in iter(lambda: stream.read(1024 * 1024), b""):
                value.update(block)
    except OSError as error:
        raise AuditError("unreadable artifact: " + str(path)) from error
    return value.hexdigest()


def check_file(path, entry):
    require(type(entry) is dict, "invalid file inventory entry")
    expected_hash = entry.get("sha256")
    expected_size = entry.get("bytes")
    require(type(expected_hash) is str and SHA256.fullmatch(expected_hash) is not None,
            "invalid SHA-256 for " + str(path))
    require(type(expected_size) is int and expected_size > 0,
            "invalid byte count for " + str(path))
    require(path.is_file(), "missing artifact: " + str(path))
    require(path.stat().st_size == expected_size, "byte count mismatch: " + str(path))
    require(digest(path) == expected_hash, "SHA-256 mismatch: " + str(path))


def child_file(root, name):
    require(type(name) is str and name not in ("", ".", "..") and
            "/" not in name and "\\" not in name and ":" not in name,
            "unsafe evidence file name")
    path = root / name
    require(path.resolve(strict=True).parent == root.resolve(strict=True),
            "evidence file escapes directory: " + name)
    return path


def check_inventory(root, inventory):
    require(inventory.get("schema") == "sitl-baseline-artifact-inventory/v0",
            "unsupported inventory schema")
    files = inventory.get("evidence_files")
    require(type(files) is list and files, "missing evidence file inventory")
    names = set()
    for item in files:
        require(type(item) is dict, "invalid evidence file entry")
        name = item.get("name")
        path = child_file(root, name)
        require(name not in names and name != "artifact-inventory.json",
                "duplicate or self-referential evidence file")
        names.add(name)
        check_file(path, item)
    actual = {path.name for path in root.iterdir()}
    require(actual == names | {"artifact-inventory.json"},
            "unlisted or missing evidence file")
    require(REQUIRED_FILES <= names, "mandatory evidence file absent")
    return names


def check_ulogs(campaign_root, inventory, campaign, root):
    ulogs = inventory.get("ulogs")
    runs = campaign.get("runs")
    require(type(ulogs) is list and type(runs) is list and len(ulogs) == len(runs) == 2,
            "GUI/headless ULog pair required")
    require({item.get("mode") for item in ulogs} == set(MODES),
            "GUI/headless ULog modes required")
    require({item.get("mode") for item in runs} == set(MODES),
            "GUI/headless campaign modes required")
    reports = {}
    for entry in ulogs:
        mode = entry["mode"]
        name = MODES[mode]
        path_string = entry.get("path")
        require(type(path_string) is str, "missing ULog path")
        path = Path(path_string)
        require(path.is_absolute() and path.is_file(), "missing absolute ULog: " + path_string)
        require(path.resolve(strict=True).is_relative_to(campaign_root.resolve(strict=True)),
                "ULog escapes campaign root")
        check_file(path, entry)
        run = next(item for item in runs if item["mode"] == mode)
        require(run.get("ulog_path") == path_string and
                run.get("ulog_sha256") == entry["sha256"] and
                run.get("ulog_bytes") == entry["bytes"],
                "campaign ULog link mismatch: " + mode)
        report = load_json(root / ("ulog-measurements-" + name + ".json"))
        require(report.get("schema") == "sitl-baseline-ulog-measurements/v0" and
                report.get("ulog_sha256") == entry["sha256"] and
                report.get("ulog_bytes") == entry["bytes"],
                "measurement ULog link mismatch: " + mode)
        require(report.get("dropouts") == run.get("ulog_dropouts") and
                report.get("missing_topics") == [] and
                type(report.get("topics")) is dict and
                len(report["topics"]) == run.get("selected_topics_present") == 14,
                "measurement topic/dropout claim mismatch: " + mode)
        attitude = report["topics"].get("vehicle_attitude")
        require(type(attitude) is list and len(attitude) == 1,
                "missing single attitude instance: " + mode)
        data = attitude[0]
        require(data.get("timestamp", {}).get("zero_steps") ==
                run.get("attitude_zero_timestamp_steps") and
                data.get("timestamp", {}).get("negative_steps") ==
                run.get("attitude_negative_timestamp_steps") and
                data.get("equal_timestamp_pairs", {}).get("same_timestamp_sample") ==
                run.get("attitude_equal_timestamp_same_sample") and
                data.get("equal_timestamp_pairs", {}).get("same_quaternion") ==
                run.get("attitude_equal_timestamp_same_quaternion"),
                "attitude measurement claim mismatch: " + mode)
        reports[name] = report
    return reports


def audit(campaign_root):
    root = campaign_root / "evidence"
    require(root.is_dir(), "missing evidence directory")
    inventory = load_json(root / "artifact-inventory.json")
    check_inventory(root, inventory)
    plan = load_json(root / "plan.json")
    build = load_json(root / "build-result.json")
    campaign = load_json(root / "campaign-result.json")
    qgc = load_json(root / "qgc-observation.json")
    run_id = inventory.get("run_id")
    require(type(run_id) is str and run_id and
            run_id == plan.get("run_id") == build.get("run_id") == campaign.get("run_id"),
            "campaign identity mismatch")
    require(plan.get("schema") == "sitl-baseline-diagnostic-plan/v0" and
            build.get("schema") == "sitl-baseline-build-result/v0" and
            campaign.get("schema") == "sitl-baseline-diagnostic-result/v0" and
            qgc.get("schema") == "sitl-qgc-observation/v0",
            "unsupported diagnostic schema")
    require(qgc.get("run_id") == run_id + "-gui", "QGC run identity mismatch")
    require(campaign.get("source_revision") == plan.get("source", {}).get("revision") and
            campaign.get("gz_submodule_revision") == plan.get("stock_model", {}).get("submodule_revision") and
            campaign.get("executable_sha256") == build.get("executable_sha256"),
            "source/build identity mismatch")
    require(digest(root / build.get("build_log")) == build.get("build_log_sha256"),
            "build log link mismatch")
    require((root / "git-status-before.txt").read_bytes() ==
            (root / "git-status-after.txt").read_bytes(),
            "checkout status changed during diagnostic")
    check_ulogs(campaign_root, inventory, campaign, root)
    require(campaign.get("phase1_result") == "incomplete", "v0 cannot claim Phase 1 pass")
    reasons = [
        "v0 has no approved numeric freshness/deadline registry or integrated acceptance checker",
        "v0 has no immutable archive of dirty source contents",
        "v0 has no second-operator reproduction",
    ]
    if qgc.get("human_visual_observation") != "complete":
        reasons.append("human QGroundControl visual observations are incomplete")
    if plan.get("phase1_numeric_acceptance") != "approved":
        reasons.append("numeric acceptance criteria are unapproved")
    return {"schema": "sitl-baseline-diagnostic-audit/v0", "run_id": run_id,
            "result": "incomplete", "integrity": "verified", "reasons": reasons,
            "inventory_sha256": digest(root / "artifact-inventory.json")}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("campaign_root", type=Path)
    args = parser.parse_args()
    try:
        result = audit(args.campaign_root)
        code = 3
    except (AuditError, OSError, TypeError, KeyError, AttributeError) as error:
        result = {"schema": "sitl-baseline-diagnostic-audit/v0",
                  "result": "fail", "integrity": "invalid", "reasons": [str(error)]}
        code = 2
    print(json.dumps(result, indent=2, allow_nan=False))
    return code


if __name__ == "__main__":
    sys.exit(main())
