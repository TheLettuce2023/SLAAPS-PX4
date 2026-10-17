#!/usr/bin/env python3
"""Independently check P2E commit, reset, sensor, and publication observations."""

import argparse
import importlib.util
import json
from pathlib import Path
import re


HERE = Path(__file__).resolve().parent
PREFIX = "Tools/research_sitl/provenance/"
spec = importlib.util.spec_from_file_location("p2d_check", HERE.parent / "native_merge/check_native_merge.py")
p2d_check = importlib.util.module_from_spec(spec)
spec.loader.exec_module(p2d_check)
require = p2d_check.require
read = p2d_check.read
sha = p2d_check.sha
uint = p2d_check.uint
OVERLAYS = p2d_check.OVERLAYS


def bit_array(value, size, label):
    require(type(value) is list and len(value) == size, label + " shape")
    for item in value:
        uint(item)
        require(item < 1 << 32, label + " width")


def committed_trace(value, run_id, case, merged, sensor):
    expected_keys = {"schema", "run_id", "fault", "count", "start_us", "stop_intent_us", "end_us",
                     "health_qualified", "positive_admission", "eligible_for_control", "records"}
    require(set(value) == expected_keys, "committed trace fields")
    require(value["schema"] == "p2e-committed-state/v1" and value["run_id"] == run_id, "commit run binding")
    require(value["health_qualified"] is False and value["positive_admission"] is False
            and value["eligible_for_control"] is False, "invented committed-state authority")
    for key in ("fault", "count", "start_us", "stop_intent_us", "end_us"):
        uint(value[key])
    require(value["fault"] == (0 if case == "normal" else 4), "wrong committed-state disposition")
    require(0 < value["start_us"] <= value["stop_intent_us"] <= value["end_us"],
            "committed-state lifecycle chronology")
    rows = value["records"]
    merged_rows = merged["records"]
    require(type(rows) is list and 0 < len(rows) <= 2048 and len(rows) == value["count"]
            and len(rows) == len(merged_rows), "committed-state row coverage")
    expected_sensor = [sensor["accelerometer_device_id"], sensor["gyroscope_device_id"],
                       sensor["accelerometer_calibration_count"], sensor["gyroscope_calibration_count"]]
    last_source = 0
    committed_updates = 0
    attitude_publications = 0
    reset_transitions = 0
    previous_resets = None
    for index, (row, merged_row) in enumerate(zip(rows, merged_rows)):
        expected_row = {"sequence", "source_us", "sample_us", "update_committed", "sensor", "control_flags",
                        "fault_flags", "validity", "resets", "attitude", "local_position"}
        require(set(row) == expected_row, "committed row fields")
        for key in ("sequence", "source_us", "sample_us", "control_flags", "fault_flags"):
            uint(row[key])
        require(row["sequence"] == index + 1 and row["sequence"] == merged_row["sequence"],
                "committed sequence")
        require(row["sample_us"] == merged_row["sample_us"] and row["control_flags"] == merged_row["control_flags"]
                and row["fault_flags"] == merged_row["fault_flags"], "committed/P2D payload differs")
        require(last_source <= row["source_us"] and merged_row["source_us"] <= row["source_us"] <= value["end_us"]
                and value["start_us"] <= row["source_us"], "committed source chronology")
        require(type(row["sensor"]) is list and row["sensor"] == expected_sensor, "sensor provenance drift")
        validity = row["validity"]
        require(type(validity) is list and len(validity) == 5
                and all(type(item) is int and item in (0, 1) for item in validity), "committed validity types")
        require([validity[0], validity[1], validity[3], validity[4]] == merged_row["validity"],
                "committed/P2D validity differs")
        resets = row["resets"]
        require(type(resets) is list and len(resets) == 5, "reset shape")
        for item in resets:
            uint(item)
            require(item < 256, "reset counter width")
        if previous_resets is not None and resets != previous_resets:
            reset_transitions += 1
        previous_resets = resets
        require(type(row["update_committed"]) is bool, "commit predicate type")
        attitude = row["attitude"]
        if validity[0]:
            require(type(attitude) is dict and set(attitude) == {"quaternion_bits", "reset"},
                    "missing committed attitude publication")
            bit_array(attitude["quaternion_bits"], 4, "quaternion")
            require(attitude["reset"] == resets[0], "attitude reset binding")
            attitude_publications += 1
        else:
            require(attitude is None, "invalid attitude was presented as published")
        local = row["local_position"]
        if row["update_committed"]:
            require(type(local) is dict and set(local) == {"position_bits", "velocity_bits", "acceleration_bits",
                                                          "validity", "resets"},
                    "missing committed local-position publication")
            bit_array(local["position_bits"], 3, "position")
            bit_array(local["velocity_bits"], 3, "velocity")
            bit_array(local["acceleration_bits"], 3, "acceleration")
            require(local["validity"] == validity[1:] and local["resets"] == resets,
                    "local-position commit binding")
            committed_updates += 1
        else:
            require(local is None, "uncommitted update presented a local-position publication")
        last_source = row["source_us"]
    return dict(case=case, records=len(rows), committed_updates=committed_updates,
                attitude_publications=attitude_publications, reset_transition_rows=reset_transitions,
                sensor_provenance=expected_sensor, health_qualified=False,
                positive_admission=False, eligible_for_control=False)


def check(area):
    area = Path(area).resolve()
    manifest = read(area / "manifest.json")
    require(manifest["schema"] == "p2e-committed-build/v1"
            and manifest["status"] == "captured_unqualified", "incomplete P2E build/run")
    require(sha(area / "source_hashes.json") == manifest["source_hashes_sha256"], "source registry drift")
    registry = read(area / "source_hashes.json", 16 * 1024 * 1024)
    require(set(manifest["overlay"]) == OVERLAYS, "overlay inventory")
    for rel, hashes in manifest["overlay"].items():
        require(registry[rel] == hashes["original"] and sha(area / "source" / rel) == hashes["derived"],
                "overlay drift")
    merge_rel = PREFIX + "merge/ReceiptMerge.cpp"
    require(manifest["overlay"][merge_rel]["original"] == manifest["overlay"][merge_rel]["derived"],
            "merge algorithm changed")
    prefixes = tuple(PREFIX + part + "/" for part in ("native", "merge", "native_merge", "committed_state"))
    expected = {rel for rel in registry if rel.startswith(prefixes)}
    actual = {str(path.relative_to(area / "inputs")) for path in (area / "inputs").rglob("*") if path.is_file()}
    require(actual == expected and PREFIX + "committed_state/CommittedState.cpp" in expected,
            "P2E source archive inventory")
    for rel in expected:
        require(sha(area / "inputs" / rel) == registry[rel], "archived P2E input drift")
    build = area / "build"
    source = area / "source"
    require(sha(build / "bin/px4") == manifest["executable_sha256"], "executable drift")
    expected_commands = {
        "configure": ["cmake", "-S", str(source), "-B", str(build), "-G", "Ninja",
                      "-DCONFIG=px4_sitl_p2e-default", "-DVENDOR=px4", "-DMODEL=sitl", "-DLABEL=p2e-default",
                      f"-DPX4_BOARD_DIR={source}/boards/px4/sitl",
                      f"-DPX4_CONFIG_FILE={source}/{PREFIX}native/p2b-default.px4board",
                      f"-DEXTERNAL_MODULES_LOCATION={source}/{PREFIX}committed_state/px4_external",
                      "-DCMAKE_BUILD_TYPE=Debug", "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"],
        "build": ["cmake", "--build", str(build), "--target", "px4", "-j", "4"],
    }
    for name in ("configure", "build"):
        require(manifest[name]["exit_code"] == 0 and manifest[name]["command"] == expected_commands[name],
                "build command binding")
        require(sha(area / (name + ".log")) == manifest[name]["log_sha256"], "build log drift")
    audit = manifest["configuration_audit"]
    require(sha(build / "boardconfig") == audit["boardconfig_sha256"]
            and sha(build / "compile_commands.json") == audit["compile_commands_sha256"],
            "configuration drift")
    config = {line.split("=", 1)[0] for line in (build / "boardconfig").read_text().splitlines()
              if line.endswith("=y")}
    require({item for item in config if item.startswith("CONFIG_MODULES_")} == {"CONFIG_MODULES_EKF2"}
            and not any(item.startswith("CONFIG_DRIVERS_") for item in config), "nonclosed P2E profile")
    require("CONFIG_BOARD_NOLOCKSTEP" in config and "CONFIG_EKF2_MULTI_INSTANCE" not in config,
            "P2E clock/instance profile")
    wanted = {"ekf2", "research_provenance", "param", "shutdown", "uorb", "ver", "list_tasks",
              "list_files", "sleep"}
    require(set(re.findall(r'apps\["([a-zA-Z0-9_]+)"\]',
                           (build / "platforms/posix/apps.cpp").read_text())) == wanted, "builtin inventory")
    commands = read(build / "compile_commands.json", 16 * 1024 * 1024)
    forbidden = ("-DENABLE_LOCKSTEP_SCHEDULER", "-DCONFIG_MUORB_APPS_SYNC_TIMESTAMP",
                 "RESEARCH_C1E_NEGATIVE", "RESEARCH_SITL_DIAGNOSTIC")
    for command in commands:
        require(not any(token in command["command"] for token in forbidden), "mixed P2E profile")
    for ending in ("/CommittedState.cpp", "/NativeMerge.cpp", "/ReceiptMerge.cpp", "/EKF2.cpp",
                   "/research_provenance.cpp"):
        selected = [command for command in commands if command["file"].endswith(ending)]
        require(len(selected) == 1 and "-DRESEARCH_P2E_COMMITTED_STATE=1" in selected[0]["command"]
                and "-DRESEARCH_P2D_NATIVE=1" in selected[0]["command"], "missing P2E profile definition")
    sensor = manifest["sensor_provenance"]
    require(set(sensor) == {"accelerometer_device_id", "gyroscope_device_id",
                            "accelerometer_calibration_count", "gyroscope_calibration_count", "source_sha256",
                            "selection_topic", "selection_instance", "sample_topic", "sample_instance"},
            "sensor provenance schema")
    require(sensor["source_sha256"] == "62665d68f92ceb47068a5b5f16a1407998e0f2cf08049e98fc55cf61e90c8143"
            and sensor["selection_topic"] == "sensor_selection" and sensor["selection_instance"] == 0
            and sensor["sample_topic"] == "sensor_combined" and sensor["sample_instance"] == 0,
            "sensor provenance source")
    require(len(manifest["runs"]) == 2 and [run["case"] for run in manifest["runs"]] == ["normal", "restart"],
            "case inventory")
    require(manifest["runs"][0]["run_id"] != manifest["runs"][1]["run_id"], "reused process identity")
    results = []
    for run in manifest["runs"]:
        case = run["case"]
        runtime = area / ("run-" + case)
        command = run["command"]
        require(len(command) == 7 and command[:2] == [str(build / "bin/px4"), "-i"]
                and command[2].isdigit() and 10000 <= int(command[2]) < 60000
                and command[3:] == ["-w", str(runtime), "-s", str(runtime / "startup.sh")],
                "native invocation binding")
        require(run["exit_code"] == 0 and run["forced_shutdown"] is False
                and run["sensor_provenance"] == sensor, "unclean or rebound P2E process")
        artifacts = {"startup.sh", "imu.txt", "stdout.log", "merged.json", "committed.json"}
        if case == "normal":
            artifacts.add("observations.json")
        require(set(run["artifacts"]) == artifacts, "case artifact inventory")
        for name, digest in run["artifacts"].items():
            require(sha(runtime / name) == digest, "case artifact drift")
        require(sha(runtime / "startup.sh") == registry[PREFIX + "committed_state/" + case + ".sh"],
                "startup binding")
        require(sha(runtime / "imu.txt") == run["fixture"]["fixture_sha256"]
                and run["fixture"]["source_sha256"] == sensor["source_sha256"], "fixture binding")
        log = (runtime / "stdout.log").read_text()
        marker = "P2E_CAPTURE_COMPLETE" if case == "normal" else "P2E_EXPECTED_RESTART_REJECTION"
        require(marker in log and "ERROR [" not in log and "P2E_UNEXPECTED_RESTART_ACCEPTANCE" not in log,
                "case completion")
        if case == "restart":
            require(not (runtime / "observations.json").exists(), "restart presented as raw success")
        merged = read(runtime / "merged.json")
        raw = read(runtime / "observations.json") if case == "normal" else None
        p2d_check.trace(merged, run["run_id"], case, raw)
        results.append(committed_trace(read(runtime / "committed.json"), run["run_id"], case, merged, sensor))
    return dict(verified=True, scope="committed_estimator_output_reset_sensor_observation_only",
                cases=results, manifest_sha256=sha(area / "manifest.json"), health_qualified=False,
                positive_admission=False, eligible_for_control=False)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("area", type=Path)
    print(json.dumps(check(parser.parse_args().area), indent=2, sort_keys=True))
