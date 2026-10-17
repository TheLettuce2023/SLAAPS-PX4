#!/usr/bin/env python3
"""Read-only evidence preflight. Never authorizes flashing or physical operation."""

import argparse
from datetime import datetime
import hashlib
import json
from pathlib import Path
import re
import sys


REQUIREMENTS = {
    "integrated_firmware": "Source-bound linked bench owner/output path and resource report",
    "software_regression": "Strict host, sanitizer, SITL and Pixhawk 6X evidence for that source",
    "output_fault_contract": "Reviewed startup, shutdown, owner failure, loss and reboot output behavior",
    "source_identity": "Exact QGC build/gamepad calibration and observed TELEM1 MAVLink identity",
    "timing": "Measured link age/queueing and scheduler gaps against approved limits",
    "canard_calibration": "MAIN1-4 labels, TED arrows, reversal, angle/PWM pairs and coupled mix verification",
    "power": "Installed BEC topology, rail voltage and simultaneous loaded current/temperature",
    "load_envelope": "Finite approved maximum wind and demonstrated holding/slew capability",
    "independent_stop": "Independent stop circuit, residual-wind restraint and restart inhibition",
    "edf_isolation": "Physical EDF energy isolation and inhibited AUX4",
    "unpowered_output_capture": "Servos disconnected: scope captures of boot, commands and all fault cases",
    "physical_test_review": "Named test authority review of powered no-wind and staged wind results",
}
SHA256 = re.compile(r"[0-9a-f]{64}\Z")


def template():
    """Unknown remains unknown; no operational coefficients or thresholds."""
    return {
        "schema_version": 1,
        "scope": "servo_only_wind_tunnel",
        "baseline": {"path": "", "sha256": ""},
        "requirements": {
            key: {"status": "missing", "reviewer": "", "reviewed_utc": "",
                  "summary": description, "evidence": []}
            for key, description in REQUIREMENTS.items()
        },
    }


def _unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"Duplicate JSON key: {key}")
        result[key] = value
    return result


def load(path):
    def reject_constant(value):
        raise ValueError(f"Nonfinite JSON constant: {value}")
    return json.loads(path.read_text(encoding="utf-8"),
                      object_pairs_hook=_unique_object, parse_constant=reject_constant)


def _artifact(record, root):
    if not isinstance(record, dict) or set(record) != {"path", "sha256"}:
        return "expected path and sha256"
    name, expected = record["path"], record["sha256"]
    if not isinstance(name, str) or not name or "\\" in name:
        return "expected nonempty portable relative path"
    if not isinstance(expected, str) or not SHA256.fullmatch(expected):
        return "expected lowercase SHA-256"
    relative = Path(name)
    if relative.is_absolute() or ".." in relative.parts or ":" in name:
        return "artifact path must stay within evidence root"
    path = (root / relative).resolve()
    if not path.is_relative_to(root):
        return "artifact resolves outside evidence root"
    if not path.is_file():
        return "artifact is missing or not a regular file"
    try:
        with path.open("rb") as stream:
            actual = hashlib.file_digest(stream, "sha256").hexdigest()
    except OSError as error:
        return f"artifact unreadable: {error}"
    return None if actual == expected else "artifact SHA-256 mismatch"


def audit(record, evidence_root):
    """Completeness and byte integrity only, never truth or safety certification."""
    root = Path(evidence_root).resolve()
    blockers = []
    if not isinstance(record, dict) or set(record) != {
            "schema_version", "scope", "baseline", "requirements"}:
        blockers.append("record: unexpected schema fields")
        record = {}
    if type(record.get("schema_version")) is not int or record.get("schema_version") != 1:
        blockers.append("record: unsupported schema version")
    if record.get("scope") != "servo_only_wind_tunnel":
        blockers.append("record: wrong scope")
    issue = _artifact(record.get("baseline"), root)
    if issue:
        blockers.append("baseline: " + issue)
    requirements = record.get("requirements")
    if not isinstance(requirements, dict):
        requirements = {}
    if set(requirements) != set(REQUIREMENTS):
        blockers.append("requirements: missing or unknown requirement IDs")
    for key in REQUIREMENTS:
        entry = requirements.get(key)
        if not isinstance(entry, dict) or set(entry) != {
                "status", "reviewer", "reviewed_utc", "summary", "evidence"}:
            blockers.append(key + ": invalid entry schema")
            continue
        if entry["status"] != "reviewed":
            blockers.append(key + ": not reviewed (missing/assumed evidence is insufficient)")
        for field in ("reviewer", "summary"):
            if not isinstance(entry[field], str) or not entry[field].strip():
                blockers.append(key + ": missing " + field)
        # Require a real UTC calendar timestamp, not a free-form acceptance claim.
        try:
            stamp = entry["reviewed_utc"]
            if not isinstance(stamp, str) or not re.fullmatch(
                    r"\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z", stamp):
                raise ValueError("invalid UTC form")
            datetime.strptime(stamp, "%Y-%m-%dT%H:%M:%SZ")
        except ValueError:
            blockers.append(key + ": invalid reviewed_utc")
        evidence = entry["evidence"]
        if not isinstance(evidence, list) or not evidence:
            blockers.append(key + ": missing evidence artifacts")
            continue
        for item in evidence:
            issue = _artifact(item, root)
            if issue:
                blockers.append(key + ": " + issue)
    return {
        "evidence_complete": not blockers,
        "physical_acceptance_granted": False,
        "flash_authorized": False,
        "outputs_authorized": False,
        "blockers": blockers,
        "limitation": "Hashes prove byte identity, not factual correctness or reviewer authority. "
                      "Independent human physical acceptance remains mandatory.",
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("record", type=Path, nargs="?")
    parser.add_argument("--evidence-root", type=Path)
    parser.add_argument("--template", action="store_true", help="Print blank record; write nothing")
    args = parser.parse_args()
    if args.template:
        if args.record or args.evidence_root:
            parser.error("--template cannot be combined with a record or evidence root")
        print(json.dumps(template(), indent=2))
        return 0
    if not args.record or not args.evidence_root:
        parser.error("record and --evidence-root are required")
    try:
        result = audit(load(args.record), args.evidence_root)
    except (OSError, ValueError, RecursionError) as error:
        print(json.dumps({"evidence_complete": False, "physical_acceptance_granted": False,
                          "flash_authorized": False, "outputs_authorized": False,
                          "blockers": [str(error)]}, indent=2))
        return 2
    print(json.dumps(result, indent=2))
    return 0 if result["evidence_complete"] else 1


if __name__ == "__main__":
    sys.exit(main())
