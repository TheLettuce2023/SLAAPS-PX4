#!/usr/bin/env python3
"""Build and run a closed two-EKF stock-selector handoff profile."""

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


HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
PREFIX = "Tools/research_sitl/provenance/"
p2i_run_spec = importlib.util.spec_from_file_location(
    "p2i_run", HERE.parent / "selected_attitude/run_selected_attitude.py")
p2i = importlib.util.module_from_spec(p2i_run_spec)
p2i_run_spec.loader.exec_module(p2i)
p2i_check_spec = importlib.util.spec_from_file_location(
    "p2i_check", HERE.parent / "selected_attitude/check_selected_attitude.py")
p2i_check = importlib.util.module_from_spec(p2i_check_spec)
p2i_check_spec.loader.exec_module(p2i_check)
base = p2i.base
require = p2i.require
PINS = p2i.PINS
BUILTINS = {"ekf2", "research_multi_imu", "research_selector_handoff", "param",
            "shutdown", "uorb", "ver", "list_tasks", "list_files", "sleep"}


def replace_once(text, before, after):
    require(text.count(before) == 1, "private P2J seam drift: " + before[:100])
    return text.replace(before, after)


def view(area):
    result = area / "source"
    result.mkdir()
    private = {"src", "src/modules", "src/modules/ekf2"}

    def populate(source, target, prefix=""):
        for item in source.iterdir():
            rel = prefix + item.name
            destination = target / item.name
            if rel in ("build", ".vscode"):
                destination.mkdir()
            elif rel in private:
                destination.mkdir()
                populate(item, destination, rel + "/")
            elif rel in PINS:
                require(base.digest(item) == PINS[rel], "unreviewed overlay source: " + rel)
                destination.write_text(item.read_text())
            else:
                destination.symlink_to(item, target_is_directory=item.is_dir())

    populate(ROOT, result)

    ekf = result / "src/modules/ekf2/EKF2.cpp"
    value = ekf.read_text()
    value = replace_once(value, "EKF2::~EKF2()\n{\n\tperf_free(_ekf_update_perf);",
                         "EKF2::~EKF2()\n{\n\tresearch_handoff_estimator_end(this);\n"
                         "\tperf_free(_ekf_update_perf);")
    multi_success = """\t\t_instance = status_instance;

\t\tScheduleNow();"""
    multi_hook = """\t\t_instance = status_instance;
\t\tresearch_handoff_estimator_start(this, HandoffEstimatorBinding{
\t\t\tstatic_cast<int8_t>(_instance), static_cast<int8_t>(_attitude_pub.get_instance()),
\t\t\tstatic_cast<int8_t>(_local_position_pub.get_instance()),
\t\t\tstatic_cast<int8_t>(_estimator_status_pub.get_instance()), static_cast<int8_t>(imu),
\t\t\t_replay_mode, _multi_mode});

\t\tScheduleNow();"""
    value = replace_once(value, multi_success, multi_hook)
    ekf.write_text('#include "SelectorHandoff.hpp"\n' + value)

    selector = result / "src/modules/ekf2/EKF2Selector.cpp"
    value = selector.read_text()
    value = replace_once(value, "\t_wind_pub.advertise();\n}",
                         "\t_wind_pub.advertise();\n\tresearch_handoff_selector_start(this);\n}")
    value = replace_once(value, "EKF2Selector::~EKF2Selector()\n{\n\tStop();\n}",
                         "EKF2Selector::~EKF2Selector()\n{\n\tStop();\n"
                         "\tresearch_handoff_selector_end(this);\n}")
    selection_publish = """\t\tsensor_selection.timestamp = hrt_absolute_time();
\t\t_sensor_selection_pub.publish(sensor_selection);"""
    selection_capture = """\t\tsensor_selection.timestamp = hrt_absolute_time();
\t\tconst uint8_t research_old_instance = _selected_instance;
\t\tconst uint8_t research_requested_instance = _request_instance.load();
\t\tconst bool research_old_healthy = research_old_instance < EKF2_MAX_INSTANCES
\t\t\t\t\t\t   && _instance[research_old_instance].healthy.get_state();
\t\tconst bool research_old_timeout = research_old_instance < EKF2_MAX_INSTANCES
\t\t\t\t\t\t   && _instance[research_old_instance].timeout;
\t\tconst bool research_new_healthy = _instance[ekf_instance].healthy.get_state();
\t\tconst bool research_new_timeout = _instance[ekf_instance].timeout;
\t\tconst bool research_selection_published = _sensor_selection_pub.publish(sensor_selection);"""
    value = replace_once(value, selection_publish, selection_capture)
    selection_end = """\t\tfor (uint8_t i = 0; i < _available_instances; i++) {
\t\t\t_instance[i].relative_test_ratio = 0;
\t\t}

\t\treturn true;"""
    selection_hook = """\t\tfor (uint8_t i = 0; i < _available_instances; i++) {
\t\t\t_instance[i].relative_test_ratio = 0;
\t\t}

\t\tresearch_handoff_selection(this, HandoffSelectionObservation{
\t\t\tsensor_selection.timestamp, _instance_changed_count,
\t\t\tsensor_selection.accel_device_id, sensor_selection.gyro_device_id,
\t\t\tresearch_old_instance, _selected_instance, _available_instances,
\t\t\tresearch_requested_instance, research_old_healthy, research_old_timeout,
\t\t\tresearch_new_healthy, research_new_timeout, research_selection_published});
\t\treturn true;"""
    value = replace_once(value, selection_end, selection_hook)
    status_publish = "\t_estimator_selector_status_pub.publish(selector_status);"
    status_hook = """\tconst bool research_status_published = _estimator_selector_status_pub.publish(selector_status);
\tresearch_handoff_status(this, HandoffStatusObservation{
\t\tselector_status.timestamp, selector_status.last_instance_change,
\t\tselector_status.instance_changed_count, selector_status.accel_device_id,
\t\tselector_status.gyro_device_id,
\t\t{selector_status.combined_test_ratio[0], selector_status.combined_test_ratio[1]},
\t\tselector_status.primary_instance, selector_status.instances_available,
\t\t{selector_status.healthy[0], selector_status.healthy[1]}, research_status_published});"""
    value = replace_once(value, status_publish, status_hook)
    attitude_update = """\tif (_instance[_selected_instance].estimator_attitude_sub.update(&attitude)) {
\t\tbool instance_change = false;"""
    attitude_capture = """\tif (_instance[_selected_instance].estimator_attitude_sub.update(&attitude)) {
\t\tconst uint64_t research_estimator_publication_us = attitude.timestamp;
\t\tconst uint8_t research_source_reset_counter = attitude.quat_reset_counter;
\t\tconst float research_source_quaternion[4] {attitude.q[0], attitude.q[1], attitude.q[2], attitude.q[3]};
\t\tconst float research_source_delta_quaternion[4] {attitude.delta_q_reset[0], attitude.delta_q_reset[1],
\t\t\tattitude.delta_q_reset[2], attitude.delta_q_reset[3]};
\t\tbool instance_change = false;"""
    value = replace_once(value, attitude_update, attitude_capture)
    attitude_publish = "\t\t\t_vehicle_attitude_pub.publish(attitude);"
    attitude_hook = """\t\t\tconst bool research_attitude_published = _vehicle_attitude_pub.publish(attitude);
\t\t\tresearch_handoff_attitude(this, HandoffAttitudeObservation{
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
    value = replace_once(value, attitude_publish, attitude_hook)
    local_update = """\tif (_instance[_selected_instance].estimator_local_position_sub.update(&local_position)) {
\t\tbool instance_change = false;"""
    local_capture = """\tif (_instance[_selected_instance].estimator_local_position_sub.update(&local_position)) {
\t\tconst uint64_t research_estimator_publication_us = local_position.timestamp;
\t\tconst float research_source_acceleration[3] {local_position.ax, local_position.ay, local_position.az};
\t\tconst uint8_t research_source_reset_counters[6] {local_position.xy_reset_counter,
\t\t\tlocal_position.z_reset_counter, local_position.vxy_reset_counter,
\t\t\tlocal_position.vz_reset_counter, local_position.heading_reset_counter,
\t\t\tlocal_position.dist_bottom_reset_counter};
\t\tbool instance_change = false;"""
    value = replace_once(value, local_update, local_capture)
    local_publish = "\t\t\t_vehicle_local_position_pub.publish(local_position);"
    local_hook = """\t\t\tconst bool research_local_position_published = _vehicle_local_position_pub.publish(local_position);
\t\t\tresearch_handoff_local_position(this, HandoffLocalPositionObservation{
\t\t\t\tlocal_position.timestamp_sample, research_estimator_publication_us, local_position.timestamp,
\t\t\t\t{research_source_acceleration[0], research_source_acceleration[1],
\t\t\t\t research_source_acceleration[2]},
\t\t\t\t{local_position.ax, local_position.ay, local_position.az},
\t\t\t\t{research_source_reset_counters[0], research_source_reset_counters[1],
\t\t\t\t research_source_reset_counters[2], research_source_reset_counters[3],
\t\t\t\t research_source_reset_counters[4], research_source_reset_counters[5]},
\t\t\t\t{local_position.xy_reset_counter, local_position.z_reset_counter,
\t\t\t\t local_position.vxy_reset_counter, local_position.vz_reset_counter,
\t\t\t\t local_position.heading_reset_counter, local_position.dist_bottom_reset_counter},
\t\t\t\t_selected_instance,
\t\t\t\t_instance[_selected_instance].estimator_local_position_sub.get_instance(),
\t\t\t\tresearch_local_position_published});"""
    value = replace_once(value, local_publish, local_hook)
    selector.write_text('#include "SelectorHandoff.hpp"\n' + value)

    cmake = result / "src/modules/ekf2/CMakeLists.txt"
    cmake.write_text(cmake.read_text() + """
target_compile_definitions(modules__ekf2 PRIVATE RESEARCH_P2J_SELECTOR_HANDOFF=1)
target_include_directories(modules__ekf2 PRIVATE
    "${PX4_SOURCE_DIR}/Tools/research_sitl/provenance/selector_handoff")
target_link_libraries(modules__ekf2 PRIVATE modules__research_selector_handoff)
""")
    return result


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
    p2j_endings = ("/SelectorHandoff.cpp", "/research_multi_imu.cpp", "/EKF2.cpp",
                   "/EKF2Selector.cpp")
    for ending in p2j_endings:
        selected = [command for command in commands if command["file"].endswith(ending)]
        require(len(selected) == 1
                and "-DRESEARCH_P2J_SELECTOR_HANDOFF=1" in selected[0]["command"],
                "missing P2J definition: " + ending)
    state = [command for command in commands if command["file"].endswith("/RecordedSystemState.cpp")]
    require(len(state) == 1 and "-DRESEARCH_P2I_SELECTED_ATTITUDE=1" in state[0]["command"]
            and "-DRESEARCH_P2J_SELECTOR_HANDOFF=1" in state[0]["command"],
            "missing recorded-state profile definition")
    return dict(builtins=sorted(apps), boardconfig_sha256=base.digest(build / "boardconfig"),
                compile_commands_sha256=base.digest(build / "compile_commands.json"),
                selector_enabled=True, actual_ekf_instances=2, recorded_system_state=True,
                admission="not_linked", control_modules="not_linked",
                output_drivers="not_linked")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True,
                        help="independently verified final P2I evidence directory")
    parser.add_argument("--ulog", type=Path, required=True,
                        help="the exact hash-bound ULog used by P2I")
    parser.add_argument("--configuration", choices=("Debug", "Release", "Sanitize"),
                        default="Debug")
    args = parser.parse_args()
    baseline = args.baseline.resolve()
    baseline_check = p2i_check.check(baseline)
    require(baseline_check["verified"], "P2I baseline not independently verified")
    baseline_manifest = json.loads((baseline / "manifest.json").read_text())
    sensor = baseline_manifest["sensor_provenance"]
    source_fixture = baseline / "run-normal/imu.txt"
    require(base.digest(source_fixture) == baseline_manifest["runs"][0]["fixture_sha256"],
            "P2I fixture drift")
    area = Path(tempfile.mkdtemp(prefix="p2j-handoff-", dir="/var/tmp"))
    print("P2J_AREA=" + str(area), flush=True)
    before = base.source_hashes()
    base.save(area / "source_hashes.json", before)
    context = p2i.context_fixture(args.ulog.resolve(), area / "system_state.txt",
                                  sensor["source_sha256"])
    source = view(area)
    build = area / "build"
    archive = area / "inputs"
    archive.mkdir()
    prefixes = tuple(PREFIX + name + "/" for name in
                     ("selected_attitude", "selector_handoff"))
    for rel in before:
        if rel.startswith(prefixes):
            destination = archive / rel
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / rel, destination)
    overlay = {rel: dict(original=digest, p2j_derived=base.digest(source / rel))
               for rel, digest in PINS.items()}
    manifest = dict(schema="p2j-selector-handoff-build/v1", status="incomplete",
                    source=str(ROOT), px4_head=base.git("rev-parse", "HEAD"),
                    configuration=args.configuration, runs=[], sensor_provenance=sensor,
                    system_state_provenance=context,
                    source_hashes_sha256=base.digest(area / "source_hashes.json"),
                    baseline=dict(path=str(baseline),
                                  manifest_sha256=base.digest(baseline / "manifest.json"),
                                  verification=baseline_check,
                                  fixture_sha256=base.digest(source_fixture)),
                    overlay=overlay)
    environment = dict(os.environ, CCACHE_DISABLE="1", CCACHE_DIR=str(area / "ccache"),
                       PYTHONDONTWRITEBYTECODE="1", GIT_CONFIG_COUNT="1",
                       GIT_CONFIG_KEY_0="core.autocrlf", GIT_CONFIG_VALUE_0="true",
                       GIT_SUBMODULES_ARE_EVIL="1", GIT_TERMINAL_PROMPT="0",
                       GIT_ALLOW_PROTOCOL="file")
    if args.configuration == "Sanitize":
        environment.update(ASAN_OPTIONS="detect_leaks=0:halt_on_error=1",
                           UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
    selected_prefix = PREFIX + "selector_handoff/"
    configure = ["cmake", "-S", str(source), "-B", str(build), "-G", "Ninja",
                 "-DCONFIG=px4_sitl_p2j-default", "-DVENDOR=px4", "-DMODEL=sitl",
                 "-DLABEL=p2j-default", f"-DPX4_BOARD_DIR={source}/boards/px4/sitl",
                 f"-DPX4_CONFIG_FILE={source}/{selected_prefix}p2j-default.px4board",
                 f"-DEXTERNAL_MODULES_LOCATION={source}/{selected_prefix}px4_external",
                 "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"] + p2i.p2h.p2g.configuration_arguments(
                     args.configuration)
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
                                                      "P2F_", "P2G_", "P2H_", "P2I_", "P2J_", "LD_"))
                               and key not in ("replay", "replay_mode")}
            run_environment.update(P2J_RUN_ID=run_id,
                                   P2J_ACCEL_DEVICE_ID=str(sensor["accelerometer_device_id"]),
                                   P2J_GYRO_DEVICE_ID=str(sensor["gyroscope_device_id"]))
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
                    child.communicate("shutdown\n", timeout=60)
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
            marker = "P2J_CAPTURE_COMPLETE" if case == "normal" else "P2J_EXPECTED_RESTART_REJECTION"
            require(run["exit_code"] == 0 and marker in log_text and "ERROR [" not in log_text
                    and "P2J_UNEXPECTED" not in log_text, "native case incomplete")
            artifacts = ("startup.sh", "imu.txt", "system_state.txt", "stdout.log",
                         "feeder.json", "system_state.json", "handoff.json")
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
