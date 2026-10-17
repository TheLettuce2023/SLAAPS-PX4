#!/usr/bin/env python3
"""Independently check P2G selector lifecycle and selected-output provenance."""

import argparse
import importlib.util
import json
from pathlib import Path
import re


HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
PREFIX = "Tools/research_sitl/provenance/"
spec = importlib.util.spec_from_file_location(
    "p2f_check", HERE.parent / "selected_state/check_selected_state.py")
p2f_check = importlib.util.module_from_spec(spec)
spec.loader.exec_module(p2f_check)
require = p2f_check.require
read = p2f_check.read
sha = p2f_check.sha
uint = p2f_check.uint
OVERLAYS = {"src/modules/ekf2/EKF2.cpp", "src/modules/ekf2/EKF2Selector.cpp",
            "src/modules/ekf2/EKF2Selector.hpp", "src/modules/ekf2/CMakeLists.txt"}
BUILTINS = {"ekf2", "research_selector_provenance", "param", "shutdown", "uorb", "ver",
            "list_tasks", "list_files", "sleep"}


def authority(value):
    keys = ("health_policy_configured", "health_qualified", "failover_qualified",
            "positive_admission", "eligible_for_control")
    require(all(value[key] is False for key in keys), "invented selector authority")


def output_time(row, previous_sample, previous_selector_publication, lifecycle):
    for key in ("sequence", "observed_us", "sample_us", "estimator_publication_us",
                "selector_publication_us", "selected_instance", "source_instance"):
        uint(row[key])
    require(row["selected_instance"] == 0 and row["source_instance"] == 0,
            "selected-output source binding")
    require(previous_sample < row["sample_us"] <= row["estimator_publication_us"]
            <= row["selector_publication_us"] <= row["observed_us"],
            "selected-output chronology")
    require(previous_selector_publication <= row["selector_publication_us"]
            and lifecycle["estimator_start_us"] <= row["estimator_publication_us"]
            and row["observed_us"] <= lifecycle["selector_end_us"],
            "selected-output lifetime")
    require(row["publication_succeeded"] is True, "failed selected-output publication")


def selector_trace(value, run_id, case, sensor):
    expected = {"schema", "run_id", "fault", "counts", "lifecycle", "binding",
                "health_policy_configured", "health_qualified", "failover_qualified",
                "positive_admission", "eligible_for_control", "selection", "statuses",
                "attitudes", "local_positions"}
    require(set(value) == expected and value["schema"] == "p2g-selector-state/v1"
            and value["run_id"] == run_id, "selector trace binding")
    authority(value)
    uint(value["fault"])
    require(value["fault"] == (0 if case == "normal" else 4), "wrong selector disposition")
    counts = value["counts"]
    require(set(counts) == {"status", "attitude", "local_position"}, "selector counts")
    for count in counts.values():
        uint(count)
    lifecycle = value["lifecycle"]
    lifecycle_keys = {"selector_start_us", "estimator_start_us", "stop_intent_us",
                      "selector_end_us", "estimator_end_us"}
    require(set(lifecycle) == lifecycle_keys, "selector lifecycle fields")
    for item in lifecycle.values():
        uint(item)
    require(0 < lifecycle["selector_start_us"] <= lifecycle["estimator_start_us"]
            <= lifecycle["stop_intent_us"] <= lifecycle["selector_end_us"]
            <= lifecycle["estimator_end_us"], "selector lifecycle chronology")
    require(type(value["binding"]) is list and len(value["binding"]) == 5
            and all(type(item) is int and item == 0 for item in value["binding"]),
            "multi-EKF publisher binding")
    selection = value["selection"]
    selection_keys = {"old_instance", "selected_instance", "instances_available",
                      "instance_changed_count", "selection_us", "last_instance_change_us",
                      "accel_device_id", "gyro_device_id", "publication_succeeded"}
    require(set(selection) == selection_keys, "selection fields")
    for key in selection_keys - {"publication_succeeded"}:
        uint(selection[key])
    require(selection["old_instance"] == 255 and selection["selected_instance"] == 0
            and selection["instances_available"] == 1 and selection["instance_changed_count"] == 1
            and selection["selection_us"] == selection["last_instance_change_us"]
            and lifecycle["estimator_start_us"] <= selection["selection_us"]
            <= lifecycle["stop_intent_us"] and selection["publication_succeeded"] is True,
            "selected-instance transition")
    require(selection["accel_device_id"] == sensor["accelerometer_device_id"]
            and selection["gyro_device_id"] == sensor["gyroscope_device_id"],
            "selected sensor binding")

    statuses = value["statuses"]
    require(type(statuses) is list and 0 < len(statuses) <= 64
            and len(statuses) == counts["status"], "selector status coverage")
    previous_observed = 0
    raw_healthy_true = 0
    for index, row in enumerate(statuses):
        keys = {"sequence", "observed_us", "publication_us", "selected_instance",
                "instances_available", "instance_changed_count", "last_instance_change_us",
                "accel_device_id", "gyro_device_id", "raw_selected_healthy",
                "publication_succeeded"}
        require(set(row) == keys, "selector status fields")
        for key in keys - {"raw_selected_healthy", "publication_succeeded"}:
            uint(row[key])
        require(row["sequence"] == index + 1 and row["selected_instance"] == 0
                and row["instances_available"] == 1 and row["instance_changed_count"] == 1
                and row["last_instance_change_us"] == selection["last_instance_change_us"]
                and row["accel_device_id"] == selection["accel_device_id"]
                and row["gyro_device_id"] == selection["gyro_device_id"]
                and selection["selection_us"] <= row["publication_us"] <= row["observed_us"]
                <= lifecycle["selector_end_us"] and previous_observed <= row["observed_us"]
                and type(row["raw_selected_healthy"]) is bool
                and row["publication_succeeded"] is True, "selector status contract")
        raw_healthy_true += int(row["raw_selected_healthy"])
        previous_observed = row["observed_us"]

    attitudes = value["attitudes"]
    require(type(attitudes) is list and len(attitudes) <= 2048
            and len(attitudes) == counts["attitude"], "selected attitude coverage")
    previous_sample = 0
    previous_publication = 0
    for index, row in enumerate(attitudes):
        keys = {"sequence", "observed_us", "sample_us", "estimator_publication_us",
                "selector_publication_us", "selected_instance", "source_instance",
                "source_reset_counter", "selected_reset_counter", "publication_succeeded"}
        require(set(row) == keys and row["sequence"] == index + 1, "selected attitude fields")
        output_time(row, previous_sample, previous_publication, lifecycle)
        uint(row["source_reset_counter"], 255)
        uint(row["selected_reset_counter"], 255)
        previous_sample = row["sample_us"]
        previous_publication = row["selector_publication_us"]

    local_positions = value["local_positions"]
    require(type(local_positions) is list and 0 < len(local_positions) <= 2048
            and len(local_positions) == counts["local_position"], "selected local-position coverage")
    previous_sample = 0
    previous_publication = 0
    for index, row in enumerate(local_positions):
        keys = {"sequence", "observed_us", "sample_us", "estimator_publication_us",
                "selector_publication_us", "selected_instance", "source_instance",
                "source_acceleration_bits", "selected_acceleration_bits",
                "source_reset_counters", "selected_reset_counters", "publication_succeeded"}
        require(set(row) == keys and row["sequence"] == index + 1, "selected local-position fields")
        output_time(row, previous_sample, previous_publication, lifecycle)
        for name in ("source_acceleration_bits", "selected_acceleration_bits"):
            require(type(row[name]) is list and len(row[name]) == 3, "acceleration payload shape")
            for item in row[name]:
                uint(item, (1 << 32) - 1)
        require(row["source_acceleration_bits"] == row["selected_acceleration_bits"],
                "selector mutated acceleration payload")
        for name in ("source_reset_counters", "selected_reset_counters"):
            require(type(row[name]) is list and len(row[name]) == 4, "reset-counter shape")
            for item in row[name]:
                uint(item, 255)
        previous_sample = row["sample_us"]
        previous_publication = row["selector_publication_us"]
    return dict(case=case, selection_transition=[255, 0], statuses=len(statuses),
                raw_healthy_true_observations=raw_healthy_true, attitudes=len(attitudes),
                local_positions=len(local_positions), acceleration_payloads_preserved=len(local_positions),
                binding=value["binding"], health_policy_configured=False, health_qualified=False,
                failover_qualified=False, positive_admission=False, eligible_for_control=False)


def feeder_trace(value, run_id, sensor):
    keys = {"schema", "run_id", "fault", "input_count", "published_count", "vehicle_imu_instance",
            "accel_device_id", "gyro_device_id", "delta_transform", "actuator_publications",
            "publication_timestamps"}
    require(set(value) == keys and value["schema"] == "p2g-vehicle-imu-feeder/v1"
            and value["run_id"] == run_id, "feeder binding")
    for key in ("fault", "input_count", "published_count", "vehicle_imu_instance",
                "accel_device_id", "gyro_device_id"):
        uint(value[key])
    require(value["fault"] == 0 and value["input_count"] == 1200
            and value["published_count"] == 1200 and value["vehicle_imu_instance"] == 0
            and value["accel_device_id"] == sensor["accelerometer_device_id"]
            and value["gyro_device_id"] == sensor["gyroscope_device_id"]
            and value["delta_transform"] is True and value["actuator_publications"] is False,
            "feeder contract")
    timestamps = value["publication_timestamps"]
    require(type(timestamps) is list and len(timestamps) == 1200, "feeder timestamp coverage")
    previous = 0
    for timestamp in timestamps:
        uint(timestamp)
        require(previous < timestamp, "feeder timestamp chronology")
        previous = timestamp
    return dict(input_count=1200, published_count=1200, vehicle_imu_instance=0,
                actuator_publications=False)


def configuration_arguments(configuration):
    if configuration == "Debug":
        return ["-DCMAKE_BUILD_TYPE=Debug"]
    if configuration == "Release":
        return ["-DCMAKE_BUILD_TYPE=Release"]
    sanitizer = "-fsanitize=address,undefined -fno-omit-frame-pointer"
    return ["-DCMAKE_BUILD_TYPE=Debug", f"-DCMAKE_C_FLAGS={sanitizer}",
            f"-DCMAKE_CXX_FLAGS={sanitizer}", "-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined"]


def check(area):
    area = Path(area).resolve()
    manifest = read(area / "manifest.json")
    require(manifest["schema"] == "p2g-selector-build/v1"
            and manifest["status"] == "captured_unqualified", "incomplete P2G build/run")
    require(manifest["configuration"] in ("Debug", "Release", "Sanitize"), "unknown configuration")
    require(sha(area / "source_hashes.json") == manifest["source_hashes_sha256"], "source registry drift")
    registry = read(area / "source_hashes.json", 16 * 1024 * 1024)
    require(set(manifest["overlay"]) == OVERLAYS, "overlay inventory")
    for rel, hashes in manifest["overlay"].items():
        require(registry[rel] == hashes["original"] and sha(area / "source" / rel) == hashes["derived"],
                "overlay drift")
    expected_inputs = {rel for rel in registry if rel.startswith(PREFIX + "selector_state/")}
    actual_inputs = {str(path.relative_to(area / "inputs"))
                     for path in (area / "inputs").rglob("*") if path.is_file()}
    require(actual_inputs == expected_inputs
            and PREFIX + "selector_state/SelectorState.cpp" in expected_inputs,
            "P2G source archive inventory")
    for rel in expected_inputs:
        require(sha(area / "inputs" / rel) == registry[rel], "archived P2G input drift")

    baseline = manifest["baseline"]
    require(set(baseline) == {"path", "manifest_sha256", "verification", "fixture_sha256"},
            "baseline binding fields")
    baseline_path = Path(baseline["path"])
    require(sha(baseline_path / "manifest.json") == baseline["manifest_sha256"], "P2F manifest drift")
    baseline_check = p2f_check.check(baseline_path)
    require(baseline_check == baseline["verification"] and baseline_check["verified"],
            "P2F verification drift")
    baseline_manifest = read(baseline_path / "manifest.json")
    sensor = manifest["sensor_provenance"]
    require(sensor == baseline_manifest["sensor_provenance"]
            and sha(baseline_path / "run-normal/imu.txt") == baseline["fixture_sha256"],
            "P2F sensor/fixture drift")

    build = area / "build"
    source = area / "source"
    require(sha(build / "bin/px4") == manifest["executable_sha256"], "executable drift")
    selector_prefix = PREFIX + "selector_state/"
    configure = ["cmake", "-S", str(source), "-B", str(build), "-G", "Ninja",
                 "-DCONFIG=px4_sitl_p2g-default", "-DVENDOR=px4", "-DMODEL=sitl",
                 "-DLABEL=p2g-default", f"-DPX4_BOARD_DIR={source}/boards/px4/sitl",
                 f"-DPX4_CONFIG_FILE={source}/{selector_prefix}p2g-default.px4board",
                 f"-DEXTERNAL_MODULES_LOCATION={source}/{selector_prefix}px4_external",
                 "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"] + configuration_arguments(manifest["configuration"])
    expected_commands = {"configure": configure,
                         "build": ["cmake", "--build", str(build), "--target", "px4", "-j", "4"]}
    for name in ("configure", "build"):
        require(manifest[name]["command"] == expected_commands[name]
                and manifest[name]["exit_code"] == 0
                and sha(area / (name + ".log")) == manifest[name]["log_sha256"],
                "build-command binding")
    audit = manifest["configuration_audit"]
    require(sha(build / "boardconfig") == audit["boardconfig_sha256"]
            and sha(build / "compile_commands.json") == audit["compile_commands_sha256"]
            and audit["selector_enabled"] is True and audit["admission"] == "not_linked"
            and audit["control_modules"] == "not_linked" and audit["output_drivers"] == "not_linked",
            "configuration drift")
    config = {line.split("=", 1)[0] for line in (build / "boardconfig").read_text().splitlines()
              if line.endswith("=y")}
    require({item for item in config if item.startswith("CONFIG_MODULES_")} == {"CONFIG_MODULES_EKF2"}
            and not any(item.startswith("CONFIG_DRIVERS_") for item in config)
            and "CONFIG_EKF2_MULTI_INSTANCE" in config and "CONFIG_BOARD_NOLOCKSTEP" in config,
            "nonclosed P2G profile")
    apps = set(re.findall(r'apps\["([a-zA-Z0-9_]+)"\]',
                          (build / "platforms/posix/apps.cpp").read_text()))
    require(apps == BUILTINS and set(audit["builtins"]) == BUILTINS, "builtin inventory")
    commands = read(build / "compile_commands.json", 16 * 1024 * 1024)
    forbidden = ("-DENABLE_LOCKSTEP_SCHEDULER", "-DCONFIG_MUORB_APPS_SYNC_TIMESTAMP",
                 "RESEARCH_C1E_NEGATIVE", "RESEARCH_SITL_DIAGNOSTIC")
    for command in commands:
        require(not any(token in command["command"] for token in forbidden), "mixed P2G profile")
    for ending in ("/SelectorState.cpp", "/EKF2.cpp", "/EKF2Selector.cpp",
                   "/research_selector_provenance.cpp"):
        selected = [command for command in commands if command["file"].endswith(ending)]
        require(len(selected) == 1 and "-DRESEARCH_P2G_SELECTOR_STATE=1" in selected[0]["command"]
                and "-DRESEARCH_P2G_MULTI_SELECTOR=1" in selected[0]["command"],
                "missing P2G profile definition")

    require(len(manifest["runs"]) == 2
            and [run["case"] for run in manifest["runs"]] == ["normal", "restart"],
            "case inventory")
    require(manifest["runs"][0]["run_id"] != manifest["runs"][1]["run_id"],
            "reused process identity")
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
                and run["sensor_provenance"] == sensor
                and run["fixture_sha256"] == baseline["fixture_sha256"], "unclean P2G process")
        artifacts = {"startup.sh", "imu.txt", "stdout.log", "selector.json", "feeder.json"}
        require(set(run["artifacts"]) == artifacts, "case artifact inventory")
        for name, digest in run["artifacts"].items():
            require(sha(runtime / name) == digest, "case artifact drift")
        require(sha(runtime / "startup.sh") == registry[selector_prefix + case + ".sh"]
                and sha(runtime / "imu.txt") == baseline["fixture_sha256"], "runtime input drift")
        log = (runtime / "stdout.log").read_text()
        marker = "P2G_CAPTURE_COMPLETE" if case == "normal" else "P2G_EXPECTED_RESTART_REJECTION"
        require(marker in log and "ERROR [" not in log
                and "P2G_UNEXPECTED_RESTART_ACCEPTANCE" not in log, "case completion")
        selector_result = selector_trace(read(runtime / "selector.json"), run["run_id"], case, sensor)
        selector_result["feeder"] = feeder_trace(read(runtime / "feeder.json"), run["run_id"], sensor)
        results.append(selector_result)
    return dict(verified=True, configuration=manifest["configuration"],
                scope="selector_enabled_selected_output_observation_only", cases=results,
                manifest_sha256=sha(area / "manifest.json"), health_policy_configured=False,
                health_qualified=False, failover_qualified=False, positive_admission=False,
                eligible_for_control=False)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("area", type=Path)
    print(json.dumps(check(parser.parse_args().area), indent=2, sort_keys=True))
