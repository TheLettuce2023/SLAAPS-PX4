#!/usr/bin/env python3
"""Build a private P2G-derived selected-output disposition profile; no authority or output."""

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
p2g_spec = importlib.util.spec_from_file_location(
    "p2g_run", HERE.parent / "selector_state/run_selector_state.py")
p2g = importlib.util.module_from_spec(p2g_spec)
p2g_spec.loader.exec_module(p2g)
check_spec = importlib.util.spec_from_file_location(
    "p2g_check", HERE.parent / "selector_state/check_selector_state.py")
p2g_check = importlib.util.module_from_spec(check_spec)
check_spec.loader.exec_module(p2g_check)
base = p2g.base
require = p2g.require
PINS = p2g.PINS
BUILTINS = p2g.BUILTINS | {"research_selector_disposition"}


def replace_once(text, before, after):
    require(text.count(before) == 1, "private P2H seam drift: " + before[:100])
    return text.replace(before, after)


def view(area):
    source = p2g.view(area)
    p2g_hashes = {rel: base.digest(source / rel) for rel in PINS}
    ekf = source / "src/modules/ekf2/EKF2.cpp"
    value = ekf.read_text()
    value = replace_once(value, '#include "SelectorState.hpp"\n',
                         '#include "SelectorState.hpp"\n#include "SelectorDisposition.hpp"\n')
    value = replace_once(value,
                         "EKF2::~EKF2()\n{\n\tresearch_selector_estimator_end(this);",
                         "EKF2::~EKF2()\n{\n\tresearch_disposition_estimator_end(this);\n"
                         "\tresearch_selector_estimator_end(this);")
    estimator_start = """\t\tresearch_selector_estimator_start(this, _instance, _attitude_pub.get_instance(),
\t\t\t_local_position_pub.get_instance(), _estimator_status_pub.get_instance(), imu,
\t\t\t_replay_mode, _multi_mode);

\t\tScheduleNow();"""
    estimator_hook = """\t\tresearch_selector_estimator_start(this, _instance, _attitude_pub.get_instance(),
\t\t\t_local_position_pub.get_instance(), _estimator_status_pub.get_instance(), imu,
\t\t\t_replay_mode, _multi_mode);
\t\tresearch_disposition_estimator_start(this, _instance, _replay_mode, _multi_mode);

\t\tScheduleNow();"""
    value = replace_once(value, estimator_start, estimator_hook)
    attitude_start = """void EKF2::PublishAttitude(const hrt_abstime &timestamp)
{
\tif (_ekf.attitude_valid()) {"""
    attitude_start_hook = """void EKF2::PublishAttitude(const hrt_abstime &timestamp)
{
\tconst bool research_attitude_valid = _ekf.attitude_valid();
\tbool research_publication_attempted = false;
\tbool research_publication_succeeded = false;

\tif (research_attitude_valid) {"""
    value = replace_once(value, attitude_start, attitude_start_hook)
    attitude_publish = """\t\tatt.timestamp = _replay_mode ? timestamp : hrt_absolute_time();
\t\t_attitude_pub.publish(att);

\t}  else if (_replay_mode) {"""
    attitude_publish_hook = """\t\tatt.timestamp = _replay_mode ? timestamp : hrt_absolute_time();
\t\tresearch_publication_attempted = true;
\t\tresearch_publication_succeeded = _attitude_pub.publish(att);

\t}  else if (_replay_mode) {"""
    value = replace_once(value, attitude_publish, attitude_publish_hook)
    replay_publish = """\t\tvehicle_attitude_s att{};
\t\t_attitude_pub.publish(att);
\t}
}

#if defined(CONFIG_EKF2_BAROMETER)"""
    replay_publish_hook = """\t\tvehicle_attitude_s att{};
\t\tresearch_publication_attempted = true;
\t\tresearch_publication_succeeded = _attitude_pub.publish(att);
\t}

\tresearch_disposition_ekf_attitude(this, DispositionEkfAttitudeObservation{
\t\ttimestamp, _instance, research_attitude_valid, research_publication_attempted,
\t\tresearch_publication_succeeded});
}

#if defined(CONFIG_EKF2_BAROMETER)"""
    value = replace_once(value, replay_publish, replay_publish_hook)
    ekf.write_text(value)

    selector = source / "src/modules/ekf2/EKF2Selector.cpp"
    value = selector.read_text()
    value = replace_once(value, '#include "SelectorState.hpp"\n',
                         '#include "SelectorState.hpp"\n#include "SelectorDisposition.hpp"\n')
    value = replace_once(value, "\tresearch_selector_start(this);\n}",
                         "\tresearch_selector_start(this);\n"
                         "\tresearch_disposition_selector_start(this);\n}")
    value = replace_once(value,
                         "\tStop();\n\tresearch_selector_end(this);\n}",
                         "\tStop();\n\tresearch_disposition_selector_end(this);\n"
                         "\tresearch_selector_end(this);\n}")
    attitude_update = """\tif (_instance[_selected_instance].estimator_attitude_sub.update(&attitude)) {
\t\tconst uint64_t research_estimator_publication_us = attitude.timestamp;"""
    attitude_update_hook = """\tconst bool research_attitude_candidate_available =
\t\t_instance[_selected_instance].estimator_attitude_sub.update(&attitude);

\tif (research_attitude_candidate_available) {
\t\tconst uint64_t research_estimator_publication_us = attitude.timestamp;"""
    value = replace_once(value, attitude_update, attitude_update_hook)
    attitude_gate = """\t\tbool publish = true;

\t\t// ensure monotonically increasing timestamp_sample through reset, don't publish
\t\t//  estimator's attitude for system (vehicle_attitude) if it's stale
\t\tif ((attitude.timestamp_sample <= _attitude_last.timestamp_sample)
\t\t    || (hrt_elapsed_time(&attitude.timestamp) > 10_ms)) {

\t\t\tpublish = false;
\t\t}

\t\t// save last primary estimator_attitude as published with original resets"""
    attitude_gate_hook = """\t\tconst uint64_t research_previous_sample_us = _attitude_last.timestamp_sample;
\t\tconst bool research_monotonic = attitude.timestamp_sample > research_previous_sample_us;
\t\tconst uint64_t research_age_us = hrt_elapsed_time(&attitude.timestamp);
\t\tconst bool research_fresh = research_age_us <= 10_ms;
\t\tconst bool publish = research_monotonic && research_fresh;
\t\tresearch_disposition_attitude(this, DispositionSelectorOutputObservation{
\t\t\tattitude.timestamp_sample, attitude.timestamp, research_previous_sample_us,
\t\t\tresearch_age_us, {}, _selected_instance,
\t\t\t_instance[_selected_instance].estimator_attitude_sub.get_instance(), true,
\t\t\tresearch_monotonic, research_fresh, publish});

\t\t// save last primary estimator_attitude as published with original resets"""
    value = replace_once(value, attitude_gate, attitude_gate_hook)
    attitude_end = """\t\t}
\t}
}

void EKF2Selector::PublishVehicleLocalPosition()"""
    attitude_end_hook = """\t\t}
\t}

\tif (!research_attitude_candidate_available) {
\t\tresearch_disposition_attitude(this, DispositionSelectorOutputObservation{
\t\t\t0, 0, 0, 0, {}, _selected_instance,
\t\t\t_instance[_selected_instance].estimator_attitude_sub.get_instance(),
\t\t\tfalse, false, false, false});
\t}
}

void EKF2Selector::PublishVehicleLocalPosition()"""
    value = replace_once(value, attitude_end, attitude_end_hook)
    local_update = """\tif (_instance[_selected_instance].estimator_local_position_sub.update(&local_position)) {
\t\tconst uint64_t research_estimator_publication_us = local_position.timestamp;"""
    local_update_hook = """\tconst bool research_local_position_candidate_available =
\t\t_instance[_selected_instance].estimator_local_position_sub.update(&local_position);

\tif (research_local_position_candidate_available) {
\t\tconst uint64_t research_estimator_publication_us = local_position.timestamp;"""
    value = replace_once(value, local_update, local_update_hook)
    local_gate = """\t\tbool publish = true;

\t\t// ensure monotonically increasing timestamp_sample through reset, don't publish
\t\t//  estimator's local position for system (vehicle_local_position) if it's stale
\t\tif ((local_position.timestamp_sample <= _local_position_last.timestamp_sample)
\t\t    || (hrt_elapsed_time(&local_position.timestamp) > 20_ms)) {

\t\t\tpublish = false;
\t\t}

\t\t// save last primary estimator_local_position as published with original resets"""
    local_gate_hook = """\t\tconst uint64_t research_previous_sample_us = _local_position_last.timestamp_sample;
\t\tconst bool research_monotonic = local_position.timestamp_sample > research_previous_sample_us;
\t\tconst uint64_t research_age_us = hrt_elapsed_time(&local_position.timestamp);
\t\tconst bool research_fresh = research_age_us <= 20_ms;
\t\tconst bool publish = research_monotonic && research_fresh;
\t\tresearch_disposition_local_position(this, DispositionSelectorOutputObservation{
\t\t\tlocal_position.timestamp_sample, local_position.timestamp, research_previous_sample_us,
\t\t\tresearch_age_us, {research_source_acceleration[0], research_source_acceleration[1],
\t\t\t research_source_acceleration[2]}, _selected_instance,
\t\t\t_instance[_selected_instance].estimator_local_position_sub.get_instance(), true,
\t\t\tresearch_monotonic, research_fresh, publish});

\t\t// save last primary estimator_local_position as published with original resets"""
    value = replace_once(value, local_gate, local_gate_hook)
    local_end = """\t\t}
\t}
}

void EKF2Selector::PublishVehicleOdometry()"""
    local_end_hook = """\t\t}
\t}

\tif (!research_local_position_candidate_available) {
\t\tresearch_disposition_local_position(this, DispositionSelectorOutputObservation{
\t\t\t0, 0, 0, 0, {}, _selected_instance,
\t\t\t_instance[_selected_instance].estimator_local_position_sub.get_instance(),
\t\t\tfalse, false, false, false});
\t}
}

void EKF2Selector::PublishVehicleOdometry()"""
    value = replace_once(value, local_end, local_end_hook)
    selector.write_text(value)

    cmake = source / "src/modules/ekf2/CMakeLists.txt"
    cmake.write_text(cmake.read_text() + """
target_compile_definitions(modules__ekf2 PRIVATE
    RESEARCH_P2H_SELECTOR_DISPOSITION=1)
target_include_directories(modules__ekf2 PRIVATE
    "${PX4_SOURCE_DIR}/Tools/research_sitl/provenance/selector_disposition")
target_link_libraries(modules__ekf2 PRIVATE modules__research_selector_disposition)
""")
    return source, p2g_hashes


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
    for ending in ("/SelectorDisposition.cpp", "/SelectorState.cpp", "/EKF2.cpp",
                   "/EKF2Selector.cpp", "/research_selector_provenance.cpp"):
        selected = [command for command in commands if command["file"].endswith(ending)]
        require(len(selected) == 1
                and "-DRESEARCH_P2H_SELECTOR_DISPOSITION=1" in selected[0]["command"],
                "missing P2H definition: " + ending)
    return dict(builtins=sorted(apps), boardconfig_sha256=base.digest(build / "boardconfig"),
                compile_commands_sha256=base.digest(build / "compile_commands.json"),
                selector_enabled=True, disposition_observer=True, admission="not_linked",
                control_modules="not_linked", output_drivers="not_linked")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True,
                        help="independently verified final P2G evidence directory")
    parser.add_argument("--configuration", choices=("Debug", "Release", "Sanitize"), default="Debug")
    args = parser.parse_args()
    baseline = args.baseline.resolve()
    baseline_check = p2g_check.check(baseline)
    require(baseline_check["verified"], "P2G baseline not independently verified")
    baseline_manifest = p2g_check.read(baseline / "manifest.json")
    sensor = baseline_manifest["sensor_provenance"]
    source_fixture = baseline / "run-normal/imu.txt"
    require(base.digest(source_fixture) == baseline_manifest["runs"][0]["fixture_sha256"],
            "P2G fixture drift")
    area = Path(tempfile.mkdtemp(prefix="p2h-disposition-", dir="/var/tmp"))
    print("P2H_AREA=" + str(area), flush=True)
    before = base.source_hashes()
    base.save(area / "source_hashes.json", before)
    source, p2g_hashes = view(area)
    build = area / "build"
    archive = area / "inputs"
    archive.mkdir()
    prefixes = (PREFIX + "selector_state/", PREFIX + "selector_disposition/")
    for rel in before:
        if rel.startswith(prefixes):
            destination = archive / rel
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / rel, destination)
    disposition_prefix = PREFIX + "selector_disposition/"
    manifest = dict(schema="p2h-selector-disposition-build/v1", status="incomplete",
                    source=str(ROOT), px4_head=base.git("rev-parse", "HEAD"),
                    configuration=args.configuration, runs=[], sensor_provenance=sensor,
                    source_hashes_sha256=base.digest(area / "source_hashes.json"),
                    baseline=dict(path=str(baseline), manifest_sha256=base.digest(baseline / "manifest.json"),
                                  verification=baseline_check,
                                  fixture_sha256=base.digest(source_fixture)),
                    overlay={rel: dict(original=digest, p2g_derived=p2g_hashes[rel],
                                       p2h_derived=base.digest(source / rel))
                             for rel, digest in PINS.items()})
    environment = dict(os.environ, CCACHE_DISABLE="1", CCACHE_DIR=str(area / "ccache"),
                       PYTHONDONTWRITEBYTECODE="1", GIT_CONFIG_COUNT="1",
                       GIT_CONFIG_KEY_0="core.autocrlf", GIT_CONFIG_VALUE_0="true",
                       GIT_SUBMODULES_ARE_EVIL="1", GIT_TERMINAL_PROMPT="0",
                       GIT_ALLOW_PROTOCOL="file")
    if args.configuration == "Sanitize":
        environment.update(ASAN_OPTIONS="detect_leaks=0:halt_on_error=1",
                           UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
    configure = ["cmake", "-S", str(source), "-B", str(build), "-G", "Ninja",
                 "-DCONFIG=px4_sitl_p2h-default", "-DVENDOR=px4", "-DMODEL=sitl",
                 "-DLABEL=p2h-default", f"-DPX4_BOARD_DIR={source}/boards/px4/sitl",
                 f"-DPX4_CONFIG_FILE={source}/{disposition_prefix}p2h-default.px4board",
                 f"-DEXTERNAL_MODULES_LOCATION={source}/{disposition_prefix}px4_external",
                 "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"] + p2g.configuration_arguments(args.configuration)
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
                               if not key.startswith(("PX4_", "SIM_", "GZ_", "P2B_", "P2E_",
                                                      "P2F_", "P2G_", "P2H_", "LD_"))
                               and key not in ("replay", "replay_mode")}
            run_environment.update(P2H_RUN_ID=run_id,
                                   P2H_ACCEL_DEVICE_ID=str(sensor["accelerometer_device_id"]),
                                   P2H_GYRO_DEVICE_ID=str(sensor["gyroscope_device_id"]))
            command_line = [str(build / "bin/px4"), "-i", str(secrets.randbelow(50000) + 10000),
                            "-w", str(runtime), "-s", str(runtime / "startup.sh")]
            run = dict(case=case, run_id=run_id, command=command_line, forced_shutdown=False,
                       fixture_sha256=base.digest(runtime / "imu.txt"), sensor_provenance=sensor)
            manifest["runs"].append(run)
            with (runtime / "stdout.log").open("w") as log:
                child = subprocess.Popen(command_line, cwd=runtime, env=run_environment,
                                         stdin=subprocess.PIPE, stdout=log, stderr=subprocess.STDOUT,
                                         text=True, start_new_session=True)
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
            marker = "P2H_CAPTURE_COMPLETE" if case == "normal" else "P2H_EXPECTED_RESTART_REJECTION"
            require(run["exit_code"] == 0 and marker in log_text and "ERROR [" not in log_text,
                    "native case incomplete")
            artifacts = ("startup.sh", "imu.txt", "stdout.log", "selector.json",
                         "feeder.json", "disposition.json")
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
