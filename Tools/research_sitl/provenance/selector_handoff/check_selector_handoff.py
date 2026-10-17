#!/usr/bin/env python3
"""Independently check closed P2J two-EKF selector handoff evidence."""

import argparse
import importlib.util
import json
import math
from pathlib import Path
import re
import struct


HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
PREFIX = "Tools/research_sitl/provenance/"
runner_spec = importlib.util.spec_from_file_location("p2j_run", HERE / "run_selector_handoff.py")
runner = importlib.util.module_from_spec(runner_spec)
runner_spec.loader.exec_module(runner)
p2i_check = runner.p2i_check
base = runner.base
require = runner.require
read = p2i_check.read
sha = p2i_check.sha
uint = p2i_check.uint
BUILTINS = runner.BUILTINS


def false_authority(value):
    for key in ("health_policy_configured", "health_qualified", "failover_qualified",
                "complete_mutation_coverage", "deployment_sensors_qualified",
                "positive_admission", "eligible_for_control", "actuator_publications"):
        require(value.get(key) is False, "invented P2J authority: " + key)


def bits(value, size, label):
    require(type(value) is list and len(value) == size, label + " shape")
    decoded = []
    for item in value:
        uint(item, (1 << 32) - 1)
        number = struct.unpack("<f", struct.pack("<I", item))[0]
        require(math.isfinite(number), label + " nonfinite")
        decoded.append(number)
    return decoded


def feeder_trace(value, run_id, sensor):
    keys = {"schema", "run_id", "fault", "input_count", "published_count",
            "vehicle_imu_instances", "split_index", "accelerometer_device_ids",
            "gyroscope_device_ids", "duplicated_recorded_sensor_payload",
            "sensor_diversity", "deployment_sensors_qualified", "pair_status",
            "handoff_status", "actuator_publications", "publication_timestamps"}
    require(set(value) == keys and value["schema"] == "p2j-multi-imu-feeder/v1"
            and value["run_id"] == run_id, "multi-IMU trace binding")
    uint(value["fault"])
    uint(value["input_count"], 1200)
    uint(value["split_index"], 1200)
    require(value["fault"] == 0 and value["input_count"] == 1200
            and value["published_count"] == [1200, 1200]
            and value["vehicle_imu_instances"] == [0, 1]
            and 1 < value["split_index"] < 1200, "multi-IMU coverage")
    accel = sensor["accelerometer_device_id"]
    gyro = sensor["gyroscope_device_id"]
    require(value["accelerometer_device_ids"] == [accel, accel]
            and value["gyroscope_device_ids"] == [gyro, gyro]
            and value["duplicated_recorded_sensor_payload"] is True
            and value["sensor_diversity"] is False
            and value["deployment_sensors_qualified"] is False
            and value["actuator_publications"] is False,
            "invented sensor diversity/deployment authority")

    pair = value["pair_status"]
    handoff = value["handoff_status"]
    status_keys = {"timestamp", "primary_instance", "instances_available",
                   "instance_changed_count", "healthy"}
    require(set(pair) == status_keys and set(handoff) == status_keys,
            "feeder selector-status fields")
    for status in (pair, handoff):
        uint(status["timestamp"])
        uint(status["primary_instance"], 1)
        uint(status["instances_available"], 2)
        uint(status["instance_changed_count"])
        require(type(status["healthy"]) is list and len(status["healthy"]) == 2
                and all(type(item) is bool for item in status["healthy"]),
                "feeder health observation shape")
    require(pair["timestamp"] > 0 and pair["primary_instance"] == 0
            and pair["instances_available"] == 2 and pair["instance_changed_count"] == 1
            and pair["healthy"] == [True, True]
            and handoff["timestamp"] > pair["timestamp"]
            and handoff["primary_instance"] == 1
            and handoff["instances_available"] == 2
            and handoff["instance_changed_count"] == 2
            and handoff["healthy"] == [False, True], "feeder handoff chronology")

    timestamps = value["publication_timestamps"]
    require(type(timestamps) is list and len(timestamps) == 2
            and all(type(row) is list and len(row) == 1200 for row in timestamps),
            "multi-IMU timestamp shape")
    for publisher, row in enumerate(timestamps):
        previous = 0
        for timestamp in row:
            uint(timestamp)
            require(timestamp > previous, "nonmonotonic IMU publisher chronology")
            previous = timestamp
        if publisher == 1:
            require(all(first <= second for first, second in zip(timestamps[0], row)),
                    "dual-IMU publication order")
    return dict(inputs=1200, publications=2400, split_index=value["split_index"],
                duplicated_recorded_sensor_payload=True, sensor_diversity=False,
                deployment_sensors_qualified=False, actuator_publications=False)


def output_time(row, previous_sample, previous_publication, lifecycle):
    for key in ("sequence", "observed_us", "sample_us", "estimator_publication_us",
                "selector_publication_us", "selected_instance", "source_instance"):
        uint(row[key])
    require(previous_sample < row["sample_us"] <= row["estimator_publication_us"]
            <= row["selector_publication_us"] <= row["observed_us"]
            <= lifecycle["selector_end_us"]
            and previous_publication <= row["selector_publication_us"],
            "selected-output chronology")


def handoff_trace(value, run_id, case, sensor):
    keys = {"schema", "run_id", "fault", "counts", "lifecycle", "bindings",
            "health_policy_configured", "health_qualified", "failover_qualified",
            "complete_mutation_coverage", "deployment_sensors_qualified",
            "positive_admission", "eligible_for_control", "actuator_publications",
            "selections", "statuses", "attitudes", "local_positions"}
    require(set(value) == keys and value["schema"] == "p2j-selector-handoff/v1"
            and value["run_id"] == run_id, "handoff trace binding")
    false_authority(value)
    uint(value["fault"])
    require(value["fault"] == (0 if case == "normal" else 4), "handoff lifecycle fault")
    counts = value["counts"]
    require(set(counts) == {"selection", "status", "attitude", "local_position"},
            "handoff count fields")
    for count in counts.values():
        uint(count, 2048)
        require(count > 0, "missing handoff evidence")
    require(counts["selection"] == 2, "unexpected selector transition count")
    require(value["bindings"] == [[0, 0, 0, 0, 0], [1, 1, 1, 1, 1]],
            "actual EKF/uORB/IMU instance binding")

    lifecycle = value["lifecycle"]
    lifecycle_keys = {"selector_start_us", "stop_intent_us", "selector_end_us",
                      "estimator_start_us", "estimator_end_us"}
    require(set(lifecycle) == lifecycle_keys, "handoff lifecycle fields")
    for key in ("selector_start_us", "stop_intent_us", "selector_end_us"):
        uint(lifecycle[key])
    for key in ("estimator_start_us", "estimator_end_us"):
        require(type(lifecycle[key]) is list and len(lifecycle[key]) == 2,
                "estimator lifecycle shape")
        for timestamp in lifecycle[key]:
            uint(timestamp)
    starts = lifecycle["estimator_start_us"]
    ends = lifecycle["estimator_end_us"]
    require(0 < lifecycle["selector_start_us"] <= starts[0] <= starts[1]
            < ends[0] < lifecycle["stop_intent_us"] <= lifecycle["selector_end_us"]
            <= ends[1], "handoff lifecycle chronology")

    selections = value["selections"]
    require(type(selections) is list and len(selections) == counts["selection"],
            "selection coverage")
    selection_keys = {"sequence", "observed_us", "selection_us", "instance_changed_count",
                      "accelerometer_device_id", "gyroscope_device_id", "old_instance",
                      "new_instance", "available_instances", "requested_instance",
                      "old_healthy", "old_timeout", "new_healthy", "new_timeout",
                      "publication_succeeded"}
    for index, row in enumerate(selections):
        require(set(row) == selection_keys, "selection fields")
        for key in selection_keys - {"old_healthy", "old_timeout", "new_healthy",
                                     "new_timeout", "publication_succeeded"}:
            uint(row[key])
        require(row["sequence"] == index + 1 and row["selection_us"] <= row["observed_us"]
                and row["accelerometer_device_id"] == sensor["accelerometer_device_id"]
                and row["gyroscope_device_id"] == sensor["gyroscope_device_id"]
                and all(type(row[key]) is bool for key in
                        ("old_healthy", "old_timeout", "new_healthy", "new_timeout",
                         "publication_succeeded"))
                and row["publication_succeeded"] is True, "selection binding")
    initial, switched = selections
    require(initial["old_instance"] == 255 and initial["new_instance"] == 0
            and initial["available_instances"] == 2 and initial["requested_instance"] == 255
            and initial["instance_changed_count"] == 1, "initial selector binding")
    require(switched["old_instance"] == 0 and switched["new_instance"] == 1
            and switched["available_instances"] == 2 and switched["requested_instance"] == 255
            and switched["instance_changed_count"] == 2
            and switched["old_healthy"] is False and switched["old_timeout"] is True
            and switched["new_healthy"] is True and switched["new_timeout"] is False
            and ends[0] <= switched["selection_us"] < lifecycle["stop_intent_us"],
            "automatic timeout handoff")

    statuses = value["statuses"]
    require(type(statuses) is list and len(statuses) == counts["status"],
            "status coverage")
    status_keys = {"sequence", "observed_us", "publication_us", "last_instance_change_us",
                   "instance_changed_count", "accelerometer_device_id", "gyroscope_device_id",
                   "combined_test_ratio_bits", "primary_instance", "available_instances",
                   "healthy", "publication_succeeded"}
    previous_publication = 0
    pair_status = None
    switched_status = None
    for index, row in enumerate(statuses):
        require(set(row) == status_keys, "status fields")
        for key in status_keys - {"combined_test_ratio_bits", "healthy",
                                  "publication_succeeded"}:
            uint(row[key])
        ratios = bits(row["combined_test_ratio_bits"], 2, "combined test ratio")
        require(row["sequence"] == index + 1
                and previous_publication < row["publication_us"] <= row["observed_us"]
                <= lifecycle["selector_end_us"]
                and row["last_instance_change_us"] <= row["publication_us"]
                and row["accelerometer_device_id"] == sensor["accelerometer_device_id"]
                and row["gyroscope_device_id"] == sensor["gyroscope_device_id"]
                and row["primary_instance"] in (0, 1) and row["available_instances"] == 2
                and type(row["healthy"]) is list and len(row["healthy"]) == 2
                and all(type(item) is bool for item in row["healthy"])
                and row["publication_succeeded"] is True
                and all(ratio > 0 for ratio in ratios), "selector-status binding")
        if (row["primary_instance"] == 0 and row["healthy"] == [True, True]
                and row["instance_changed_count"] == 1):
            pair_status = row
        if (row["primary_instance"] == 1 and row["healthy"] == [False, True]
                and row["instance_changed_count"] == 2):
            switched_status = row
        previous_publication = row["publication_us"]
    require(pair_status is not None and switched_status is not None
            and pair_status["publication_us"] < switched_status["publication_us"],
            "missing raw-health/status handoff observations")

    attitudes = value["attitudes"]
    require(type(attitudes) is list and len(attitudes) == counts["attitude"],
            "attitude coverage")
    attitude_keys = {"sequence", "observed_us", "sample_us", "estimator_publication_us",
                     "selector_publication_us", "source_quaternion_bits",
                     "selected_quaternion_bits", "source_delta_quaternion_bits",
                     "selected_delta_quaternion_bits", "selected_instance", "source_instance",
                     "source_reset_counter", "selected_reset_counter", "publication_succeeded"}
    previous_sample = 0
    previous_publication = 0
    previous_instance = 0
    previous_reset = 0
    attitude_switches = 0
    attitude_counts = [0, 0]
    for index, row in enumerate(attitudes):
        require(set(row) == attitude_keys and row["sequence"] == index + 1,
                "attitude fields")
        output_time(row, previous_sample, previous_publication, lifecycle)
        source_q = bits(row["source_quaternion_bits"], 4, "source quaternion")
        selected_q = bits(row["selected_quaternion_bits"], 4, "selected quaternion")
        bits(row["source_delta_quaternion_bits"], 4, "source attitude reset")
        bits(row["selected_delta_quaternion_bits"], 4, "selected attitude reset")
        uint(row["source_reset_counter"], 255)
        uint(row["selected_reset_counter"], 255)
        require(row["selected_instance"] in (0, 1)
                and row["source_instance"] == row["selected_instance"]
                and row["publication_succeeded"] is True
                and row["source_quaternion_bits"] == row["selected_quaternion_bits"]
                and source_q == selected_q, "selected attitude payload/binding")
        if index == 0:
            require(row["selected_instance"] == 0
                    and row["source_reset_counter"] == row["selected_reset_counter"],
                    "initial attitude instance/reset")
        elif row["selected_instance"] != previous_instance:
            attitude_switches += 1
            require(previous_instance == 0 and row["selected_instance"] == 1
                    and row["selected_reset_counter"] == (previous_reset + 1) % 256,
                    "attitude handoff reset barrier")
        else:
            require(row["selected_instance"] == previous_instance,
                    "attitude instance regression")
        attitude_counts[row["selected_instance"]] += 1
        previous_sample = row["sample_us"]
        previous_publication = row["selector_publication_us"]
        previous_instance = row["selected_instance"]
        previous_reset = row["selected_reset_counter"]
    require(attitude_switches == 1 and all(attitude_counts), "attitude handoff coverage")

    local_positions = value["local_positions"]
    require(type(local_positions) is list and len(local_positions) == counts["local_position"],
            "local-position coverage")
    local_keys = {"sequence", "observed_us", "sample_us", "estimator_publication_us",
                  "selector_publication_us", "source_acceleration_bits",
                  "selected_acceleration_bits", "source_reset_counters",
                  "selected_reset_counters", "selected_instance", "source_instance",
                  "publication_succeeded"}
    previous_sample = 0
    previous_publication = 0
    previous_instance = 0
    previous_resets = None
    local_switches = 0
    local_counts = [0, 0]
    for index, row in enumerate(local_positions):
        require(set(row) == local_keys and row["sequence"] == index + 1,
                "local-position fields")
        output_time(row, previous_sample, previous_publication, lifecycle)
        bits(row["source_acceleration_bits"], 3, "source acceleration")
        bits(row["selected_acceleration_bits"], 3, "selected acceleration")
        for key in ("source_reset_counters", "selected_reset_counters"):
            require(type(row[key]) is list and len(row[key]) == 6, "local reset shape")
            for counter in row[key]:
                uint(counter, 255)
        require(row["selected_instance"] in (0, 1)
                and row["source_instance"] == row["selected_instance"]
                and row["publication_succeeded"] is True
                and row["source_acceleration_bits"] == row["selected_acceleration_bits"],
                "selected acceleration payload/binding")
        if index == 0:
            require(row["selected_instance"] == 0
                    and row["source_reset_counters"] == row["selected_reset_counters"],
                    "initial local-position instance/reset")
        elif row["selected_instance"] != previous_instance:
            local_switches += 1
            require(previous_instance == 0 and row["selected_instance"] == 1
                    and row["selected_reset_counters"]
                    == [((counter + 1) % 256) for counter in previous_resets],
                    "local-position handoff reset barrier")
        else:
            require(row["selected_instance"] == previous_instance,
                    "local-position instance regression")
        local_counts[row["selected_instance"]] += 1
        previous_sample = row["sample_us"]
        previous_publication = row["selector_publication_us"]
        previous_instance = row["selected_instance"]
        previous_resets = row["selected_reset_counters"]
    require(local_switches == 1 and all(local_counts), "local-position handoff coverage")
    return dict(selections=2, automatic_timeout_handoffs=1, statuses=len(statuses),
                attitude_publications=len(attitudes), attitude_by_instance=attitude_counts,
                local_position_publications=len(local_positions),
                local_position_by_instance=local_counts, attitude_reset_barriers=1,
                local_position_reset_barriers=1, actual_ekf_instances=2,
                health_policy_configured=False, health_qualified=False,
                failover_qualified=False, complete_mutation_coverage=False,
                deployment_sensors_qualified=False, positive_admission=False,
                eligible_for_control=False, actuator_publications=False)


def check(area):
    area = area.resolve()
    manifest = read(area / "manifest.json")
    require(manifest["schema"] == "p2j-selector-handoff-build/v1"
            and manifest["status"] == "captured_unqualified"
            and manifest["source"] == str(ROOT)
            and manifest["px4_head"] == runner.base.git("rev-parse", "HEAD")
            and manifest["configuration"] in ("Debug", "Release", "Sanitize"),
            "manifest identity/status")
    registry = read(area / "source_hashes.json", 16 * 1024 * 1024)
    require(sha(area / "source_hashes.json") == manifest["source_hashes_sha256"],
            "source registry hash")
    baseline = manifest["baseline"]
    baseline_path = Path(baseline["path"])
    baseline_result = p2i_check.check(baseline_path)
    require(baseline_result == baseline["verification"] and baseline_result["verified"]
            and sha(baseline_path / "manifest.json") == baseline["manifest_sha256"],
            "P2I verification drift")
    baseline_manifest = read(baseline_path / "manifest.json")
    for rel, hashes in manifest["overlay"].items():
        require(registry[rel] == hashes["original"]
                and sha(area / "source" / rel) == hashes["p2j_derived"], "overlay drift")
    prefixes = tuple(PREFIX + name + "/" for name in ("selected_attitude", "selector_handoff"))
    expected_inputs = {rel for rel in registry if rel.startswith(prefixes)}
    actual_inputs = {str(path.relative_to(area / "inputs"))
                     for path in (area / "inputs").rglob("*") if path.is_file()}
    require(actual_inputs == expected_inputs
            and PREFIX + "selector_handoff/SelectorHandoff.cpp" in expected_inputs
            and PREFIX + "selector_handoff/check_selector_handoff.py" in expected_inputs,
            "P2J source archive inventory")
    for rel in expected_inputs:
        require(sha(area / "inputs" / rel) == registry[rel], "archived P2J input drift")
    context_rows = p2i_check.context_fixture(area / "system_state.txt",
                                             manifest["system_state_provenance"])
    sensor = manifest["sensor_provenance"]
    require(sensor == baseline_manifest["sensor_provenance"]
            and sha(baseline_path / "run-normal/imu.txt") == baseline["fixture_sha256"],
            "P2I sensor/fixture drift")

    build = area / "build"
    source = area / "source"
    require(sha(build / "bin/px4") == manifest["executable_sha256"], "executable drift")
    selected_prefix = PREFIX + "selector_handoff/"
    configure = ["cmake", "-S", str(source), "-B", str(build), "-G", "Ninja",
                 "-DCONFIG=px4_sitl_p2j-default", "-DVENDOR=px4", "-DMODEL=sitl",
                 "-DLABEL=p2j-default", f"-DPX4_BOARD_DIR={source}/boards/px4/sitl",
                 f"-DPX4_CONFIG_FILE={source}/{selected_prefix}p2j-default.px4board",
                 f"-DEXTERNAL_MODULES_LOCATION={source}/{selected_prefix}px4_external",
                 "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"] + runner.p2i.p2h.p2g.configuration_arguments(
                     manifest["configuration"])
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
            and audit["selector_enabled"] is True and audit["actual_ekf_instances"] == 2
            and audit["recorded_system_state"] is True
            and audit["admission"] == "not_linked" and audit["control_modules"] == "not_linked"
            and audit["output_drivers"] == "not_linked", "configuration drift")
    config = {line.split("=", 1)[0] for line in (build / "boardconfig").read_text().splitlines()
              if line.endswith("=y")}
    require({item for item in config if item.startswith("CONFIG_MODULES_")} == {"CONFIG_MODULES_EKF2"}
            and not any(item.startswith("CONFIG_DRIVERS_") for item in config)
            and "CONFIG_EKF2_MULTI_INSTANCE" in config and "CONFIG_BOARD_NOLOCKSTEP" in config,
            "nonclosed P2J profile")
    apps = set(re.findall(r'apps\["([a-zA-Z0-9_]+)"\]',
                          (build / "platforms/posix/apps.cpp").read_text()))
    require(apps == BUILTINS and set(audit["builtins"]) == BUILTINS, "builtin inventory")
    commands = read(build / "compile_commands.json", 16 * 1024 * 1024)
    for command in commands:
        require(not any(token in command["command"] for token in
                        ("-DENABLE_LOCKSTEP_SCHEDULER", "-DCONFIG_MUORB_APPS_SYNC_TIMESTAMP",
                         "RESEARCH_C1E_NEGATIVE", "RESEARCH_SITL_DIAGNOSTIC")),
                "mixed P2J profile")
    for ending in ("/SelectorHandoff.cpp", "/research_multi_imu.cpp", "/EKF2.cpp",
                   "/EKF2Selector.cpp"):
        selected = [command for command in commands if command["file"].endswith(ending)]
        require(len(selected) == 1
                and "-DRESEARCH_P2J_SELECTOR_HANDOFF=1" in selected[0]["command"],
                "missing P2J profile definition")

    require(len(manifest["runs"]) == 2
            and [run["case"] for run in manifest["runs"]] == ["normal", "restart"]
            and manifest["runs"][0]["run_id"] != manifest["runs"][1]["run_id"],
            "case inventory/identity")
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
                and run["fixture_sha256"] == baseline["fixture_sha256"]
                and run["system_state_fixture_sha256"]
                == manifest["system_state_provenance"]["fixture_sha256"],
                "unclean P2J process/input")
        artifacts = {"startup.sh", "imu.txt", "system_state.txt", "stdout.log",
                     "feeder.json", "system_state.json", "handoff.json"}
        require(set(run["artifacts"]) == artifacts, "case artifact inventory")
        for name, digest in run["artifacts"].items():
            require(sha(runtime / name) == digest, "case artifact drift")
        require(sha(runtime / "startup.sh") == registry[selected_prefix + case + ".sh"]
                and sha(runtime / "imu.txt") == baseline["fixture_sha256"]
                and p2i_check.context_fixture(runtime / "system_state.txt",
                                              manifest["system_state_provenance"])
                == context_rows, "runtime input drift")
        log = (runtime / "stdout.log").read_text()
        marker = "P2J_CAPTURE_COMPLETE" if case == "normal" else "P2J_EXPECTED_RESTART_REJECTION"
        require(marker in log and "ERROR [" not in log and "P2J_UNEXPECTED" not in log
                and "primary EKF changed 0 (timeout) -> 1" in log, "case completion")
        feeder = feeder_trace(read(runtime / "feeder.json"), run["run_id"], sensor)
        handoff = handoff_trace(read(runtime / "handoff.json"), run["run_id"], case, sensor)
        state = p2i_check.system_state_trace(read(runtime / "system_state.json"), context_rows)
        results.append(dict(case=case, feeder=feeder, handoff=handoff, system_state=state,
                            health_policy_configured=False, health_qualified=False,
                            failover_qualified=False, complete_mutation_coverage=False,
                            deployment_sensors_qualified=False, positive_admission=False,
                            eligible_for_control=False, actuator_publications=False))
    return dict(verified=True, configuration=manifest["configuration"],
                scope="actual_two_ekf_timeout_handoff_observation_only", cases=results,
                manifest_sha256=sha(area / "manifest.json"), health_policy_configured=False,
                health_qualified=False, failover_qualified=False,
                complete_mutation_coverage=False, deployment_sensors_qualified=False,
                positive_admission=False, eligible_for_control=False,
                actuator_publications=False)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("area", type=Path)
    print(json.dumps(check(parser.parse_args().area), indent=2, sort_keys=True))
