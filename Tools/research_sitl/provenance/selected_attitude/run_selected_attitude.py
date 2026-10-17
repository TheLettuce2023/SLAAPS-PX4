#!/usr/bin/env python3
"""Build a source-derived valid selected-attitude profile; no admission or output path."""

import argparse
import importlib.util
import json
import os
from pathlib import Path
import re
import secrets
import shutil
import signal
import subprocess
import tempfile

from pyulog import ULog


HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
PREFIX = "Tools/research_sitl/provenance/"
p2h_spec = importlib.util.spec_from_file_location(
    "p2h_run", HERE.parent / "selector_disposition/run_selector_disposition.py")
p2h = importlib.util.module_from_spec(p2h_spec)
p2h_spec.loader.exec_module(p2h)
p2h_check_spec = importlib.util.spec_from_file_location(
    "p2h_check", HERE.parent / "selector_disposition/check_selector_disposition.py")
p2h_check = importlib.util.module_from_spec(p2h_check_spec)
p2h_check_spec.loader.exec_module(p2h_check)
base = p2h.base
require = p2h.require
PINS = p2h.PINS
BUILTINS = p2h.BUILTINS | {"research_selected_attitude"}
SOURCE_FEEDER = (HERE.parent / "selector_state/px4_external/src/"
                 "research_selector_provenance/research_selector_provenance.cpp")

STATUS_FIELDS = (
    "armed_time", "takeoff_time", "arming_state", "latest_arming_reason",
    "latest_disarming_reason", "nav_state_timestamp", "nav_state_user_intention",
    "nav_state", "executor_in_charge", "nav_state_display", "accepts_offboard_setpoints",
    "valid_nav_states_mask", "can_set_nav_states_mask", "hil_state", "vehicle_type",
    "failsafe", "failsafe_and_user_took_over", "failsafe_defer_state",
    "gcs_connection_lost", "gcs_connection_lost_counter", "high_latency_data_link_lost",
    "is_vtol", "is_vtol_tailsitter", "in_transition_mode", "in_transition_to_fw",
    "system_type", "system_id", "component_id", "safety_button_available", "safety_off",
    "power_input_valid", "usb_connected", "open_drone_id_system_present",
    "open_drone_id_system_healthy", "parachute_system_present", "parachute_system_healthy",
    "traffic_avoidance_system_present", "rc_calibration_in_progress", "calibration_enabled",
    "pre_flight_checks_pass",
)
LAND_FIELDS = (
    "freefall", "ground_contact", "maybe_landed", "landed", "in_ground_effect",
    "in_descend", "has_low_throttle", "vertical_movement", "horizontal_movement",
    "rotational_movement", "close_to_ground_or_skipped_check", "at_rest",
)


def replace_once(text, before, after):
    require(text.count(before) == 1, "private P2I seam drift: " + before[:100])
    return text.replace(before, after)


def generated_feeder(source):
    value = SOURCE_FEEDER.read_text()
    value = replace_once(value, '#include "SelectorState.hpp"\n',
                         '#include "SelectorState.hpp"\n#include "RecordedSystemState.hpp"\n')
    value = replace_once(value,
                         "int load_fixture(const char *path, const char *identity)\n{",
                         "int load_fixture(const char *path, const char *state_path, "
                         "const char *identity)\n{")
    value = replace_once(value,
                         "\tbootstrapped = true; // A failed bootstrap cannot be retried in this process.\n"
                         "\tFILE *file = fopen(path, \"r\");",
                         "\tbootstrapped = true; // A failed bootstrap cannot be retried in this process.\n\n"
                         "\tif (!research_recorded_state_load(state_path)) {\n"
                         "\t\tfail(Fault::Input);\n\t\treturn -1;\n\t}\n\n"
                         "\tFILE *file = fopen(path, \"r\");")
    value = replace_once(value,
                         "\tif (!bootstrapped || !sensors_bound || primed || fed || "
                         "first_fault.load() || !publish_input(0)) {",
                         "\tconst uint64_t first_source_us = research_recorded_state_first_imu_us();\n\n"
                         "\tif (!bootstrapped || !sensors_bound || primed || fed || "
                         "first_fault.load()\n"
                         "\t    || !first_source_us || !research_recorded_state_publish_through(first_source_us)\n"
                         "\t    || !publish_input(0)) {")
    value = replace_once(value,
                         "\tfor (; next_input < input_count; ++next_input) {\n"
                         "\t\tif (!publish_input(next_input)) {",
                         "\tconst uint64_t first_source_us = research_recorded_state_first_imu_us();\n"
                         "\tconst uint64_t source_step_us = research_recorded_state_step_us();\n\n"
                         "\tfor (; next_input < input_count; ++next_input) {\n"
                         "\t\tconst uint64_t source_us = first_source_us + next_input * source_step_us;\n\n"
                         "\t\tif (!research_recorded_state_publish_through(source_us)\n"
                         "\t\t    || !publish_input(next_input)) {")
    value = replace_once(value, "\treturn 0;\n}\n\nint prepare_stop()",
                         "\tif (!research_recorded_state_complete()) {\n"
                         "\t\tfail(Fault::Publication);\n\t\treturn -1;\n\t}\n\n"
                         "\treturn 0;\n}\n\nint prepare_stop()")
    value = replace_once(value,
                         "int finish(const char *selector_path, const char *feeder_path)\n{",
                         "int finish(const char *selector_path, const char *feeder_path, "
                         "const char *state_path)\n{")
    value = replace_once(value,
                         "\tconst bool selector_ok = research_selector_finish(selector_path);\n"
                         "\tconst bool feeder_ok = export_feeder(feeder_path);\n\n"
                         "\tif (!feeder_ok) {",
                         "\tconst bool selector_ok = research_selector_finish(selector_path);\n"
                         "\tconst bool feeder_ok = export_feeder(feeder_path);\n"
                         "\tconst bool state_ok = research_recorded_state_finish(state_path);\n\n"
                         "\tif (!feeder_ok || !state_ok) {")
    value = replace_once(value,
                         "\treturn selector_ok && feeder_ok && first_fault.load() == 0 ? 0 : -1;",
                         "\treturn selector_ok && feeder_ok && state_ok && first_fault.load() == 0 ? 0 : -1;")
    value = replace_once(value,
                         "\tif (argc == 4 && strcmp(argv[1], \"load\") == 0) {\n"
                         "\t\treturn load_fixture(argv[2], argv[3]);\n\t}",
                         "\tif (argc == 5 && strcmp(argv[1], \"load\") == 0) {\n"
                         "\t\treturn load_fixture(argv[2], argv[3], argv[4]);\n\t}")
    value = replace_once(value,
                         "\tif (argc == 4 && strcmp(argv[1], \"finish\") == 0) {\n"
                         "\t\treturn finish(argv[2], argv[3]);\n\t}",
                         "\tif (argc == 5 && strcmp(argv[1], \"finish\") == 0) {\n"
                         "\t\treturn finish(argv[2], argv[3], argv[4]);\n\t}")
    target = source / "src/modules/ekf2/research_selector_provenance_p2i.cpp"
    target.write_text(value)
    return target


def view(area):
    source, _ = p2h.view(area)
    p2h_hashes = {rel: base.digest(source / rel) for rel in PINS}
    feeder = generated_feeder(source)
    selector = source / "src/modules/ekf2/EKF2Selector.cpp"
    value = selector.read_text()
    value = replace_once(value, '#include "SelectorDisposition.hpp"\n',
                         '#include "SelectorDisposition.hpp"\n#include "SelectedAttitude.hpp"\n')
    value = replace_once(value,
                         "\tresearch_selector_start(this);\n"
                         "\tresearch_disposition_selector_start(this);\n}",
                         "\tresearch_selector_start(this);\n"
                         "\tresearch_disposition_selector_start(this);\n"
                         "\tresearch_selected_attitude_selector_start(this);\n}")
    value = replace_once(value,
                         "\tStop();\n\tresearch_disposition_selector_end(this);\n"
                         "\tresearch_selector_end(this);\n}",
                         "\tStop();\n\tresearch_selected_attitude_selector_end(this);\n"
                         "\tresearch_disposition_selector_end(this);\n"
                         "\tresearch_selector_end(this);\n}")
    source_capture = """\t\tconst uint64_t research_estimator_publication_us = attitude.timestamp;
\t\tconst uint8_t research_source_reset_counter = attitude.quat_reset_counter;
\t\tbool instance_change = false;"""
    source_capture_p2i = """\t\tconst uint64_t research_estimator_publication_us = attitude.timestamp;
\t\tconst uint8_t research_source_reset_counter = attitude.quat_reset_counter;
\t\tconst float research_source_quaternion[4] {attitude.q[0], attitude.q[1], attitude.q[2], attitude.q[3]};
\t\tconst float research_source_delta_quaternion[4] {attitude.delta_q_reset[0], attitude.delta_q_reset[1],
\t\t\tattitude.delta_q_reset[2], attitude.delta_q_reset[3]};
\t\tbool instance_change = false;"""
    value = replace_once(value, source_capture, source_capture_p2i)
    publication = """\t\t\tconst bool research_attitude_published = _vehicle_attitude_pub.publish(attitude);
\t\t\tresearch_selector_attitude(this, SelectorAttitudeObservation{
\t\t\t\tattitude.timestamp_sample, research_estimator_publication_us, attitude.timestamp,
\t\t\t\t_selected_instance,
\t\t\t\t_instance[_selected_instance].estimator_attitude_sub.get_instance(),
\t\t\t\tresearch_source_reset_counter, attitude.quat_reset_counter,
\t\t\t\tresearch_attitude_published});"""
    publication_p2i = publication + """
\t\t\tresearch_selected_attitude_output(this, SelectedAttitudeObservation{
\t\t\t\tattitude.timestamp_sample, research_estimator_publication_us, attitude.timestamp,
\t\t\t\t{research_source_quaternion[0], research_source_quaternion[1],
\t\t\t\t research_source_quaternion[2], research_source_quaternion[3]},
\t\t\t\t{attitude.q[0], attitude.q[1], attitude.q[2], attitude.q[3]},
\t\t\t\t{research_source_delta_quaternion[0], research_source_delta_quaternion[1],
\t\t\t\t research_source_delta_quaternion[2], research_source_delta_quaternion[3]},
\t\t\t\t{attitude.delta_q_reset[0], attitude.delta_q_reset[1],
\t\t\t\t attitude.delta_q_reset[2], attitude.delta_q_reset[3]},
\t\t\t\t_selected_instance,
\t\t\t\t_instance[_selected_instance].estimator_attitude_sub.get_instance(),
\t\t\t\tresearch_source_reset_counter, attitude.quat_reset_counter,
\t\t\t\tresearch_attitude_published});"""
    value = replace_once(value, publication, publication_p2i)
    selector.write_text(value)
    cmake = source / "src/modules/ekf2/CMakeLists.txt"
    cmake.write_text(cmake.read_text() + """
target_compile_definitions(modules__ekf2 PRIVATE
    RESEARCH_P2I_SELECTED_ATTITUDE=1)
target_include_directories(modules__ekf2 PRIVATE
    "${PX4_SOURCE_DIR}/Tools/research_sitl/provenance/selected_attitude")
target_link_libraries(modules__ekf2 PRIVATE modules__research_selected_attitude)
""")
    return source, p2h_hashes, feeder


def dataset(ulog, name):
    selected = [item for item in ulog.data_list if item.name == name and item.multi_id == 0]
    require(len(selected) == 1, "missing/ambiguous source topic: " + name)
    return selected[0].data


def selected_rows(data, kind, fields, first_us, last_us):
    timestamps = [int(value) for value in data["timestamp"]]
    prior = [index for index, timestamp in enumerate(timestamps) if timestamp <= first_us]
    require(prior, "missing initial source state: " + kind)
    indexes = [prior[-1]] + [index for index, timestamp in enumerate(timestamps)
                            if first_us < timestamp <= last_us]
    rows = []
    for index in indexes:
        values = tuple(int(data[field][index]) for field in fields)
        rows.append((timestamps[index], kind, values))
    return rows


def context_fixture(source, target, expected_hash):
    require(base.digest(source) == expected_hash, "recorded ULog differs")
    ulog = ULog(str(source), message_name_filter_list=[
        "sensor_combined", "vehicle_status", "vehicle_land_detected"])
    require(not ulog.file_corruption, "corrupt recorded ULog")
    imu = dataset(ulog, "sensor_combined")
    imu_timestamps = [int(value) for value in imu["timestamp"][:1200]]
    require(len(imu_timestamps) == 1200 and imu_timestamps[0] > 0, "short IMU source")
    require(all(current - previous == 4000
                for previous, current in zip(imu_timestamps, imu_timestamps[1:])),
            "nonuniform source IMU timestamps")
    first_us = imu_timestamps[0]
    last_us = imu_timestamps[-1]
    status_rows = selected_rows(dataset(ulog, "vehicle_status"), "S", STATUS_FIELDS,
                                first_us, last_us)
    land_rows = selected_rows(dataset(ulog, "vehicle_land_detected"), "L", LAND_FIELDS,
                              first_us, last_us)
    rows = sorted(status_rows + land_rows, key=lambda row: (row[0], row[1]))
    require(len(rows) <= 32, "recorded system-state fixture exceeds closed capacity")
    lines = [f"M {first_us} {last_us} 4000 1200 {len(status_rows)} "
             f"{len(land_rows)} {len(rows)}"]
    lines += [" ".join((kind, str(timestamp), *(str(value) for value in values)))
              for timestamp, kind, values in rows]
    target.write_text("\n".join(lines) + "\n")
    require(base.digest(source) == expected_hash, "recorded ULog changed during extraction")
    return dict(source_path=str(source), source_sha256=expected_hash,
                first_imu_source_us=first_us, last_imu_source_us=last_us,
                imu_count=1200, step_us=4000, status_count=len(status_rows),
                land_count=len(land_rows), event_count=len(rows),
                fixture_sha256=base.digest(target), full_payloads_preserved=True,
                timestamp_transform="recorded chronology to fresh hrt publication timestamps")


def audit(build):
    enabled = {line.split("=", 1)[0] for line in (build / "boardconfig").read_text().splitlines()
               if line.endswith("=y")}
    require({item for item in enabled if item.startswith("CONFIG_MODULES_")} == {"CONFIG_MODULES_EKF2"},
            "unexpected modules")
    require(not any(item.startswith("CONFIG_DRIVERS_") for item in enabled), "driver selected")
    require("CONFIG_BOARD_NOLOCKSTEP" in enabled and "CONFIG_EKF2_MULTI_INSTANCE" in enabled,
            "wrong clock/selector profile")
    commands = json.loads((build / "compile_commands.json").read_text())
    forbidden = ("-DENABLE_LOCKSTEP_SCHEDULER", "-DCONFIG_MUORB_APPS_SYNC_TIMESTAMP",
                 "RESEARCH_C1E_NEGATIVE", "RESEARCH_SITL_DIAGNOSTIC")
    require(not any(token in command["command"] for token in forbidden for command in commands),
            "forbidden profile definition")
    apps = set(re.findall(r'apps\["([a-zA-Z0-9_]+)"\]',
                          (build / "platforms/posix/apps.cpp").read_text()))
    require(apps == BUILTINS, "unexpected linked builtins: " + repr(apps))
    endings = ("/SelectedAttitude.cpp", "/RecordedSystemState.cpp", "/SelectorDisposition.cpp",
               "/SelectorState.cpp", "/EKF2.cpp", "/EKF2Selector.cpp",
               "/research_selector_provenance_p2i.cpp")
    for ending in endings:
        selected = [command for command in commands if command["file"].endswith(ending)]
        require(len(selected) == 1
                and "-DRESEARCH_P2I_SELECTED_ATTITUDE=1" in selected[0]["command"],
                "missing P2I definition: " + ending)
    return dict(builtins=sorted(apps), boardconfig_sha256=base.digest(build / "boardconfig"),
                compile_commands_sha256=base.digest(build / "compile_commands.json"),
                selector_enabled=True, recorded_system_state=True, selected_attitude_observer=True,
                admission="not_linked", control_modules="not_linked", output_drivers="not_linked")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True,
                        help="independently verified final P2H evidence directory")
    parser.add_argument("--ulog", type=Path, required=True,
                        help="the exact hash-bound ULog that supplied the accepted IMU fixture")
    parser.add_argument("--configuration", choices=("Debug", "Release", "Sanitize"), default="Debug")
    args = parser.parse_args()
    baseline = args.baseline.resolve()
    baseline_check = p2h_check.check(baseline)
    require(baseline_check["verified"], "P2H baseline not independently verified")
    baseline_manifest = p2h_check.read(baseline / "manifest.json")
    sensor = baseline_manifest["sensor_provenance"]
    source_fixture = baseline / "run-normal/imu.txt"
    require(base.digest(source_fixture) == baseline_manifest["runs"][0]["fixture_sha256"],
            "P2H fixture drift")
    area = Path(tempfile.mkdtemp(prefix="p2i-attitude-", dir="/var/tmp"))
    print("P2I_AREA=" + str(area), flush=True)
    before = base.source_hashes()
    base.save(area / "source_hashes.json", before)
    context = context_fixture(args.ulog.resolve(), area / "system_state.txt", sensor["source_sha256"])
    source, p2h_hashes, feeder = view(area)
    build = area / "build"
    archive = area / "inputs"
    archive.mkdir()
    prefixes = tuple(PREFIX + name + "/" for name in
                     ("selector_state", "selector_disposition", "selected_attitude"))
    for rel in before:
        if rel.startswith(prefixes):
            destination = archive / rel
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / rel, destination)
    selected_prefix = PREFIX + "selected_attitude/"
    manifest = dict(schema="p2i-selected-attitude-build/v1", status="incomplete",
                    source=str(ROOT), px4_head=base.git("rev-parse", "HEAD"),
                    configuration=args.configuration, runs=[], sensor_provenance=sensor,
                    system_state_provenance=context,
                    source_hashes_sha256=base.digest(area / "source_hashes.json"),
                    baseline=dict(path=str(baseline),
                                  manifest_sha256=base.digest(baseline / "manifest.json"),
                                  verification=baseline_check,
                                  fixture_sha256=base.digest(source_fixture)),
                    overlay={rel: dict(original=digest, p2h_derived=p2h_hashes[rel],
                                       p2i_derived=base.digest(source / rel))
                             for rel, digest in PINS.items()},
                    generated_feeder=dict(source_sha256=base.digest(SOURCE_FEEDER),
                                          derived_sha256=base.digest(feeder)))
    environment = dict(os.environ, CCACHE_DISABLE="1", CCACHE_DIR=str(area / "ccache"),
                       PYTHONDONTWRITEBYTECODE="1", GIT_CONFIG_COUNT="1",
                       GIT_CONFIG_KEY_0="core.autocrlf", GIT_CONFIG_VALUE_0="true",
                       GIT_SUBMODULES_ARE_EVIL="1", GIT_TERMINAL_PROMPT="0",
                       GIT_ALLOW_PROTOCOL="file")
    if args.configuration == "Sanitize":
        environment.update(ASAN_OPTIONS="detect_leaks=0:halt_on_error=1",
                           UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
    configure = ["cmake", "-S", str(source), "-B", str(build), "-G", "Ninja",
                 "-DCONFIG=px4_sitl_p2i-default", "-DVENDOR=px4", "-DMODEL=sitl",
                 "-DLABEL=p2i-default", f"-DPX4_BOARD_DIR={source}/boards/px4/sitl",
                 f"-DPX4_CONFIG_FILE={source}/{selected_prefix}p2i-default.px4board",
                 f"-DEXTERNAL_MODULES_LOCATION={source}/{selected_prefix}px4_external",
                 "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"] + p2h.p2g.configuration_arguments(args.configuration)
    commands = [("configure", configure),
                ("build", ["cmake", "--build", str(build), "--target", "px4", "-j", "4"])]
    try:
        for name, command_line in commands:
            print(name, flush=True)
            with (area / (name + ".log")).open("w") as log:
                code = base.bounded_command(command_line, source, environment, log)
            manifest[name] = dict(command=command_line, exit_code=code,
                                  log_sha256=base.digest(area / (name + ".log")))
            require(code == 0, name + " failed")
            manifest["configuration_audit"] = audit(build)
            base.save(area / "manifest.json", manifest)
        manifest["executable_sha256"] = base.digest(build / "bin/px4")
        for case in ("normal", "restart"):
            runtime = area / ("run-" + case)
            runtime.mkdir()
            shutil.copy2(source_fixture, runtime / "imu.txt")
            shutil.copy2(area / "system_state.txt", runtime / "system_state.txt")
            shutil.copy2(HERE / (case + ".sh"), runtime / "startup.sh")
            run_id = str(secrets.randbits(63) + 1)
            run_environment = {key: value for key, value in environment.items()
                               if not key.startswith(("PX4_", "SIM_", "GZ_", "P2B_", "P2E_",
                                                      "P2F_", "P2G_", "P2H_", "P2I_", "LD_"))
                               and key not in ("replay", "replay_mode")}
            run_environment.update(P2I_RUN_ID=run_id,
                                   P2I_ACCEL_DEVICE_ID=str(sensor["accelerometer_device_id"]),
                                   P2I_GYRO_DEVICE_ID=str(sensor["gyroscope_device_id"]))
            command_line = [str(build / "bin/px4"), "-i", str(secrets.randbelow(50000) + 10000),
                            "-w", str(runtime), "-s", str(runtime / "startup.sh")]
            run = dict(case=case, run_id=run_id, command=command_line, forced_shutdown=False,
                       fixture_sha256=base.digest(runtime / "imu.txt"),
                       system_state_fixture_sha256=base.digest(runtime / "system_state.txt"),
                       sensor_provenance=sensor)
            manifest["runs"].append(run)
            with (runtime / "stdout.log").open("w") as log:
                child = subprocess.Popen(command_line, cwd=runtime, env=run_environment,
                                         stdin=subprocess.PIPE, stdout=log, stderr=subprocess.STDOUT,
                                         text=True, start_new_session=True)
                try:
                    child.communicate("shutdown\n", timeout=45)
                except BaseException:
                    run["forced_shutdown"] = True
                    if child.poll() is None:
                        os.killpg(child.pid, signal.SIGTERM)
                        try:
                            child.communicate(timeout=5)
                        except subprocess.TimeoutExpired:
                            os.killpg(child.pid, signal.SIGKILL)
                            child.communicate(timeout=5)
                    raise
                run["exit_code"] = child.returncode
            log_text = (runtime / "stdout.log").read_text()
            marker = "P2I_CAPTURE_COMPLETE" if case == "normal" else "P2I_EXPECTED_RESTART_REJECTION"
            require(run["exit_code"] == 0 and marker in log_text and "ERROR [" not in log_text,
                    "native case incomplete")
            artifacts = ("startup.sh", "imu.txt", "system_state.txt", "stdout.log",
                         "selector.json", "feeder.json", "system_state.json",
                         "disposition.json", "selected_attitude.json")
            run["artifacts"] = {name: base.digest(runtime / name) for name in artifacts}
        require(base.source_hashes() == before, "source changed during build/run")
        manifest["status"] = "captured_unqualified"
    except BaseException as error:
        manifest["status"] = "failed"
        manifest["error"] = str(error)
        raise
    finally:
        base.save(area / "manifest.json", manifest)
    print(str(area), flush=True)


if __name__ == "__main__":
    main()
