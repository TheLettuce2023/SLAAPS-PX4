#!/usr/bin/env python3
"""Check first-operator stock Cessna evidence; keep reproduction gate explicit."""

import argparse
from datetime import datetime
import json
from pathlib import Path
import re
import sys
import tarfile

from inspect_raw_ulog import digest, load_json, require
from score_interval import REGISTRY_SHA256, registry, score_ulog


def utc(value):
    require(type(value) is str, "missing UTC timestamp")
    result = datetime.fromisoformat(value.replace("Z", "+00:00"))
    require(result.tzinfo is not None and result.utcoffset().total_seconds() == 0,
            "timestamp must be UTC")
    return result


def clock_bracket(pair, bound, ready, stopped):
    require(type(pair) is list and len(pair) == 2 and
            all(type(x["px4_us"]) is int for x in pair), "invalid clock bracket")
    observed = [utc(item["utc"]) for item in pair]
    require(pair[0]["px4_us"] <= bound <= pair[1]["px4_us"] and
            all(ready <= value < stopped for value in observed),
            "interval UTC/monotonic observations missing")
    # Wall UTC can step under WSL time synchronization. The approved interval
    # clock is PX4 monotonic time; preserve and report the UTC step explicitly.
    return observed[1] < observed[0]


def reference(path, root):
    path, root = Path(path).resolve(), Path(root).resolve()
    require(path.is_relative_to(root), "artifact outside package")
    return {"path": str(path.relative_to(root)), "sha256": digest(path),
            "bytes": path.stat().st_size}


def artifact(root, ref):
    require(type(ref) is dict and set(ref) == {"path", "sha256", "bytes"}, "invalid artifact reference")
    require(type(ref["path"]) is str and type(ref["bytes"]) is int and ref["bytes"] >= 0,
            "invalid artifact path/size")
    path = (root / ref["path"]).resolve(strict=True)
    require(path.is_relative_to(root.resolve()), "artifact escapes package")
    require(path.is_file() and path.stat().st_size == ref["bytes"] and
            digest(path) == ref["sha256"], "artifact integrity: " + ref["path"])
    return path


def verify_source(root, plan):
    """Verify archived input bytes rather than relying on dirty Git status alone."""
    import hashlib
    index_path = artifact(root, plan["artifacts"]["source_index"])
    index = load_json(index_path)
    require(index["schema"] == "sitl-source-snapshot/v1" and index["repositories"], "invalid source snapshot")
    for repo in index["repositories"]:
        require(type(repo["revision"]) is str and re.fullmatch(r"[0-9a-f]{40}", repo["revision"]) and
                type(repo["status"]) is str, "missing or malformed revision/status")
    expected = {entry["path"]: entry for entry in index["files"]}
    require(len(expected) == len(index["files"]) and expected, "duplicate/empty source inventory")
    require(all(not Path(name).is_absolute() and ".." not in Path(name).parts for name in expected),
            "source inventory path escapes repository")
    limits = registry()
    for proposal in (limits["approved_proposal"], limits["base_approved_proposal"]):
        require(expected.get(proposal["path"], {}).get("sha256") == proposal["sha256"],
                "approved proposal missing or changed in source archive")
    seen = set()
    with tarfile.open(artifact(root, plan["artifacts"]["source_archive"]), "r:gz") as archive:
        for member in archive:
            require(member.name in expected and member.name not in seen, "archive member mismatch")
            seen.add(member.name)
            entry = expected[member.name]
            require(member.mode == entry["mode"], "source mode mismatch")
            if member.issym():
                require(entry.get("symlink") == member.linkname, "source symlink mismatch")
            else:
                require(member.isfile() and "symlink" not in entry, "unexpected archive member type")
                stream = archive.extractfile(member)
                value = hashlib.file_digest(stream, "sha256").hexdigest()
                require(value == entry["sha256"] and member.size == entry["bytes"], "archived source mismatch")
    require(seen == set(expected), "source archive incomplete")
    build = load_json(artifact(root, plan["artifacts"]["build"]))
    require(type(build["exit_code"]) is int and build["exit_code"] == 0, "build failed")
    require(build["source_index_sha256"] == plan["artifacts"]["source_index"]["sha256"] and
            build["source_verified_after_build"] is True, "source/build binding missing")
    require(build["executable"] == plan["artifacts"]["executable"], "build/executable mismatch")
    artifact(root, build["transcript"])
    return index


def check_run(root, manifest_path):
    limits = registry()
    result = {"schema": "stock-cessna-acceptance-result/v1", "status": "incomplete",
              "phase1_result": "incomplete", "failures": [], "missing": []}
    require(manifest_path.resolve().is_relative_to(root.resolve()), "manifest outside package")
    manifest = load_json(manifest_path)
    require(manifest["schema"] == "stock-cessna-run-evidence/v1", "unsupported run evidence")
    plan_path = artifact(root, manifest["plan"])
    plan = load_json(plan_path)
    result["run_id"] = plan["run_id"]
    require(plan["schema"] == "stock-cessna-run-plan/v1" and
            plan["campaign_id"] == limits["campaign_id"] and
            plan["registry_sha256"] == REGISTRY_SHA256, "plan policy mismatch")
    require(plan["mode"] in ("gui", "headless") and type(plan["operator"]) is str and
            plan["operator"].strip(), "missing mode/operator")
    require(plan["interval_rule"] == {"ready_offset_us": 10000000, "duration_us": 60000000},
            "interval rule differs from approval")
    require(type(plan["telemetry_link_required"]) is bool and
            (plan["mode"] != "gui" or plan["telemetry_link_required"]), "invalid link expectation")
    for key in ("source_index", "source_archive", "build", "executable", "parameters", "registry",
                "approval", "signal_inventory"):
        artifact(root, plan["artifacts"][key])
    require(plan["artifacts"]["registry"]["sha256"] == REGISTRY_SHA256, "registry artifact mismatch")
    source_index = verify_source(root, plan)
    paths = {key: artifact(root, ref) for key, ref in manifest["artifacts"].items()}
    required = {"ulog", "transcript", "launch", "checkpoint", "lifecycle", "clock_bounds", "runtime_checks",
                "source_verification"}
    for key in sorted(required - paths.keys()):
        result["missing"].append("missing artifact: " + key)
    if result["missing"]:
        return result
    records = {key: load_json(paths[key]) for key in required - {"ulog", "transcript"}}
    for key, record in records.items():
        require(record["run_id"] == plan["run_id"], "run identity mismatch: " + key)
    launch, checkpoint, life = records["launch"], records["checkpoint"], records["lifecycle"]
    source_check = records["source_verification"]
    require(source_check["source_index_sha256"] == plan["artifacts"]["source_index"]["sha256"] and
            source_check["verified_before_launch"] is True and source_check["verified_after_shutdown"] is True and
            source_check["executable_unchanged"] is True, "source/executable changed during runtime")
    require(launch["plan_sha256"] == manifest["plan"]["sha256"], "launch did not bind frozen plan")
    require(launch["parameter_sha256"] == plan["artifacts"]["parameters"]["sha256"] and
            launch["executable_sha256"] == plan["artifacts"]["executable"]["sha256"], "launch input mismatch")
    require(launch["preexisting_px4_gz"] == [], "run did not start from fresh processes")
    created, launched, ready, stopped = map(utc, [plan["created_utc"], launch["utc"],
                                                checkpoint["utc"], life["shutdown_utc"]])
    require(created <= launched < ready < stopped, "invalid lifecycle chronology")
    require(type(checkpoint["px4_us"]) is int and checkpoint["px4_us"] > 0, "invalid checkpoint time")
    start = checkpoint["px4_us"] + limits["interval"]["settling_us"]
    end = start + limits["interval"]["score_duration_us"]
    result["utc_boundary_clock_steps"] = []
    for label, bound in (("start", start), ("end", end)):
        pair = records["clock_bounds"][label]
        if clock_bracket(pair, bound, ready, stopped):
            result["utc_boundary_clock_steps"].append({"boundary": label, "observations": pair,
                "note": "wall UTC stepped backward; no exact UTC synchronization claim; scoring uses PX4 monotonic time"})
    checks = records["runtime_checks"]
    transcript = paths["transcript"].read_text(errors="replace")
    for name in ("model_spawned", "bridge_clock_pose", "synthetic_airspeed_source", "disarmed_hold_ready",
                 "estimator_position_gnss_ready", "actuator_channels", "environment_network"):
        item = checks.get(name)
        if not item or item.get("status") == "incomplete":
            result["missing"].append("runtime check: " + name)
        elif item.get("status") == "fail":
            result["failures"].append("runtime check: " + name)
        else:
            require(item.get("status") == "pass" and type(item.get("quote")) is str and
                    item["quote"].strip() and item["quote"] in transcript,
                    "runtime check lacks transcript evidence: " + name)
    require(type(life["wrapper_exit_code"]) is int, "missing wrapper exit")
    if life["wrapper_exit_code"] != 0 or life["remaining_owned_processes"]:
        result["missing"].append("normal shutdown/cleanup not demonstrated")
    if not life["owned_processes"] or life["normal_shutdown"] is not True:
        result["missing"].append("missing process ownership or normal shutdown")
    require("shutdown" in transcript.lower() and "exiting" in transcript.lower(), "missing PX4 shutdown transcript")
    exception = limits["timestamp_policy"]["synthetic_pressure_zero_sample_exception"]
    publisher_verified = (any(entry["path"] == exception["publisher_source_path"] and
                              entry.get("sha256") == exception["publisher_source_sha256"]
                              for entry in source_index["files"]) and
                          checks.get("synthetic_airspeed_source", {}).get("status") == "pass")
    result["signals"] = score_ulog(paths["ulog"], manifest["artifacts"]["ulog"]["sha256"],
                                   start, end, plan["telemetry_link_required"], checkpoint["px4_us"], publisher_verified)
    result["failures"].extend(result["signals"]["failures"])
    result["missing"].extend(result["signals"]["missing"])
    if plan["mode"] == "gui":
        if "qgc" not in paths:
            result["missing"].append("human QGC observation pending")
        else:
            qgc = load_json(paths["qgc"])
            require(qgc["run_id"] == plan["run_id"] and launched < utc(qgc["utc"]) < stopped,
                    "QGC observation outside live run")
            for name in ("parameter_loading", "live_attitude", "gps_map_position", "displayed_mode",
                         "mavlink_inspector_traffic"):
                if qgc.get(name) is False:
                    result["failures"].append("QGC " + name)
                elif qgc.get(name) is not True:
                    result["missing"].append("QGC " + name)
            if qgc.get("comms_lost") == "live":
                result["failures"].append("QGC live Comms Lost")
            elif qgc.get("comms_lost") not in ("none", "after_normal_shutdown"):
                result["missing"].append("QGC warning timing unknown")
    result["status"] = "fail" if result["failures"] else "incomplete" if result["missing"] else "pass"
    result["scope"] = "single-operator stock Cessna run; reproduction gate deferred"
    result["manifest_sha256"] = digest(manifest_path)
    result["checker_sha256"] = digest(Path(__file__))
    result["scorer_sha256"] = digest(Path(__file__).with_name("score_interval.py"))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    try:
        result = check_run(args.root, args.manifest)
    except Exception as error:
        result = {"status": "fail", "phase1_result": "incomplete", "reason": str(error)}
    with args.output.open("x", encoding="utf-8") as stream:
        json.dump(result, stream, indent=2, allow_nan=False)
        stream.write("\n")
    print(json.dumps({"status": result["status"], "phase1_result": "incomplete", "output": str(args.output)}))
    return {"pass": 0, "fail": 2, "incomplete": 3}[result["status"]]


if __name__ == "__main__":
    sys.exit(main())
