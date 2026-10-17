#!/usr/bin/env python3
"""Independently check P2H upstream validity and selected-output disposition provenance."""

import argparse
import importlib.util
import json
from pathlib import Path
import re


HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
PREFIX = "Tools/research_sitl/provenance/"
spec = importlib.util.spec_from_file_location(
    "p2g_check", HERE.parent / "selector_state/check_selector_state.py")
p2g_check = importlib.util.module_from_spec(spec)
spec.loader.exec_module(p2g_check)
require = p2g_check.require
read = p2g_check.read
sha = p2g_check.sha
uint = p2g_check.uint
OVERLAYS = p2g_check.OVERLAYS
BUILTINS = p2g_check.BUILTINS | {"research_selector_disposition"}


def authority(value):
    p2g_check.authority(value)


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
            <= lifecycle["estimator_end_us"], "disposition lifecycle chronology")
    require(value["binding"] == [0, 0], "disposition source binding")

    ekf_attitudes = value["ekf_attitudes"]
    require(type(ekf_attitudes) is list and len(ekf_attitudes) == counts["ekf_attitude"],
            "EKF attitude-attempt coverage")
    previous_sample = 0
    previous_observed = 0
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
                and (not row["publication_succeeded"] or row["publication_attempted"])
                and (not row["publication_attempted"] or row["attitude_valid"]),
                "EKF attitude-attempt contract")
        previous_sample = row["sample_us"]
        previous_observed = row["observed_us"]
    require(not any(row["attitude_valid"] or row["publication_attempted"]
                    or row["publication_succeeded"] for row in ekf_attitudes),
            "unexpected attitude-valid source in frozen fixture")

    attitudes = value["attitude_dispositions"]
    require(type(attitudes) is list and len(attitudes) == counts["attitude"],
            "attitude-disposition coverage")
    previous_observed = 0
    attitude_published = []
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
            require(0 < row["sample_us"] <= row["source_publication_us"] <= row["observed_us"]
                    and row["monotonic"] == (row["sample_us"] > row["previous_sample_us"])
                    and row["fresh"] == (row["age_us"] <= 10000)
                    and row["publish_requested"] == (row["monotonic"] and row["fresh"]),
                    "attitude gate disposition")
            if row["publish_requested"]:
                attitude_published.append(row)
        else:
            require(all(row[key] == 0 for key in ("sample_us", "source_publication_us",
                                                  "previous_sample_us", "age_us"))
                    and not row["monotonic"] and not row["fresh"]
                    and not row["publish_requested"], "invented absent attitude candidate")
        previous_observed = row["observed_us"]
    require(not any(row["candidate_available"] for row in attitudes),
            "unexpected selector attitude candidate in frozen fixture")
    require(len(attitude_published) == len(selector["attitudes"]),
            "attitude publication/disposition mismatch")
    for disposition, published in zip(attitude_published, selector["attitudes"]):
        require(disposition["sample_us"] == published["sample_us"],
                "attitude publication chronology mismatch")

    local_positions = value["local_position_dispositions"]
    require(type(local_positions) is list and len(local_positions) == counts["local_position"],
            "local-position disposition coverage")
    previous_observed = 0
    previous_candidate_sample = 0
    local_published = []
    local_candidates = 0
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
        require(row["sequence"] == index + 1 and row["selected_instance"] == 0
                and row["source_instance"] == 0 and previous_observed <= row["observed_us"]
                <= lifecycle["selector_end_us"], "local-position disposition chronology")
        require(type(row["source_acceleration_bits"]) is list
                and len(row["source_acceleration_bits"]) == 3, "acceleration disposition shape")
        for item in row["source_acceleration_bits"]:
            uint(item, (1 << 32) - 1)
        for key in ("candidate_available", "monotonic", "fresh", "publish_requested"):
            require(type(row[key]) is bool, "local-position disposition boolean")
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

    return dict(case=case, ekf_attitude_attempts=len(ekf_attitudes),
                ekf_attitude_valid=0, attitude_candidate_checks=len(attitudes),
                attitude_candidates=0, attitude_publications=0,
                local_position_checks=len(local_positions), local_position_candidates=local_candidates,
                local_position_publications=len(local_published), local_position_stale=local_stale,
                local_position_nonmonotonic=local_nonmonotonic,
                health_policy_configured=False, health_qualified=False, failover_qualified=False,
                positive_admission=False, eligible_for_control=False)


def check(area):
    area = Path(area).resolve()
    manifest = read(area / "manifest.json")
    require(manifest["schema"] == "p2h-selector-disposition-build/v1"
            and manifest["status"] == "captured_unqualified", "incomplete P2H build/run")
    require(manifest["configuration"] in ("Debug", "Release", "Sanitize"),
            "unknown configuration")
    require(sha(area / "source_hashes.json") == manifest["source_hashes_sha256"],
            "source registry drift")
    registry = read(area / "source_hashes.json", 16 * 1024 * 1024)
    require(set(manifest["overlay"]) == OVERLAYS, "overlay inventory")

    baseline = manifest["baseline"]
    require(set(baseline) == {"path", "manifest_sha256", "verification", "fixture_sha256"},
            "baseline binding fields")
    baseline_path = Path(baseline["path"])
    require(sha(baseline_path / "manifest.json") == baseline["manifest_sha256"],
            "P2G manifest drift")
    baseline_check = p2g_check.check(baseline_path)
    require(baseline_check == baseline["verification"] and baseline_check["verified"],
            "P2G verification drift")
    baseline_manifest = read(baseline_path / "manifest.json")
    for rel, hashes in manifest["overlay"].items():
        require(registry[rel] == hashes["original"]
                and baseline_manifest["overlay"][rel]["derived"] == hashes["p2g_derived"]
                and sha(area / "source" / rel) == hashes["p2h_derived"], "overlay drift")

    prefixes = (PREFIX + "selector_state/", PREFIX + "selector_disposition/")
    expected_inputs = {rel for rel in registry if rel.startswith(prefixes)}
    actual_inputs = {str(path.relative_to(area / "inputs"))
                     for path in (area / "inputs").rglob("*") if path.is_file()}
    require(actual_inputs == expected_inputs
            and PREFIX + "selector_disposition/SelectorDisposition.cpp" in expected_inputs,
            "P2H source archive inventory")
    for rel in expected_inputs:
        require(sha(area / "inputs" / rel) == registry[rel], "archived P2H input drift")

    sensor = manifest["sensor_provenance"]
    require(sensor == baseline_manifest["sensor_provenance"]
            and sha(baseline_path / "run-normal/imu.txt") == baseline["fixture_sha256"],
            "P2G sensor/fixture drift")
    build = area / "build"
    source = area / "source"
    require(sha(build / "bin/px4") == manifest["executable_sha256"], "executable drift")
    disposition_prefix = PREFIX + "selector_disposition/"
    configure = ["cmake", "-S", str(source), "-B", str(build), "-G", "Ninja",
                 "-DCONFIG=px4_sitl_p2h-default", "-DVENDOR=px4", "-DMODEL=sitl",
                 "-DLABEL=p2h-default", f"-DPX4_BOARD_DIR={source}/boards/px4/sitl",
                 f"-DPX4_CONFIG_FILE={source}/{disposition_prefix}p2h-default.px4board",
                 f"-DEXTERNAL_MODULES_LOCATION={source}/{disposition_prefix}px4_external",
                 "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"] + p2g_check.configuration_arguments(manifest["configuration"])
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
            and audit["selector_enabled"] is True and audit["disposition_observer"] is True
            and audit["admission"] == "not_linked" and audit["control_modules"] == "not_linked"
            and audit["output_drivers"] == "not_linked", "configuration drift")
    config = {line.split("=", 1)[0] for line in (build / "boardconfig").read_text().splitlines()
              if line.endswith("=y")}
    require({item for item in config if item.startswith("CONFIG_MODULES_")} == {"CONFIG_MODULES_EKF2"}
            and not any(item.startswith("CONFIG_DRIVERS_") for item in config)
            and "CONFIG_EKF2_MULTI_INSTANCE" in config and "CONFIG_BOARD_NOLOCKSTEP" in config,
            "nonclosed P2H profile")
    apps = set(re.findall(r'apps\["([a-zA-Z0-9_]+)"\]',
                          (build / "platforms/posix/apps.cpp").read_text()))
    require(apps == BUILTINS and set(audit["builtins"]) == BUILTINS, "builtin inventory")
    commands = read(build / "compile_commands.json", 16 * 1024 * 1024)
    for command in commands:
        require(not any(token in command["command"] for token in
                        ("-DENABLE_LOCKSTEP_SCHEDULER", "-DCONFIG_MUORB_APPS_SYNC_TIMESTAMP",
                         "RESEARCH_C1E_NEGATIVE", "RESEARCH_SITL_DIAGNOSTIC")),
                "mixed P2H profile")
    for ending in ("/SelectorDisposition.cpp", "/SelectorState.cpp", "/EKF2.cpp",
                   "/EKF2Selector.cpp", "/research_selector_provenance.cpp"):
        selected = [command for command in commands if command["file"].endswith(ending)]
        require(len(selected) == 1
                and "-DRESEARCH_P2H_SELECTOR_DISPOSITION=1" in selected[0]["command"],
                "missing P2H profile definition")

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
                and run["fixture_sha256"] == baseline["fixture_sha256"], "unclean P2H process")
        artifacts = {"startup.sh", "imu.txt", "stdout.log", "selector.json", "feeder.json",
                     "disposition.json"}
        require(set(run["artifacts"]) == artifacts, "case artifact inventory")
        for name, digest in run["artifacts"].items():
            require(sha(runtime / name) == digest, "case artifact drift")
        require(sha(runtime / "startup.sh") == registry[disposition_prefix + case + ".sh"]
                and sha(runtime / "imu.txt") == baseline["fixture_sha256"], "runtime input drift")
        log = (runtime / "stdout.log").read_text()
        marker = "P2H_CAPTURE_COMPLETE" if case == "normal" else "P2H_EXPECTED_RESTART_REJECTION"
        require(marker in log and "ERROR [" not in log
                and "P2H_UNEXPECTED" not in log, "case completion")
        selector = read(runtime / "selector.json")
        selector_result = p2g_check.selector_trace(selector, run["run_id"], case, sensor)
        selector_result["feeder"] = p2g_check.feeder_trace(
            read(runtime / "feeder.json"), run["run_id"], sensor)
        disposition_result = disposition_trace(
            read(runtime / "disposition.json"), run["run_id"], case, selector)
        disposition_result["selector"] = selector_result
        results.append(disposition_result)
    return dict(verified=True, configuration=manifest["configuration"],
                scope="selector_output_disposition_observation_only", cases=results,
                manifest_sha256=sha(area / "manifest.json"), health_policy_configured=False,
                health_qualified=False, failover_qualified=False, positive_admission=False,
                eligible_for_control=False)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("area", type=Path)
    print(json.dumps(check(parser.parse_args().area), indent=2, sort_keys=True))
