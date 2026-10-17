#!/usr/bin/env python3
"""Check P2B source observations and their isolated build/run binding."""

import argparse
import hashlib
import json
from pathlib import Path
import re


def require(value, reason):
    if not value:
        raise ValueError(reason)


def unique(pairs):
    result = {}
    for k, v in pairs:
        require(k not in result, "duplicate key")
        result[k] = v
    return result


def bad_number(value):
    raise ValueError("noncanonical number: " + value)


def read(path, bound=4 * 1024 * 1024):
    require(path.is_file() and path.stat().st_size <= bound, "missing/oversize JSON")
    return json.loads(path.read_text(), object_pairs_hook=unique,
                      parse_constant=bad_number, parse_float=bad_number)


def sha(path):
    with path.open("rb") as f:
        return hashlib.file_digest(f, "sha256").hexdigest()


def uint(value, maximum=(1 << 64) - 1):
    require(type(value) is int and 0 <= value <= maximum, "invalid unsigned value")


def observations(value, run_id):
    require(set(value) == {"schema", "run_id", "input_count", "fault", "count", "admission_instantiated",
                           "clock_map_qualified", "input_timestamps", "records"}, "unknown observation schema")
    require(value["schema"] == "native-ekf-observation/v1" and value["run_id"] == run_id
            and re.fullmatch(r"[1-9][0-9]*", run_id) is not None and int(run_id) < (1 << 64), "wrong run binding")
    require(value["admission_instantiated"] is False and value["clock_map_qualified"] is False, "authority invented")
    for field in ("input_count", "fault", "count"):
        uint(value[field])
    require(value["fault"] == 0 and 0 < value["count"] <= 2048 and 0 < value["input_count"] <= 1200, "fault/bounds")
    require(type(value["records"]) is list and len(value["records"]) == value["count"], "record count")
    stamps = value["input_timestamps"]
    require(type(stamps) is list and len(stamps) == value["input_count"], "input chronology count")
    previous = 0
    for stamp in stamps:
        uint(stamp)
        require(stamp > previous, "input clock regression")
        previous = stamp
    positions = {stamp:i for i, stamp in enumerate(stamps)}
    last_sample, last_now, last_index = 0, 0, -1
    delays, gaps, changes = [], [], 0
    booleans = ("attitude_valid", "horizontal_valid", "vertical_position_valid", "vertical_velocity_valid")
    last_state = None
    for index, record in enumerate(value["records"]):
        require(set(record) == {"sequence", "sample_us", "observed_us", "control_flags", "fault_flags", *booleans}, "record fields")
        for field in ("sequence", "sample_us", "observed_us", "control_flags"):
            uint(record[field])
        uint(record["fault_flags"], (1 << 32) - 1)
        require(all(type(record[k]) is bool for k in booleans), "non-Boolean predicate")
        require(record["sequence"] == index + 1, "source sequence gap")
        sample, now = record["sample_us"], record["observed_us"]
        require(sample > last_sample and now >= last_now and sample <= now, "source chronology")
        require(sample in positions and positions[sample] > last_index, "sample not from recorded input publication")
        # Source getter is exactly control_status.flags.tilt_align (bit 0).
        require(record["attitude_valid"] == bool(record["control_flags"] & 1), "source predicate contradiction")
        state = tuple(record[k] for k in booleans) + (record["control_flags"], record["fault_flags"])
        changes += int(last_state is not None and state != last_state)
        if last_now:
            gaps.append(now - last_now)
        delays.append(now - sample)
        last_sample, last_now, last_index, last_state = sample, now, positions[sample], state
    return {"record_count": len(delays), "input_count": len(stamps), "unobserved_input_publications": len(stamps) - len(delays),
            "observed_state_changes": changes, "maximum_input_to_hook_us": max(delays),
            "maximum_hook_gap_us": max(gaps, default=0),
            "invalid_attitude_observations": sum(not r["attitude_valid"] for r in value["records"]),
            "valid_attitude_observations": sum(r["attitude_valid"] for r in value["records"]),
            "publisher_continuity_qualified": False, "positive_admission": False}


def source_binding(area, manifest, registry, runtime):
    expected_overlay = {"src/modules/ekf2/EKF2.cpp", "src/modules/ekf2/CMakeLists.txt"}
    require(set(manifest["overlay"]) == expected_overlay, "incomplete overlay inventory")
    for rel, hashes in manifest["overlay"].items():
        require(registry[rel] == hashes["original"] and sha(area / "source" / rel) == hashes["derived"], "source overlay drift")
    prefix = "Tools/research_sitl/provenance/native/"
    expected = {rel for rel in registry if rel.startswith(prefix)}
    require(prefix + "startup.sh" in expected and prefix + "run_capture.py" in expected, "incomplete native registry")
    archive = Path(manifest["native_source_archive"]).resolve()
    require(archive.parent == area and archive.name.startswith("source-inputs-") and archive.is_dir(), "missing/escaped source archive")
    actual = {str(p.relative_to(archive)) for p in archive.rglob("*") if p.is_file()}
    require(actual == expected, "native archive inventory differs")
    for rel in expected:
        require(not (archive / rel).is_symlink() and sha(archive / rel) == registry[rel], "archived native input drift")
    require(sha(runtime / "startup.sh") == registry[prefix + "startup.sh"], "executed startup drift")


def check(area):
    area = Path(area).resolve()
    manifest = read(area / "manifest.json")
    require(manifest["schema"] == "p2b-native-build/v1" and manifest["status"] == "captured_unqualified", "incomplete native run")
    require(len(manifest["runs"]) == 1, "unexpected run count")
    build = area / "build"; runtime = area / "run-1"; run = manifest["runs"][0]
    require(sha(build / "bin/px4") == manifest["executable_sha256"], "executable drift")
    require(run["exit_code"] == 0 and run["forced_shutdown"] is False, "unclean process closure")
    require(sha(runtime / "stdout.log") == run["stdout_sha256"] and
            sha(runtime / "observations.json") == run["observations_sha256"], "run artifact drift")
    require("P2B_CAPTURE_COMPLETE" in (runtime / "stdout.log").read_text(), "missing completion marker")
    require("ERROR [" not in (runtime / "stdout.log").read_text(), "native runtime error")
    require(sha(runtime / "imu.txt") == manifest["fixture"]["fixture_sha256"], "fixture drift")
    require(sha(area / "source_hashes.json") == manifest["source_hashes_sha256"], "source registry drift")
    registry = read(area / "source_hashes.json", 16 * 1024 * 1024)
    source_binding(area, manifest, registry, runtime)
    config = {line.split("=", 1)[0] for line in (build / "boardconfig").read_text().splitlines() if line.endswith("=y")}
    require({c for c in config if c.startswith("CONFIG_MODULES_")} == {"CONFIG_MODULES_EKF2"}, "module outside allowlist")
    require(not any(c.startswith("CONFIG_DRIVERS_") for c in config), "output driver configured")
    require("CONFIG_BOARD_NOLOCKSTEP" in config and "CONFIG_EKF2_MULTI_INSTANCE" not in config, "wrong native profile")
    wanted = {"ekf2", "research_provenance", "param", "shutdown", "uorb", "ver", "list_tasks", "list_files", "sleep"}
    actual = set(re.findall(r'apps\["([a-zA-Z0-9_]+)"\]', (build / "platforms/posix/apps.cpp").read_text()))
    require(actual == wanted, "builtin outside allowlist")
    audit = manifest["configuration_audit"]
    require(sha(build / "boardconfig") == audit["boardconfig_sha256"] and
            sha(build / "compile_commands.json") == audit["compile_commands_sha256"], "build config drift")
    commands = read(build / "compile_commands.json", 16 * 1024 * 1024)
    require(not any(flag in c["command"] for c in commands for flag in
                    ("-DENABLE_LOCKSTEP_SCHEDULER", "-DCONFIG_MUORB_APPS_SYNC_TIMESTAMP", "RESEARCH_SITL_DIAGNOSTIC", "RESEARCH_C1E_NEGATIVE")), "mixed/proxy profile")
    trace = read(runtime / "observations.json")
    result = observations(trace, run["run_id"])
    context = run["clock_context"]
    require(context["parent_time_namespace"] == context["child_time_namespace"], "time namespaces differ")
    require(context["interposition_environment_removed"] is True, "clock interposition not excluded")
    first, last = context["before_launch_ns"] // 1000, (context["after_exit_ns"] + 999) // 1000
    require(all(first <= r["sample_us"] <= r["observed_us"] <= last for r in trace["records"]), "host/native clock bracket contradiction")
    result.update(verified=True, scope="native_output_boundary_observation_only", manifest_sha256=sha(area / "manifest.json"))
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("area", type=Path)
    args = parser.parse_args()
    print(json.dumps(check(args.area), indent=2, sort_keys=True))
