#!/usr/bin/env python3
"""Independently check P2F single-instance and status-change provenance."""

import argparse
import importlib.util
import json
from pathlib import Path
import re


HERE = Path(__file__).resolve().parent
PREFIX = "Tools/research_sitl/provenance/"
spec = importlib.util.spec_from_file_location(
    "p2e_check", HERE.parent / "committed_state/check_committed_state.py")
p2e_check = importlib.util.module_from_spec(spec)
spec.loader.exec_module(p2e_check)
p2d_check = p2e_check.p2d_check
require = p2e_check.require
read = p2e_check.read
sha = p2e_check.sha
uint = p2e_check.uint
OVERLAYS = p2e_check.OVERLAYS


def selected_trace(value, run_id, case, committed):
    expected_keys = {"schema", "run_id", "fault", "count", "start_us", "stop_intent_us", "end_us",
                     "binding", "health_policy_configured", "health_qualified", "positive_admission",
                     "eligible_for_control", "records"}
    require(set(value) == expected_keys, "selected-state trace fields")
    require(value["schema"] == "p2f-selected-state/v1" and value["run_id"] == run_id,
            "selected-state run binding")
    require(value["health_policy_configured"] is False and value["health_qualified"] is False
            and value["positive_admission"] is False and value["eligible_for_control"] is False,
            "invented selected-state authority")
    for key in ("fault", "count", "start_us", "stop_intent_us", "end_us"):
        uint(value[key])
    require(value["fault"] == (0 if case == "normal" else 4), "wrong selected-state disposition")
    require(0 < value["start_us"] <= value["stop_intent_us"] <= value["end_us"],
            "selected-state lifecycle chronology")
    require(type(value["binding"]) is list and len(value["binding"]) == 4
            and all(type(item) is int and item == 0 for item in value["binding"]),
            "single-instance publisher binding")
    rows = value["records"]
    committed_rows = committed["records"]
    require(type(rows) is list and 0 < len(rows) <= 2048 and len(rows) == value["count"]
            and len(rows) == len(committed_rows), "selected-state row coverage")
    previous_source = 0
    previous_control_changes = None
    previous_fault_changes = None
    status_publications = 0
    control_transition_rows = 0
    fault_transition_rows = 0
    stale_status_rows = 0
    for index, (row, committed_row) in enumerate(zip(rows, committed_rows)):
        expected_row = {"sequence", "source_us", "sample_us", "update_committed", "instance",
                        "control_flags", "fault_flags", "published_control_flags",
                        "published_fault_flags", "control_status_changes", "fault_status_changes", "status"}
        require(set(row) == expected_row, "selected-state row fields")
        for key in ("sequence", "source_us", "sample_us", "control_flags", "fault_flags",
                    "published_control_flags", "published_fault_flags", "control_status_changes",
                    "fault_status_changes"):
            uint(row[key])
        require(row["sequence"] == index + 1 and type(row["instance"]) is int and row["instance"] == 0,
                "selected-state sequence/instance")
        require(row["sample_us"] == committed_row["sample_us"]
                and row["update_committed"] == committed_row["update_committed"]
                and row["control_flags"] == committed_row["control_flags"]
                and row["fault_flags"] == committed_row["fault_flags"],
                "selected-state/P2E payload differs")
        require(value["start_us"] <= row["source_us"] <= value["end_us"]
                and previous_source <= row["source_us"], "selected-state source chronology")
        require(type(row["update_committed"]) is bool, "selected-state commit predicate type")
        control_changed = False
        fault_changed = False
        if previous_control_changes is not None:
            require(previous_control_changes <= row["control_status_changes"]
                    <= previous_control_changes + 1, "control change-counter continuity")
            require(previous_fault_changes <= row["fault_status_changes"]
                    <= previous_fault_changes + 1, "fault change-counter continuity")
            control_changed = row["control_status_changes"] != previous_control_changes
            fault_changed = row["fault_status_changes"] != previous_fault_changes
            control_transition_rows += int(control_changed)
            fault_transition_rows += int(fault_changed)
        status = row["status"]
        if not row["update_committed"]:
            require(status is None, "status publication without committed update")
        else:
            require(row["published_control_flags"] == row["control_flags"]
                    and row["published_fault_flags"] == row["fault_flags"],
                    "committed status cache differs from EKF state")
        if control_changed or fault_changed:
            require(type(status) is dict, "missing status publication on change")
        if status is not None:
            expected_status = {"sample_us", "publication_us", "control_flags", "fault_flags",
                               "control_status_changes", "fault_status_changes", "instance",
                               "publication_instance"}
            require(type(status) is dict and set(status) == expected_status, "status publication fields")
            for key in ("sample_us", "publication_us", "control_flags", "fault_flags",
                        "control_status_changes", "fault_status_changes"):
                uint(status[key])
            require(0 < status["sample_us"] <= row["sample_us"]
                    and value["start_us"] <= status["publication_us"] <= row["source_us"],
                    "status publication chronology")
            require(status["control_flags"] == row["published_control_flags"]
                    and status["fault_flags"] == row["published_fault_flags"]
                    and status["control_status_changes"] == row["control_status_changes"]
                    and status["fault_status_changes"] == row["fault_status_changes"],
                    "status publication mutation binding")
            require(type(status["instance"]) is int and status["instance"] == 0
                    and type(status["publication_instance"]) is int and status["publication_instance"] == 0,
                    "status publication instance binding")
            status_publications += 1
        if (row["published_control_flags"] != row["control_flags"]
                or row["published_fault_flags"] != row["fault_flags"]):
            stale_status_rows += 1
        previous_source = row["source_us"]
        previous_control_changes = row["control_status_changes"]
        previous_fault_changes = row["fault_status_changes"]
    require(status_publications > 0 and control_transition_rows > 0, "unexercised status mutation evidence")
    return dict(case=case, records=len(rows), status_publications=status_publications,
                control_transition_rows=control_transition_rows,
                fault_transition_rows=fault_transition_rows, stale_status_rows=stale_status_rows,
                binding=value["binding"], health_policy_configured=False, health_qualified=False,
                positive_admission=False, eligible_for_control=False)


def check(area):
    area = Path(area).resolve()
    manifest = read(area / "manifest.json")
    require(manifest["schema"] == "p2f-selected-build/v1"
            and manifest["status"] == "captured_unqualified", "incomplete P2F build/run")
    require(sha(area / "source_hashes.json") == manifest["source_hashes_sha256"], "source registry drift")
    registry = read(area / "source_hashes.json", 16 * 1024 * 1024)
    require(set(manifest["overlay"]) == OVERLAYS, "overlay inventory")
    for rel, hashes in manifest["overlay"].items():
        require(registry[rel] == hashes["original"] and sha(area / "source" / rel) == hashes["derived"],
                "overlay drift")
    merge_rel = PREFIX + "merge/ReceiptMerge.cpp"
    require(manifest["overlay"][merge_rel]["original"] == manifest["overlay"][merge_rel]["derived"],
            "merge algorithm changed")
    prefixes = tuple(PREFIX + part + "/" for part in
                     ("native", "merge", "native_merge", "committed_state", "selected_state"))
    expected = {rel for rel in registry if rel.startswith(prefixes)}
    actual = {str(path.relative_to(area / "inputs")) for path in (area / "inputs").rglob("*") if path.is_file()}
    require(actual == expected and PREFIX + "selected_state/SelectedState.cpp" in expected,
            "P2F source archive inventory")
    for rel in expected:
        require(sha(area / "inputs" / rel) == registry[rel], "archived P2F input drift")
    build = area / "build"
    source = area / "source"
    require(sha(build / "bin/px4") == manifest["executable_sha256"], "executable drift")
    expected_commands = {
        "configure": ["cmake", "-S", str(source), "-B", str(build), "-G", "Ninja",
                      "-DCONFIG=px4_sitl_p2f-default", "-DVENDOR=px4", "-DMODEL=sitl", "-DLABEL=p2f-default",
                      f"-DPX4_BOARD_DIR={source}/boards/px4/sitl",
                      f"-DPX4_CONFIG_FILE={source}/{PREFIX}native/p2b-default.px4board",
                      f"-DEXTERNAL_MODULES_LOCATION={source}/{PREFIX}selected_state/px4_external",
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
            and not any(item.startswith("CONFIG_DRIVERS_") for item in config), "nonclosed P2F profile")
    require("CONFIG_BOARD_NOLOCKSTEP" in config and "CONFIG_EKF2_MULTI_INSTANCE" not in config,
            "P2F clock/instance profile")
    wanted = {"ekf2", "research_provenance", "param", "shutdown", "uorb", "ver", "list_tasks",
              "list_files", "sleep"}
    require(set(re.findall(r'apps\["([a-zA-Z0-9_]+)"\]',
                           (build / "platforms/posix/apps.cpp").read_text())) == wanted, "builtin inventory")
    commands = read(build / "compile_commands.json", 16 * 1024 * 1024)
    forbidden = ("-DENABLE_LOCKSTEP_SCHEDULER", "-DCONFIG_MUORB_APPS_SYNC_TIMESTAMP",
                 "RESEARCH_C1E_NEGATIVE", "RESEARCH_SITL_DIAGNOSTIC")
    for command in commands:
        require(not any(token in command["command"] for token in forbidden), "mixed P2F profile")
    for ending in ("/SelectedState.cpp", "/CommittedState.cpp", "/NativeMerge.cpp", "/ReceiptMerge.cpp",
                   "/EKF2.cpp", "/research_provenance.cpp"):
        selected = [command for command in commands if command["file"].endswith(ending)]
        require(len(selected) == 1 and "-DRESEARCH_P2F_SELECTED_STATE=1" in selected[0]["command"]
                and "-DRESEARCH_P2E_COMMITTED_STATE=1" in selected[0]["command"]
                and "-DRESEARCH_P2D_NATIVE=1" in selected[0]["command"],
                "missing P2F profile definition")
    sensor = manifest["sensor_provenance"]
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
                and run["sensor_provenance"] == sensor, "unclean or rebound P2F process")
        artifacts = {"startup.sh", "imu.txt", "stdout.log", "merged.json", "committed.json", "selected.json"}
        if case == "normal":
            artifacts.add("observations.json")
        require(set(run["artifacts"]) == artifacts, "case artifact inventory")
        for name, digest in run["artifacts"].items():
            require(sha(runtime / name) == digest, "case artifact drift")
        require(sha(runtime / "startup.sh") == registry[PREFIX + "selected_state/" + case + ".sh"],
                "startup binding")
        require(sha(runtime / "imu.txt") == run["fixture"]["fixture_sha256"]
                and run["fixture"]["source_sha256"] == sensor["source_sha256"], "fixture binding")
        log = (runtime / "stdout.log").read_text()
        marker = "P2F_CAPTURE_COMPLETE" if case == "normal" else "P2F_EXPECTED_RESTART_REJECTION"
        require(marker in log and "ERROR [" not in log and "P2F_UNEXPECTED_RESTART_ACCEPTANCE" not in log,
                "case completion")
        if case == "restart":
            require(not (runtime / "observations.json").exists(), "restart presented as raw success")
        merged = read(runtime / "merged.json")
        raw = read(runtime / "observations.json") if case == "normal" else None
        p2d_check.trace(merged, run["run_id"], case, raw)
        committed = read(runtime / "committed.json")
        p2e_check.committed_trace(committed, run["run_id"], case, merged, sensor)
        results.append(selected_trace(read(runtime / "selected.json"), run["run_id"], case, committed))
    return dict(verified=True, scope="single_instance_status_change_observation_only", cases=results,
                manifest_sha256=sha(area / "manifest.json"), health_policy_configured=False,
                health_qualified=False, positive_admission=False, eligible_for_control=False)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("area", type=Path)
    print(json.dumps(check(parser.parse_args().area), indent=2, sort_keys=True))
