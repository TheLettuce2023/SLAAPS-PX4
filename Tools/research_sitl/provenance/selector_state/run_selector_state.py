#!/usr/bin/env python3
"""Build a private selector-enabled EKF provenance profile; no admission or output path."""

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
spec = importlib.util.spec_from_file_location(
    "p2f_check", HERE.parent / "selected_state/check_selected_state.py")
p2f_check = importlib.util.module_from_spec(spec)
spec.loader.exec_module(p2f_check)
base_spec = importlib.util.spec_from_file_location(
    "passive_build", ROOT / "Tools/research_sitl/px4_attachment/run_probe.py")
base = importlib.util.module_from_spec(base_spec)
base_spec.loader.exec_module(base)
require = p2f_check.require
PINS = {
    "src/modules/ekf2/EKF2.cpp": "1c1e2bca5b9e33aaee146c078b95f5b6e2bd87734223f72a2e0505b300d94ef5",
    "src/modules/ekf2/EKF2Selector.cpp": "e6cff5d668c6fa1054678cae4f034e87823351919dd3acc66dda17abb6bc9b0c",
    "src/modules/ekf2/EKF2Selector.hpp": "434d6ca77f6a3117baee114e48519b3ae1ee3e1ddadbf71c715bfc3b27e3d4a7",
    "src/modules/ekf2/CMakeLists.txt": "fccb276512fc59150318b1d2fe689c6fca63c2f6cb24972b209a9b62cd1b74dd",
}
BUILTINS = {"ekf2", "research_selector_provenance", "param", "shutdown", "uorb", "ver",
            "list_tasks", "list_files", "sleep"}


def replace_once(text, before, after):
    require(text.count(before) == 1, "private P2G seam drift: " + before[:100])
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
                         "EKF2::~EKF2()\n{\n\tresearch_selector_estimator_end(this);\n"
                         "\tperf_free(_ekf_update_perf);")
    multi_success = """\t\t_instance = status_instance;

\t\tScheduleNow();"""
    multi_hook = """\t\t_instance = status_instance;
\t\tresearch_selector_estimator_start(this, _instance, _attitude_pub.get_instance(),
\t\t\t_local_position_pub.get_instance(), _estimator_status_pub.get_instance(), imu,
\t\t\t_replay_mode, _multi_mode);

\t\tScheduleNow();"""
    value = replace_once(value, multi_success, multi_hook)
    ekf.write_text('#include "SelectorState.hpp"\n' + value)

    selector = result / "src/modules/ekf2/EKF2Selector.cpp"
    value = selector.read_text()
    constructor = "\t_wind_pub.advertise();\n}"
    value = replace_once(value, constructor,
                         "\t_wind_pub.advertise();\n\tresearch_selector_start(this);\n}")
    value = replace_once(value, "EKF2Selector::~EKF2Selector()\n{\n\tStop();\n}",
                         "EKF2Selector::~EKF2Selector()\n{\n\tStop();\n"
                         "\tresearch_selector_end(this);\n}")
    selection_publish = """\t\tsensor_selection.timestamp = hrt_absolute_time();
\t\t_sensor_selection_pub.publish(sensor_selection);"""
    selection_capture = """\t\tsensor_selection.timestamp = hrt_absolute_time();
\t\tconst uint8_t research_old_instance = _selected_instance;
\t\tconst bool research_selection_published = _sensor_selection_pub.publish(sensor_selection);"""
    value = replace_once(value, selection_publish, selection_capture)
    selection_end = """\t\tfor (uint8_t i = 0; i < _available_instances; i++) {
\t\t\t_instance[i].relative_test_ratio = 0;
\t\t}

\t\treturn true;"""
    selection_hook = """\t\tfor (uint8_t i = 0; i < _available_instances; i++) {
\t\t\t_instance[i].relative_test_ratio = 0;
\t\t}

\t\tresearch_selector_selection(this, SelectorSelectionObservation{
\t\t\tsensor_selection.timestamp, _last_instance_change, _instance_changed_count,
\t\t\tsensor_selection.accel_device_id, sensor_selection.gyro_device_id,
\t\t\tresearch_old_instance, _selected_instance, _available_instances,
\t\t\tresearch_selection_published});
\t\treturn true;"""
    value = replace_once(value, selection_end, selection_hook)
    status_publish = "\t_estimator_selector_status_pub.publish(selector_status);"
    status_hook = """\tconst bool research_status_published = _estimator_selector_status_pub.publish(selector_status);
\tresearch_selector_status(this, SelectorStatusObservation{
\t\tselector_status.timestamp, selector_status.last_instance_change,
\t\tselector_status.instance_changed_count, selector_status.accel_device_id,
\t\tselector_status.gyro_device_id, selector_status.primary_instance,
\t\tselector_status.instances_available, selector_status.healthy[selector_status.primary_instance],
\t\tresearch_status_published});"""
    value = replace_once(value, status_publish, status_hook)
    attitude_update = """\tif (_instance[_selected_instance].estimator_attitude_sub.update(&attitude)) {
\t\tbool instance_change = false;"""
    attitude_capture = """\tif (_instance[_selected_instance].estimator_attitude_sub.update(&attitude)) {
\t\tconst uint64_t research_estimator_publication_us = attitude.timestamp;
\t\tconst uint8_t research_source_reset_counter = attitude.quat_reset_counter;
\t\tbool instance_change = false;"""
    value = replace_once(value, attitude_update, attitude_capture)
    attitude_publish = "\t\t\t_vehicle_attitude_pub.publish(attitude);"
    attitude_hook = """\t\t\tconst bool research_attitude_published = _vehicle_attitude_pub.publish(attitude);
\t\t\tresearch_selector_attitude(this, SelectorAttitudeObservation{
\t\t\t\tattitude.timestamp_sample, research_estimator_publication_us, attitude.timestamp,
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
\t\tconst uint8_t research_source_reset_counters[4] {local_position.xy_reset_counter,
\t\t\tlocal_position.z_reset_counter, local_position.vxy_reset_counter,
\t\t\tlocal_position.vz_reset_counter};
\t\tbool instance_change = false;"""
    value = replace_once(value, local_update, local_capture)
    local_publish = "\t\t\t_vehicle_local_position_pub.publish(local_position);"
    local_hook = """\t\t\tconst bool research_local_position_published = _vehicle_local_position_pub.publish(local_position);
\t\t\tresearch_selector_local_position(this, SelectorLocalPositionObservation{
\t\t\t\tlocal_position.timestamp_sample, research_estimator_publication_us, local_position.timestamp,
\t\t\t\t{research_source_acceleration[0], research_source_acceleration[1],
\t\t\t\t research_source_acceleration[2]},
\t\t\t\t{local_position.ax, local_position.ay, local_position.az}, _selected_instance,
\t\t\t\t_instance[_selected_instance].estimator_local_position_sub.get_instance(),
\t\t\t\tresearch_source_reset_counters[0], research_source_reset_counters[1],
\t\t\t\tresearch_source_reset_counters[2], research_source_reset_counters[3],
\t\t\t\tlocal_position.xy_reset_counter, local_position.z_reset_counter,
\t\t\t\tlocal_position.vxy_reset_counter, local_position.vz_reset_counter,
\t\t\t\tresearch_local_position_published});"""
    value = replace_once(value, local_publish, local_hook)
    selector.write_text('#include "SelectorState.hpp"\n' + value)

    cmake = result / "src/modules/ekf2/CMakeLists.txt"
    cmake.write_text(cmake.read_text() + """
target_compile_definitions(modules__ekf2 PRIVATE
    RESEARCH_P2G_SELECTOR_STATE=1
    RESEARCH_P2G_MULTI_SELECTOR=1)
target_include_directories(modules__ekf2 PRIVATE
    "${PX4_SOURCE_DIR}/Tools/research_sitl/provenance/selector_state")
target_link_libraries(modules__ekf2 PRIVATE modules__research_selector_provenance)
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
    for ending in ("/SelectorState.cpp", "/EKF2.cpp", "/EKF2Selector.cpp",
                   "/research_selector_provenance.cpp"):
        selected = [command for command in commands if command["file"].endswith(ending)]
        require(len(selected) == 1 and "-DRESEARCH_P2G_SELECTOR_STATE=1" in selected[0]["command"]
                and "-DRESEARCH_P2G_MULTI_SELECTOR=1" in selected[0]["command"],
                "missing P2G definition: " + ending)
    return dict(builtins=sorted(apps), boardconfig_sha256=base.digest(build / "boardconfig"),
                compile_commands_sha256=base.digest(build / "compile_commands.json"),
                selector_enabled=True, admission="not_linked", control_modules="not_linked",
                output_drivers="not_linked")


def configuration_arguments(configuration):
    if configuration == "Debug":
        return ["-DCMAKE_BUILD_TYPE=Debug"]
    if configuration == "Release":
        return ["-DCMAKE_BUILD_TYPE=Release"]
    sanitizer = "-fsanitize=address,undefined -fno-omit-frame-pointer"
    return ["-DCMAKE_BUILD_TYPE=Debug", f"-DCMAKE_C_FLAGS={sanitizer}",
            f"-DCMAKE_CXX_FLAGS={sanitizer}", "-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True,
                        help="independently verified final P2F evidence directory")
    parser.add_argument("--configuration", choices=("Debug", "Release", "Sanitize"), default="Debug")
    args = parser.parse_args()
    baseline = args.baseline.resolve()
    baseline_check = p2f_check.check(baseline)
    require(baseline_check["verified"], "P2F baseline not independently verified")
    baseline_manifest = p2f_check.read(baseline / "manifest.json")
    sensor = baseline_manifest["sensor_provenance"]
    source_fixture = baseline / "run-normal/imu.txt"
    require(base.digest(source_fixture) == baseline_manifest["runs"][0]["fixture"]["fixture_sha256"],
            "P2F fixture drift")
    area = Path(tempfile.mkdtemp(prefix="p2g-selector-", dir="/var/tmp"))
    print("P2G_AREA=" + str(area), flush=True)
    before = base.source_hashes()
    base.save(area / "source_hashes.json", before)
    source = view(area)
    build = area / "build"
    archive = area / "inputs"
    archive.mkdir()
    selector_prefix = PREFIX + "selector_state/"
    for rel in before:
        if rel.startswith(selector_prefix):
            destination = archive / rel
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / rel, destination)
    manifest = dict(schema="p2g-selector-build/v1", status="incomplete", source=str(ROOT),
                    px4_head=base.git("rev-parse", "HEAD"), configuration=args.configuration, runs=[],
                    sensor_provenance=sensor, source_hashes_sha256=base.digest(area / "source_hashes.json"),
                    baseline=dict(path=str(baseline), manifest_sha256=base.digest(baseline / "manifest.json"),
                                  verification=baseline_check,
                                  fixture_sha256=base.digest(source_fixture)),
                    overlay={rel: dict(original=digest, derived=base.digest(source / rel))
                             for rel, digest in PINS.items()})
    environment = dict(os.environ, CCACHE_DISABLE="1", CCACHE_DIR=str(area / "ccache"),
                       PYTHONDONTWRITEBYTECODE="1", GIT_CONFIG_COUNT="1",
                       GIT_CONFIG_KEY_0="core.autocrlf", GIT_CONFIG_VALUE_0="true",
                       GIT_SUBMODULES_ARE_EVIL="1", GIT_TERMINAL_PROMPT="0", GIT_ALLOW_PROTOCOL="file")
    if args.configuration == "Sanitize":
        environment.update(ASAN_OPTIONS="detect_leaks=0:halt_on_error=1",
                           UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
    configure = ["cmake", "-S", str(source), "-B", str(build), "-G", "Ninja",
                 "-DCONFIG=px4_sitl_p2g-default", "-DVENDOR=px4", "-DMODEL=sitl",
                 "-DLABEL=p2g-default", f"-DPX4_BOARD_DIR={source}/boards/px4/sitl",
                 f"-DPX4_CONFIG_FILE={source}/{selector_prefix}p2g-default.px4board",
                 f"-DEXTERNAL_MODULES_LOCATION={source}/{selector_prefix}px4_external",
                 "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"] + configuration_arguments(args.configuration)
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
            shutil.copy2(HERE / (case + ".sh"), runtime / "startup.sh")
            run_id = str(secrets.randbits(63) + 1)
            run_environment = {key: value for key, value in environment.items()
                               if not key.startswith(("PX4_", "SIM_", "GZ_", "P2B_", "P2E_", "P2F_", "P2G_", "LD_"))
                               and key not in ("replay", "replay_mode")}
            run_environment.update(P2G_RUN_ID=run_id,
                                   P2G_ACCEL_DEVICE_ID=str(sensor["accelerometer_device_id"]),
                                   P2G_GYRO_DEVICE_ID=str(sensor["gyroscope_device_id"]))
            command_line = [str(build / "bin/px4"), "-i", str(secrets.randbelow(50000) + 10000),
                            "-w", str(runtime), "-s", str(runtime / "startup.sh")]
            run = dict(case=case, run_id=run_id, command=command_line, forced_shutdown=False,
                       fixture_sha256=base.digest(runtime / "imu.txt"), sensor_provenance=sensor)
            manifest["runs"].append(run)
            with (runtime / "stdout.log").open("w") as log:
                child = subprocess.Popen(command_line, cwd=runtime, env=run_environment, stdin=subprocess.PIPE,
                                         stdout=log, stderr=subprocess.STDOUT, text=True, start_new_session=True)
                try:
                    child.communicate("shutdown\n", timeout=40)
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
            marker = "P2G_CAPTURE_COMPLETE" if case == "normal" else "P2G_EXPECTED_RESTART_REJECTION"
            require(run["exit_code"] == 0 and marker in log_text and "ERROR [" not in log_text,
                    "native case incomplete")
            artifacts = ("startup.sh", "imu.txt", "stdout.log", "selector.json", "feeder.json")
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
