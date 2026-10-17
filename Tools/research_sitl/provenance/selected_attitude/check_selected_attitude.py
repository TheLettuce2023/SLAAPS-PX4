#!/usr/bin/env python3
"""Independently check P2I recorded-state and valid selected-attitude provenance."""

import argparse
import importlib.util
import json
import math
from pathlib import Path
import re
import struct

from pyulog import ULog


HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
PREFIX = "Tools/research_sitl/provenance/"
p2h_spec = importlib.util.spec_from_file_location(
    "p2h_check", HERE.parent / "selector_disposition/check_selector_disposition.py")
p2h_check = importlib.util.module_from_spec(p2h_spec)
p2h_spec.loader.exec_module(p2h_check)
runner_spec = importlib.util.spec_from_file_location("p2i_run", HERE / "run_selected_attitude.py")
runner = importlib.util.module_from_spec(runner_spec)
runner_spec.loader.exec_module(runner)
p2g_check = p2h_check.p2g_check
require = p2h_check.require
read = p2h_check.read
sha = p2h_check.sha
uint = p2h_check.uint
OVERLAYS = p2h_check.OVERLAYS
BUILTINS = p2h_check.BUILTINS | {"research_selected_attitude"}


def authority(value):
    p2g_check.authority(value)


def bit_array(value, size, label):
    require(type(value) is list and len(value) == size, label + " shape")
    for item in value:
        uint(item, (1 << 32) - 1)
        require(math.isfinite(struct.unpack("<f", struct.pack("<I", item))[0]),
                label + " nonfinite")


def context_rows_from_ulog(path, expected_hash):
    require(sha(path) == expected_hash, "recorded ULog drift")
    ulog = ULog(str(path), message_name_filter_list=[
        "sensor_combined", "vehicle_status", "vehicle_land_detected"])
    require(not ulog.file_corruption, "corrupt recorded ULog")
    imu = runner.dataset(ulog, "sensor_combined")
    timestamps = [int(value) for value in imu["timestamp"][:1200]]
    require(len(timestamps) == 1200 and all(current - previous == 4000
                                            for previous, current in zip(timestamps, timestamps[1:])),
            "recorded IMU chronology")
    first_us, last_us = timestamps[0], timestamps[-1]
    statuses = runner.selected_rows(runner.dataset(ulog, "vehicle_status"), "S",
                                    runner.STATUS_FIELDS, first_us, last_us)
    lands = runner.selected_rows(runner.dataset(ulog, "vehicle_land_detected"), "L",
                                 runner.LAND_FIELDS, first_us, last_us)
    return first_us, last_us, sorted(statuses + lands, key=lambda row: (row[0], row[1])), statuses, lands


def context_fixture(path, provenance):
    lines = path.read_text().splitlines()
    require(lines, "empty system-state fixture")
    header = lines[0].split()
    require(len(header) == 8 and header[0] == "M", "system-state header")
    metadata = [int(value) for value in header[1:]]
    require(all(value >= 0 for value in metadata), "negative system-state metadata")
    first_us, last_us, step_us, imu_count, status_count, land_count, total = metadata
    source_path = Path(provenance["source_path"])
    expected_first, expected_last, expected_rows, statuses, lands = context_rows_from_ulog(
        source_path, provenance["source_sha256"])
    require([first_us, last_us, step_us, imu_count, status_count, land_count, total]
            == [expected_first, expected_last, 4000, 1200, len(statuses), len(lands),
                len(expected_rows)], "system-state metadata/source mismatch")
    actual_rows = []
    for line in lines[1:]:
        fields = line.split()
        require(len(fields) >= 2 and fields[0] in ("S", "L"), "system-state row framing")
        expected_fields = runner.STATUS_FIELDS if fields[0] == "S" else runner.LAND_FIELDS
        require(len(fields) == len(expected_fields) + 2, "system-state row width")
        values = tuple(int(value) for value in fields[2:])
        require(all(value >= 0 for value in values), "negative system-state value")
        actual_rows.append((int(fields[1]), fields[0], values))
    require(actual_rows == expected_rows, "system-state payload differs from recorded ULog")
    expected_provenance = {
        "source_path": str(source_path), "source_sha256": provenance["source_sha256"],
        "first_imu_source_us": first_us, "last_imu_source_us": last_us,
        "imu_count": 1200, "step_us": 4000, "status_count": len(statuses),
        "land_count": len(lands), "event_count": len(expected_rows),
        "fixture_sha256": sha(path), "full_payloads_preserved": True,
        "timestamp_transform": "recorded chronology to fresh hrt publication timestamps",
    }
    require(provenance == expected_provenance, "system-state provenance fields")
    return actual_rows


def system_state_trace(value, expected_rows):
    keys = {"schema", "fault", "first_imu_source_us", "last_imu_source_us", "step_us",
            "counts", "published_count", "actuator_publications", "events"}
    require(set(value) == keys and value["schema"] == "p2i-recorded-system-state/v1",
            "system-state trace fields")
    for key in ("fault", "first_imu_source_us", "last_imu_source_us", "step_us",
                "published_count"):
        uint(value[key])
    require(value["fault"] == 0 and value["actuator_publications"] is False,
            "system-state replay fault/authority")
    counts = value["counts"]
    require(set(counts) == {"total", "status", "land"}, "system-state counts")
    require(counts == {"total": len(expected_rows),
                       "status": sum(row[1] == "S" for row in expected_rows),
                       "land": sum(row[1] == "L" for row in expected_rows)}
            and value["published_count"] == len(expected_rows), "system-state coverage")
    require(value["first_imu_source_us"] > 0
            and value["last_imu_source_us"] == value["first_imu_source_us"] + 1199 * value["step_us"]
            and value["step_us"] == 4000, "system-state source window")
    events = value["events"]
    require(type(events) is list and len(events) == len(expected_rows), "system-state events")
    previous_publication = 0
    for index, (event, expected) in enumerate(zip(events, expected_rows)):
        require(set(event) == {"sequence", "kind", "source_us", "publication_us"},
                "system-state event fields")
        for key in ("sequence", "source_us", "publication_us"):
            uint(event[key])
        kind = "vehicle_status" if expected[1] == "S" else "vehicle_land_detected"
        require(event["sequence"] == index + 1 and event["kind"] == kind
                and event["source_us"] == expected[0]
                and previous_publication <= event["publication_us"],
                "system-state event binding")
        previous_publication = event["publication_us"]
    return dict(events=len(events), status=counts["status"], land=counts["land"],
                full_payloads_preserved=True, actuator_publications=False)


def disposition_trace(value, run_id, case, selector):
    expected = {"schema", "run_id", "fault", "counts", "lifecycle", "binding",
                "health_policy_configured", "health_qualified", "failover_qualified",
                "positive_admission", "eligible_for_control", "ekf_attitudes",
                "attitude_dispositions", "local_position_dispositions"}
    require(set(value) == expected and value["schema"] == "p2h-selector-disposition/v1"
            and value["run_id"] == run_id, "disposition trace binding")
    authority(value)
    uint(value["fault"])
    require(value["fault"] == (0 if case == "normal" else 4), "wrong disposition lifecycle fault")
    counts = value["counts"]
    require(set(counts) == {"ekf_attitude", "attitude", "local_position"},
            "disposition counts")
    for count in counts.values():
        uint(count, 2048)
        require(count > 0, "missing disposition observations")
    lifecycle = value["lifecycle"]
    keys = {"selector_start_us", "estimator_start_us", "stop_intent_us",
            "selector_end_us", "estimator_end_us"}
    require(set(lifecycle) == keys, "disposition lifecycle fields")
    for item in lifecycle.values():
        uint(item)
    require(0 < lifecycle["selector_start_us"] <= lifecycle["estimator_start_us"]
            <= lifecycle["stop_intent_us"] <= lifecycle["selector_end_us"]
            <= lifecycle["estimator_end_us"] and value["binding"] == [0, 0],
            "disposition lifecycle/binding")

    ekf_attitudes = value["ekf_attitudes"]
    require(type(ekf_attitudes) is list and len(ekf_attitudes) == counts["ekf_attitude"],
            "EKF attitude-attempt coverage")
    previous_sample = 0
    previous_observed = 0
    valid_count = 0
    for index, row in enumerate(ekf_attitudes):
        row_keys = {"sequence", "observed_us", "sample_us", "estimator_instance",
                    "attitude_valid", "publication_attempted", "publication_succeeded"}
        require(set(row) == row_keys, "EKF attitude-attempt fields")
        for key in row_keys - {"attitude_valid", "publication_attempted", "publication_succeeded"}:
            uint(row[key])
        require(row["sequence"] == index + 1 and row["estimator_instance"] == 0
                and previous_sample < row["sample_us"] <= row["observed_us"]
                and previous_observed <= row["observed_us"] <= lifecycle["estimator_end_us"]
                and type(row["attitude_valid"]) is bool
                and type(row["publication_attempted"]) is bool
                and type(row["publication_succeeded"]) is bool
                and row["publication_attempted"] == row["attitude_valid"]
                and row["publication_succeeded"] == row["publication_attempted"],
                "EKF attitude-attempt contract")
        valid_count += int(row["attitude_valid"])
        previous_sample = row["sample_us"]
        previous_observed = row["observed_us"]
    require(0 < valid_count < len(ekf_attitudes), "missing observed attitude-valid transition")

    attitudes = value["attitude_dispositions"]
    require(type(attitudes) is list and len(attitudes) == counts["attitude"],
            "attitude-disposition coverage")
    previous_observed = 0
    previous_candidate_sample = 0
    attitude_candidates = 0
    published_attitudes = []
    for index, row in enumerate(attitudes):
        row_keys = {"sequence", "observed_us", "sample_us", "source_publication_us",
                    "previous_sample_us", "age_us", "selected_instance", "source_instance",
                    "candidate_available", "monotonic", "fresh", "publish_requested"}
        require(set(row) == row_keys, "attitude-disposition fields")
        for key in row_keys - {"candidate_available", "monotonic", "fresh", "publish_requested"}:
            uint(row[key])
        require(row["sequence"] == index + 1 and row["selected_instance"] == 0
                and row["source_instance"] == 0 and previous_observed <= row["observed_us"]
                <= lifecycle["selector_end_us"], "attitude-disposition chronology")
        for key in ("candidate_available", "monotonic", "fresh", "publish_requested"):
            require(type(row[key]) is bool, "attitude-disposition boolean")
        if row["candidate_available"]:
            attitude_candidates += 1
            require(row["previous_sample_us"] == previous_candidate_sample
                    and 0 < row["sample_us"] <= row["source_publication_us"] <= row["observed_us"]
                    and row["monotonic"] == (row["sample_us"] > row["previous_sample_us"])
                    and row["fresh"] == (row["age_us"] <= 10000)
                    and row["publish_requested"] == (row["monotonic"] and row["fresh"]),
                    "attitude gate disposition")
            previous_candidate_sample = row["sample_us"]
            if row["publish_requested"]:
                published_attitudes.append(row)
        else:
            require(all(row[key] == 0 for key in ("sample_us", "source_publication_us",
                                                  "previous_sample_us", "age_us"))
                    and not row["monotonic"] and not row["fresh"]
                    and not row["publish_requested"], "invented absent attitude candidate")
        previous_observed = row["observed_us"]
    require(attitude_candidates > 0 and len(published_attitudes) == len(selector["attitudes"]),
            "attitude publication/disposition mismatch")
    for disposition, published in zip(published_attitudes, selector["attitudes"]):
        require(disposition["sample_us"] == published["sample_us"],
                "attitude publication chronology mismatch")

    local_positions = value["local_position_dispositions"]
    require(type(local_positions) is list and len(local_positions) == counts["local_position"],
            "local-position disposition coverage")
    previous_observed = 0
    previous_candidate_sample = 0
    local_candidates = 0
    local_published = []
    local_stale = 0
    local_nonmonotonic = 0
    for index, row in enumerate(local_positions):
        row_keys = {"sequence", "observed_us", "sample_us", "source_publication_us",
                    "previous_sample_us", "age_us", "source_acceleration_bits",
                    "selected_instance", "source_instance", "candidate_available",
                    "monotonic", "fresh", "publish_requested"}
        require(set(row) == row_keys, "local-position disposition fields")
        for key in row_keys - {"source_acceleration_bits", "candidate_available", "monotonic",
                               "fresh", "publish_requested"}:
            uint(row[key])
        bit_array(row["source_acceleration_bits"], 3, "acceleration disposition")
        require(row["sequence"] == index + 1 and row["selected_instance"] == 0
                and row["source_instance"] == 0 and previous_observed <= row["observed_us"]
                <= lifecycle["selector_end_us"], "local-position disposition chronology")
        if row["candidate_available"]:
            local_candidates += 1
            require(row["previous_sample_us"] == previous_candidate_sample
                    and 0 < row["sample_us"] <= row["source_publication_us"] <= row["observed_us"]
                    and row["monotonic"] == (row["sample_us"] > row["previous_sample_us"])
                    and row["fresh"] == (row["age_us"] <= 20000)
                    and row["publish_requested"] == (row["monotonic"] and row["fresh"]),
                    "local-position gate disposition")
            previous_candidate_sample = row["sample_us"]
            local_stale += int(not row["fresh"])
            local_nonmonotonic += int(not row["monotonic"])
            if row["publish_requested"]:
                local_published.append(row)
        else:
            require(all(row[key] == 0 for key in ("sample_us", "source_publication_us",
                                                  "previous_sample_us", "age_us"))
                    and row["source_acceleration_bits"] == [0, 0, 0]
                    and not row["monotonic"] and not row["fresh"]
                    and not row["publish_requested"], "invented absent local-position candidate")
        previous_observed = row["observed_us"]
    require(local_candidates > 0 and len(local_published) == len(selector["local_positions"]),
            "local-position publication/disposition mismatch")
    for disposition, published in zip(local_published, selector["local_positions"]):
        require(disposition["sample_us"] == published["sample_us"]
                and disposition["source_acceleration_bits"] == published["source_acceleration_bits"],
                "selected acceleration disposition mismatch")
    return dict(ekf_attitude_attempts=len(ekf_attitudes), ekf_attitude_valid=valid_count,
                attitude_candidate_checks=len(attitudes), attitude_candidates=attitude_candidates,
                attitude_publications=len(published_attitudes),
                local_position_checks=len(local_positions), local_position_candidates=local_candidates,
                local_position_publications=len(local_published), local_position_stale=local_stale,
                local_position_nonmonotonic=local_nonmonotonic)


def selected_trace(value, run_id, case, selector):
    keys = {"schema", "run_id", "fault", "count", "lifecycle", "binding",
            "health_policy_configured", "health_qualified", "failover_qualified",
            "positive_admission", "eligible_for_control", "actuator_publications", "records"}
    require(set(value) == keys and value["schema"] == "p2i-selected-attitude/v1"
            and value["run_id"] == run_id, "selected-attitude trace binding")
    authority(value)
    require(value["actuator_publications"] is False and value["binding"] == [0, 0],
            "selected-attitude authority/binding")
    uint(value["fault"])
    uint(value["count"], 2048)
    require(value["fault"] == (0 if case == "normal" else 4) and value["count"] > 0,
            "selected-attitude lifecycle disposition")
    lifecycle = value["lifecycle"]
    require(set(lifecycle) == {"selector_start_us", "stop_intent_us", "selector_end_us"},
            "selected-attitude lifecycle fields")
    for item in lifecycle.values():
        uint(item)
    require(0 < lifecycle["selector_start_us"] <= lifecycle["stop_intent_us"]
            <= lifecycle["selector_end_us"], "selected-attitude lifecycle chronology")
    records = value["records"]
    require(type(records) is list and len(records) == value["count"]
            and len(records) == len(selector["attitudes"]), "selected-attitude coverage")
    previous_sample = 0
    previous_publication = 0
    for index, (row, inherited) in enumerate(zip(records, selector["attitudes"])):
        row_keys = {"sequence", "observed_us", "sample_us", "estimator_publication_us",
                    "selector_publication_us", "selected_instance", "source_instance",
                    "source_quaternion_bits", "selected_quaternion_bits",
                    "source_delta_quaternion_bits", "selected_delta_quaternion_bits",
                    "source_reset_counter", "selected_reset_counter", "publication_succeeded"}
        require(set(row) == row_keys and row["sequence"] == index + 1,
                "selected-attitude record fields")
        p2g_check.output_time(row, previous_sample, previous_publication,
                              {"estimator_start_us": lifecycle["selector_start_us"],
                               "selector_end_us": lifecycle["selector_end_us"]})
        for field in ("source_quaternion_bits", "selected_quaternion_bits",
                      "source_delta_quaternion_bits", "selected_delta_quaternion_bits"):
            bit_array(row[field], 4, field)
        require(row["source_quaternion_bits"] == row["selected_quaternion_bits"]
                and row["source_delta_quaternion_bits"] == row["selected_delta_quaternion_bits"],
                "selector mutated attitude payload")
        uint(row["source_reset_counter"], 255)
        uint(row["selected_reset_counter"], 255)
        require(row["source_reset_counter"] == row["selected_reset_counter"]
                and all(row[key] == inherited[key] for key in
                        ("sample_us", "estimator_publication_us", "selector_publication_us",
                         "selected_instance", "source_instance", "source_reset_counter",
                         "selected_reset_counter", "publication_succeeded")),
                "selected-attitude/inherited selector mismatch")
        previous_sample = row["sample_us"]
        previous_publication = row["selector_publication_us"]
    return dict(publications=len(records), quaternion_payloads_preserved=len(records),
                reset_payloads_preserved=len(records), actuator_publications=False)


def check(area):
    area = Path(area).resolve()
    manifest = read(area / "manifest.json")
    require(manifest["schema"] == "p2i-selected-attitude-build/v1"
            and manifest["status"] == "captured_unqualified", "incomplete P2I build/run")
    require(manifest["configuration"] in ("Debug", "Release", "Sanitize"),
            "unknown configuration")
    require(sha(area / "source_hashes.json") == manifest["source_hashes_sha256"],
            "source registry drift")
    registry = read(area / "source_hashes.json", 16 * 1024 * 1024)
    require(set(manifest["overlay"]) == OVERLAYS, "overlay inventory")
    baseline = manifest["baseline"]
    baseline_path = Path(baseline["path"])
    require(sha(baseline_path / "manifest.json") == baseline["manifest_sha256"],
            "P2H manifest drift")
    baseline_result = p2h_check.check(baseline_path)
    require(baseline_result == baseline["verification"] and baseline_result["verified"],
            "P2H verification drift")
    baseline_manifest = read(baseline_path / "manifest.json")
    for rel, hashes in manifest["overlay"].items():
        require(registry[rel] == hashes["original"]
                and baseline_manifest["overlay"][rel]["p2h_derived"] == hashes["p2h_derived"]
                and sha(area / "source" / rel) == hashes["p2i_derived"], "overlay drift")
    prefixes = tuple(PREFIX + name + "/" for name in
                     ("selector_state", "selector_disposition", "selected_attitude"))
    expected_inputs = {rel for rel in registry if rel.startswith(prefixes)}
    actual_inputs = {str(path.relative_to(area / "inputs"))
                     for path in (area / "inputs").rglob("*") if path.is_file()}
    require(actual_inputs == expected_inputs
            and PREFIX + "selected_attitude/SelectedAttitude.cpp" in expected_inputs,
            "P2I source archive inventory")
    for rel in expected_inputs:
        require(sha(area / "inputs" / rel) == registry[rel], "archived P2I input drift")
    generated = manifest["generated_feeder"]
    source_feeder_rel = str(runner.SOURCE_FEEDER.relative_to(ROOT))
    require(generated == {"source_sha256": registry[source_feeder_rel],
                          "derived_sha256": sha(area / "source/src/modules/ekf2/"
                                                "research_selector_provenance_p2i.cpp")},
            "generated feeder drift")
    source_state_rows = context_fixture(area / "system_state.txt",
                                        manifest["system_state_provenance"])
    sensor = manifest["sensor_provenance"]
    require(sensor == baseline_manifest["sensor_provenance"]
            and sha(baseline_path / "run-normal/imu.txt") == baseline["fixture_sha256"],
            "P2H sensor/fixture drift")

    build = area / "build"
    source = area / "source"
    require(sha(build / "bin/px4") == manifest["executable_sha256"], "executable drift")
    selected_prefix = PREFIX + "selected_attitude/"
    configure = ["cmake", "-S", str(source), "-B", str(build), "-G", "Ninja",
                 "-DCONFIG=px4_sitl_p2i-default", "-DVENDOR=px4", "-DMODEL=sitl",
                 "-DLABEL=p2i-default", f"-DPX4_BOARD_DIR={source}/boards/px4/sitl",
                 f"-DPX4_CONFIG_FILE={source}/{selected_prefix}p2i-default.px4board",
                 f"-DEXTERNAL_MODULES_LOCATION={source}/{selected_prefix}px4_external",
                 "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"] + p2g_check.configuration_arguments(
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
            and audit["selector_enabled"] is True and audit["recorded_system_state"] is True
            and audit["selected_attitude_observer"] is True
            and audit["admission"] == "not_linked" and audit["control_modules"] == "not_linked"
            and audit["output_drivers"] == "not_linked", "configuration drift")
    config = {line.split("=", 1)[0] for line in (build / "boardconfig").read_text().splitlines()
              if line.endswith("=y")}
    require({item for item in config if item.startswith("CONFIG_MODULES_")} == {"CONFIG_MODULES_EKF2"}
            and not any(item.startswith("CONFIG_DRIVERS_") for item in config)
            and "CONFIG_EKF2_MULTI_INSTANCE" in config and "CONFIG_BOARD_NOLOCKSTEP" in config,
            "nonclosed P2I profile")
    apps = set(re.findall(r'apps\["([a-zA-Z0-9_]+)"\]',
                          (build / "platforms/posix/apps.cpp").read_text()))
    require(apps == BUILTINS and set(audit["builtins"]) == BUILTINS, "builtin inventory")
    commands = read(build / "compile_commands.json", 16 * 1024 * 1024)
    endings = ("/SelectedAttitude.cpp", "/RecordedSystemState.cpp", "/SelectorDisposition.cpp",
               "/SelectorState.cpp", "/EKF2.cpp", "/EKF2Selector.cpp",
               "/research_selector_provenance_p2i.cpp")
    for command in commands:
        require(not any(token in command["command"] for token in
                        ("-DENABLE_LOCKSTEP_SCHEDULER", "-DCONFIG_MUORB_APPS_SYNC_TIMESTAMP",
                         "RESEARCH_C1E_NEGATIVE", "RESEARCH_SITL_DIAGNOSTIC")), "mixed P2I profile")
    for ending in endings:
        selected = [command for command in commands if command["file"].endswith(ending)]
        require(len(selected) == 1 and "-DRESEARCH_P2I_SELECTED_ATTITUDE=1" in selected[0]["command"],
                "missing P2I profile definition")

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
                and run["system_state_fixture_sha256"] == manifest["system_state_provenance"]["fixture_sha256"],
                "unclean P2I process/input")
        artifacts = {"startup.sh", "imu.txt", "system_state.txt", "stdout.log", "selector.json",
                     "feeder.json", "system_state.json", "disposition.json", "selected_attitude.json"}
        require(set(run["artifacts"]) == artifacts, "case artifact inventory")
        for name, digest in run["artifacts"].items():
            require(sha(runtime / name) == digest, "case artifact drift")
        require(sha(runtime / "startup.sh") == registry[selected_prefix + case + ".sh"]
                and sha(runtime / "imu.txt") == baseline["fixture_sha256"]
                and context_fixture(runtime / "system_state.txt",
                                    manifest["system_state_provenance"]) == source_state_rows,
                "runtime input drift")
        log = (runtime / "stdout.log").read_text()
        marker = "P2I_CAPTURE_COMPLETE" if case == "normal" else "P2I_EXPECTED_RESTART_REJECTION"
        require(marker in log and "ERROR [" not in log and "P2I_UNEXPECTED" not in log,
                "case completion")
        selector = read(runtime / "selector.json")
        selector_result = p2g_check.selector_trace(selector, run["run_id"], case, sensor)
        selector_result["feeder"] = p2g_check.feeder_trace(
            read(runtime / "feeder.json"), run["run_id"], sensor)
        result = dict(case=case, selector=selector_result,
                      system_state=system_state_trace(read(runtime / "system_state.json"),
                                                      source_state_rows),
                      disposition=disposition_trace(read(runtime / "disposition.json"),
                                                    run["run_id"], case, selector),
                      selected_attitude=selected_trace(read(runtime / "selected_attitude.json"),
                                                       run["run_id"], case, selector),
                      health_policy_configured=False, health_qualified=False,
                      failover_qualified=False, positive_admission=False,
                      eligible_for_control=False, actuator_publications=False)
        results.append(result)
    return dict(verified=True, configuration=manifest["configuration"],
                scope="source_derived_valid_selected_attitude_observation_only", cases=results,
                manifest_sha256=sha(area / "manifest.json"), health_policy_configured=False,
                health_qualified=False, failover_qualified=False, positive_admission=False,
                eligible_for_control=False, actuator_publications=False)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("area", type=Path)
    print(json.dumps(check(parser.parse_args().area), indent=2, sort_keys=True))
