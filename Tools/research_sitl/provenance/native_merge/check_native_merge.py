#!/usr/bin/env python3
"""Check actual typed source/owner observations against the parallel raw capture."""
import argparse
import importlib.util
import json
from pathlib import Path
import re

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("raw_check", HERE.parent / "native/check_capture.py")
raw_check = importlib.util.module_from_spec(spec)
spec.loader.exec_module(raw_check)
require, read, sha, uint = raw_check.require, raw_check.read, raw_check.sha, raw_check.uint
PREFIX = "Tools/research_sitl/provenance/"
OVERLAYS = {"src/modules/ekf2/EKF2.cpp", "src/modules/ekf2/CMakeLists.txt", PREFIX + "merge/ReceiptMerge.hpp",
            PREFIX + "merge/ReceiptMerge.cpp", PREFIX + "native/px4_external/src/research_provenance/research_provenance.cpp"}


def trace(value, run_id, case, raw=None):
    require(type(run_id) is str and re.fullmatch(r"[1-9][0-9]*", run_id) is not None
            and int(run_id) < 1 << 64, "invalid run identity")
    require(set(value) == {"schema", "run_id", "bridge_fault", "owner_fault", "source_count", "start_us",
                           "stop_intent_us", "end_us", "close_us", "positive_admission", "clock_map_qualified", "records"}, "trace fields")
    require(value["schema"] == "p2d-native-merge/v1" and value["run_id"] == run_id, "run binding")
    require(value["positive_admission"] is False and value["clock_map_qualified"] is False, "invented authority")
    for key in ("bridge_fault", "owner_fault", "source_count", "start_us", "stop_intent_us", "end_us", "close_us"):
        uint(value[key])
    require(value["bridge_fault"] == (0 if case == "normal" else 4), "wrong bridge disposition")
    require(value["owner_fault"] == 15, "owner did not close normally")
    require(0 < value["start_us"] <= value["stop_intent_us"] <= value["end_us"] <= value["close_us"], "lifecycle chronology")
    rows = value["records"]
    require(type(rows) is list and 0 < len(rows) <= 2048 and len(rows) == value["source_count"], "lost merge tail")
    if raw is not None:
        raw_check.observations(raw, run_id)
        require(len(rows) == len(raw["records"]), "raw/merged counts differ")
    last_source, last_end, last_sample = 0, 0, 0
    maximum_delay = 0
    for i, row in enumerate(rows):
        require(set(row) == {"owner_order", "owner_begin_us", "owner_end_us", "slot", "sequence", "source_us", "kind",
                             "identity", "sample_us", "control_flags", "fault_flags", "validity"}, "row fields")
        for key in set(row) - {"identity", "validity"}: uint(row[key])
        require(row["owner_order"] == i + 1 and row["sequence"] == i + 1 and row["slot"] == 0
                and row["kind"] == (0 if i == 0 else 1), "order/initial/sequence")
        require(type(row["identity"]) is list and all(type(x) is int for x in row["identity"])
                and row["identity"] == [int(run_id)] * 3 + [1, 1, 1], "native lifetime binding")
        validity = row["validity"]
        require(type(validity) is list and len(validity) == 4 and all(type(x) is int and x in (0, 1) for x in validity), "predicate types")
        require(row["fault_flags"] < 1 << 32 and validity[0] == int(bool(row["control_flags"] & 1)), "predicate contradiction")
        require(last_sample < row["sample_us"] <= row["source_us"] and last_source <= row["source_us"], "source chronology")
        require(value["start_us"] <= row["source_us"] <= value["end_us"], "source outside observed lifetime")
        require(0 < row["owner_begin_us"] and last_end <= row["owner_begin_us"] <= row["owner_end_us"] <= value["close_us"]
                and row["source_us"] <= row["owner_end_us"], "owner completion bracket")
        if raw is not None:
            original = raw["records"][i]
            require(all(row[k] == original[k] for k in ("sequence", "sample_us", "control_flags", "fault_flags")), "typed/raw payload differs")
            require(validity == [int(original[k]) for k in ("attitude_valid", "horizontal_valid", "vertical_position_valid", "vertical_velocity_valid")], "typed/raw predicates differ")
            require(original["observed_us"] <= row["source_us"], "typed hook preceded raw hook")
        maximum_delay = max(maximum_delay, row["owner_end_us"] - row["source_us"])
        last_source, last_end, last_sample = row["source_us"], row["owner_end_us"], row["sample_us"]
    return dict(case=case, records=len(rows), maximum_source_to_owner_completion_us=maximum_delay,
                restart_rejected=case == "restart", positive_admission=False)


def check(area):
    area = Path(area).resolve()
    m = read(area / "manifest.json")
    require(m["schema"] == "p2d-native-build/v1" and m["status"] == "captured_unqualified", "incomplete build/run")
    require(sha(area / "source_hashes.json") == m["source_hashes_sha256"], "source registry drift")
    registry = read(area / "source_hashes.json", 16 * 1024 * 1024)
    require(set(m["overlay"]) == OVERLAYS, "overlay inventory")
    for rel, hashes in m["overlay"].items():
        require(registry[rel] == hashes["original"] and sha(area / "source" / rel) == hashes["derived"], "overlay drift")
    require(m["overlay"][PREFIX + "merge/ReceiptMerge.cpp"]["original"] ==
            m["overlay"][PREFIX + "merge/ReceiptMerge.cpp"]["derived"], "merge algorithm changed")
    prefixes = tuple(PREFIX + x + "/" for x in ("native", "merge", "native_merge"))
    expected = {rel for rel in registry if rel.startswith(prefixes)}
    actual = {str(p.relative_to(area / "inputs")) for p in (area / "inputs").rglob("*") if p.is_file()}
    require(actual == expected and PREFIX + "native_merge/NativeMerge.cpp" in expected, "source archive inventory")
    for rel in expected: require(sha(area / "inputs" / rel) == registry[rel], "archived input drift")
    build = area / "build"
    require(sha(build / "bin/px4") == m["executable_sha256"], "executable drift")
    source = area / "source"
    expected_commands = {
        "configure": ["cmake", "-S", str(source), "-B", str(build), "-G", "Ninja", "-DCONFIG=px4_sitl_p2d-default",
                      "-DVENDOR=px4", "-DMODEL=sitl", "-DLABEL=p2d-default", f"-DPX4_BOARD_DIR={source}/boards/px4/sitl",
                      f"-DPX4_CONFIG_FILE={source}/{PREFIX}native/p2b-default.px4board",
                      f"-DEXTERNAL_MODULES_LOCATION={source}/{PREFIX}native_merge/px4_external",
                      "-DCMAKE_BUILD_TYPE=Debug", "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"],
        "build": ["cmake", "--build", str(build), "--target", "px4", "-j", "4"],
    }
    for name in ("configure", "build"):
        require(m[name]["exit_code"] == 0 and m[name]["command"] == expected_commands[name], "build command binding")
        require(sha(area / (name + ".log")) == m[name]["log_sha256"], "build log drift")
    audit = m["configuration_audit"]
    require(sha(build / "boardconfig") == audit["boardconfig_sha256"] and
            sha(build / "compile_commands.json") == audit["compile_commands_sha256"], "configuration drift")
    config = {line.split("=", 1)[0] for line in (build / "boardconfig").read_text().splitlines() if line.endswith("=y")}
    require({c for c in config if c.startswith("CONFIG_MODULES_")} == {"CONFIG_MODULES_EKF2"} and
            not any(c.startswith("CONFIG_DRIVERS_") for c in config), "nonclosed profile")
    require("CONFIG_BOARD_NOLOCKSTEP" in config and "CONFIG_EKF2_MULTI_INSTANCE" not in config, "clock/instance profile")
    wanted = {"ekf2", "research_provenance", "param", "shutdown", "uorb", "ver", "list_tasks", "list_files", "sleep"}
    require(set(re.findall(r'apps\["([a-zA-Z0-9_]+)"\]', (build / "platforms/posix/apps.cpp").read_text())) == wanted, "builtin inventory")
    commands = read(build / "compile_commands.json", 16 * 1024 * 1024)
    for c in commands:
        require(not any(x in c["command"] for x in ("-DENABLE_LOCKSTEP_SCHEDULER", "-DCONFIG_MUORB_APPS_SYNC_TIMESTAMP",
                                                   "RESEARCH_C1E_NEGATIVE", "RESEARCH_SITL_DIAGNOSTIC", "RESEARCH_CLOSED_MERGE_HOST")), "mixed profile")
    for ending in ("/NativeMerge.cpp", "/ReceiptMerge.cpp", "/EKF2.cpp", "/research_provenance.cpp"):
        selected = [c for c in commands if c["file"].endswith(ending)]
        require(len(selected) == 1 and "-DRESEARCH_P2D_NATIVE=1" in selected[0]["command"], "missing native typed profile")
    require(len(m["runs"]) == 2 and [r["case"] for r in m["runs"]] == ["normal", "restart"], "case inventory")
    require(m["runs"][0]["run_id"] != m["runs"][1]["run_id"], "reused process lifetime")
    results = []
    for run in m["runs"]:
        case = run["case"]; runtime = area / ("run-" + case)
        command = run["command"]
        require(len(command) == 7 and command[:2] == [str(build / "bin/px4"), "-i"] and
                command[2].isdigit() and 10000 <= int(command[2]) < 60000 and
                command[3:] == ["-w", str(runtime), "-s", str(runtime / "startup.sh")], "native invocation binding")
        require(run["exit_code"] == 0 and run["forced_shutdown"] is False, "unclean process")
        artifacts = {"startup.sh", "imu.txt", "stdout.log", "merged.json"} | ({"observations.json"} if case == "normal" else set())
        require(set(run["artifacts"]) == artifacts, "case artifact inventory")
        for name, digest in run["artifacts"].items(): require(sha(runtime / name) == digest, "case artifact drift")
        require(sha(runtime / "startup.sh") == registry[PREFIX + "native_merge/" + case + ".sh"], "startup binding")
        require(sha(runtime / "imu.txt") == run["fixture"]["fixture_sha256"] and run["fixture"]["source_sha256"] ==
                "62665d68f92ceb47068a5b5f16a1407998e0f2cf08049e98fc55cf61e90c8143", "fixture binding")
        log = (runtime / "stdout.log").read_text()
        marker = "P2D_CAPTURE_COMPLETE" if case == "normal" else "P2D_EXPECTED_RESTART_REJECTION"
        require(marker in log and "ERROR [" not in log and "P2D_UNEXPECTED_RESTART_ACCEPTANCE" not in log, "case completion")
        if case == "restart": require(not (runtime / "observations.json").exists(), "restart presented as raw success")
        results.append(trace(read(runtime / "merged.json"), run["run_id"], case,
                             read(runtime / "observations.json") if case == "normal" else None))
    return dict(verified=True, scope="native_typed_observation_merge_only", cases=results,
                manifest_sha256=sha(area / "manifest.json"), positive_admission=False, clock_map_qualified=False)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("area", type=Path)
    print(json.dumps(check(parser.parse_args().area), indent=2, sort_keys=True))
