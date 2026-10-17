#!/usr/bin/env python3
"""Build a private single-instance EKF selection/status observer; no admission path."""

import argparse
import importlib.util
import os
from pathlib import Path
import secrets
import shutil
import signal
import subprocess
import tempfile


HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
PREFIX = "Tools/research_sitl/provenance/"
spec = importlib.util.spec_from_file_location(
    "p2e", HERE.parent / "committed_state/run_committed_state.py")
p2e = importlib.util.module_from_spec(spec)
spec.loader.exec_module(p2e)
p2b = p2e.p2b
base = p2e.base
require = p2e.require
PINS = dict(p2e.PINS)


def replace_once(text, before, after):
    require(text.count(before) == 1, "private P2F seam drift: " + before[:80])
    return text.replace(before, after)


def view(area):
    require(base.digest(HERE.parent / "committed_state/run_committed_state.py") ==
            "72b2820af2dbf916ce3aab2440d8319ec52019e761d7ac06c0ce07f571a34a2a",
            "P2E helper drift")
    source = p2e.view(area)
    cpp = source / "src/modules/ekf2/EKF2.cpp"
    value = cpp.read_text()
    constructor = ("\tresearch_committed_source_start(this, _replay_mode, _multi_mode);\n"
                   "\tinitFusionControl();\n\tAdvertiseTopics();")
    selected_start = constructor + """
	research_selected_source_start(this, _instance, _attitude_pub.get_instance(),
		_local_position_pub.get_instance(), _estimator_status_flags_pub.get_instance(),
		_replay_mode, _multi_mode);"""
    value = replace_once(value, constructor, selected_start)
    value = replace_once(value, "\tresearch_committed_source_end(this);",
                         "\tresearch_selected_source_end(this);\n\tresearch_committed_source_end(this);")
    status_publish = "\t\t_estimator_status_flags_pub.publish(status_flags);"
    status_hook = """		const bool research_status_published = _estimator_status_flags_pub.publish(status_flags);
		research_selected_status(this, EstimatorStatusPublicationObservation{
			status_flags.timestamp_sample, status_flags.timestamp, _filter_control_status,
			_filter_fault_status, status_flags.control_status_changes,
			status_flags.fault_status_changes, _instance,
			_estimator_status_flags_pub.get_instance(), research_status_published});"""
    value = replace_once(value, status_publish, status_hook)
    iteration_end = "\t\t\t_ekf.isLocalVerticalVelocityValid()});"
    selected_iteration = iteration_end + """

		research_selected_iteration(this, SelectedEstimatorIterationObservation{
			now, _ekf.control_status().value, _filter_control_status,
			_ekf.fault_status().value, _filter_fault_status,
			_filter_control_status_changes, _filter_fault_status_changes,
			_instance, research_update_committed, _replay_mode, _multi_mode});"""
    value = replace_once(value, iteration_end, selected_iteration)
    cpp.write_text('#include "SelectedState.hpp"\n' + value)
    cmake = source / "src/modules/ekf2/CMakeLists.txt"
    cmake.write_text(cmake.read_text() + """
target_compile_definitions(modules__ekf2 PRIVATE RESEARCH_P2F_SELECTED_STATE=1)
target_include_directories(modules__ekf2 PRIVATE "${PX4_SOURCE_DIR}/Tools/research_sitl/provenance/selected_state")
""")

    feeder = source / (PREFIX + "native/px4_external/src/research_provenance/research_provenance.cpp")
    value = '#include "SelectedState.hpp"\n' + feeder.read_text()
    begin = "\tif (!research_committed_begin(run_id)) { fail(Fault::Input); return -1; }"
    value = replace_once(value, begin,
                         begin + "\n\tif (!research_selected_begin(run_id)) { fail(Fault::Input); return -1; }")
    prepare = ("\tconst bool committed_ready = research_committed_prepare_stop();\n"
               "\tconst bool merge_ready = research_merge_prepare_stop();\n"
               "\treturn committed_ready && merge_ready ? 0 : -1;")
    selected_prepare = ("\tconst bool selected_ready = research_selected_prepare_stop();\n"
                        "\tconst bool committed_ready = research_committed_prepare_stop();\n"
                        "\tconst bool merge_ready = research_merge_prepare_stop();\n"
                        "\treturn selected_ready && committed_ready && merge_ready ? 0 : -1;")
    value = replace_once(value, prepare, selected_prepare)
    finish = ('\tconst bool committed_ok = research_committed_finish("committed.json");\n'
              '\tconst bool merge_ok = research_merge_finish("merged.json");\n'
              '\tif (!committed_ok || !merge_ok) { fail(Fault::Sink); return -1; }')
    selected_finish = ('\tconst bool selected_ok = research_selected_finish("selected.json");\n'
                       '\tconst bool committed_ok = research_committed_finish("committed.json");\n'
                       '\tconst bool merge_ok = research_merge_finish("merged.json");\n'
                       '\tif (!selected_ok || !committed_ok || !merge_ok) { fail(Fault::Sink); return -1; }')
    value = replace_once(value, finish, selected_finish)
    feeder.write_text(value)
    return source


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ulog", type=Path, required=True)
    args = parser.parse_args()
    ulog = args.ulog.resolve()
    sensor_provenance = p2e.source_sensor_provenance(ulog)
    area = Path(tempfile.mkdtemp(prefix="p2f-selected-", dir="/var/tmp"))
    print("P2F_AREA=" + str(area), flush=True)
    before = base.source_hashes()
    base.save(area / "source_hashes.json", before)
    source = view(area)
    build = area / "build"
    archive = area / "inputs"
    archive.mkdir()
    prefixes = tuple(PREFIX + part + "/" for part in
                     ("native", "merge", "native_merge", "committed_state", "selected_state"))
    for rel in before:
        if rel.startswith(prefixes):
            destination = archive / rel
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / rel, destination)
    manifest = dict(schema="p2f-selected-build/v1", status="incomplete", source=str(ROOT),
                    px4_head=base.git("rev-parse", "HEAD"), runs=[], sensor_provenance=sensor_provenance,
                    source_hashes_sha256=base.digest(area / "source_hashes.json"),
                    overlay={rel: dict(original=digest, derived=base.digest(source / rel))
                             for rel, digest in PINS.items()})
    env = dict(os.environ, CCACHE_DISABLE="1", CCACHE_DIR=str(area / "ccache"),
               PYTHONDONTWRITEBYTECODE="1", GIT_CONFIG_COUNT="1", GIT_CONFIG_KEY_0="core.autocrlf",
               GIT_CONFIG_VALUE_0="true", GIT_SUBMODULES_ARE_EVIL="1", GIT_TERMINAL_PROMPT="0",
               GIT_ALLOW_PROTOCOL="file")
    commands = [
        ("configure", ["cmake", "-S", str(source), "-B", str(build), "-G", "Ninja",
                       "-DCONFIG=px4_sitl_p2f-default", "-DVENDOR=px4", "-DMODEL=sitl", "-DLABEL=p2f-default",
                       f"-DPX4_BOARD_DIR={source}/boards/px4/sitl",
                       f"-DPX4_CONFIG_FILE={source}/{PREFIX}native/p2b-default.px4board",
                       f"-DEXTERNAL_MODULES_LOCATION={source}/{PREFIX}selected_state/px4_external",
                       "-DCMAKE_BUILD_TYPE=Debug", "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"]),
        ("build", ["cmake", "--build", str(build), "--target", "px4", "-j", "4"])]
    try:
        for name, command_line in commands:
            print(name, flush=True)
            with (area / (name + ".log")).open("w") as log:
                code = base.bounded_command(command_line, source, env, log)
            manifest[name] = dict(command=command_line, exit_code=code,
                                  log_sha256=base.digest(area / (name + ".log")))
            require(code == 0, name + " failed")
            manifest["configuration_audit"] = p2b.audit(build)
            base.save(area / "manifest.json", manifest)
        manifest["executable_sha256"] = base.digest(build / "bin/px4")
        for case in ("normal", "restart"):
            runtime = area / ("run-" + case)
            runtime.mkdir()
            stimulus = p2b.fixture(ulog, runtime / "imu.txt")
            shutil.copy2(HERE / (case + ".sh"), runtime / "startup.sh")
            run_id = str(secrets.randbits(63) + 1)
            environment = {key: value for key, value in os.environ.items()
                           if not key.startswith(("PX4_", "SIM_", "GZ_", "P2B_", "P2E_", "P2F_", "LD_"))
                           and key not in ("replay", "replay_mode")}
            environment.update(P2B_RUN_ID=run_id,
                               P2E_ACCEL_DEVICE_ID=str(sensor_provenance["accelerometer_device_id"]),
                               P2E_GYRO_DEVICE_ID=str(sensor_provenance["gyroscope_device_id"]))
            command_line = [str(build / "bin/px4"), "-i", str(secrets.randbelow(50000) + 10000),
                            "-w", str(runtime), "-s", str(runtime / "startup.sh")]
            run = dict(case=case, run_id=run_id, command=command_line, fixture=stimulus,
                       sensor_provenance=sensor_provenance, forced_shutdown=False)
            manifest["runs"].append(run)
            with (runtime / "stdout.log").open("w") as log:
                child = subprocess.Popen(command_line, cwd=runtime, env=environment, stdin=subprocess.PIPE,
                                         stdout=log, stderr=subprocess.STDOUT, text=True, start_new_session=True)
                try:
                    child.communicate("shutdown\n", timeout=30)
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
            marker = "P2F_CAPTURE_COMPLETE" if case == "normal" else "P2F_EXPECTED_RESTART_REJECTION"
            require(run["exit_code"] == 0 and marker in log_text and "ERROR [" not in log_text,
                    "native case incomplete")
            artifacts = ["startup.sh", "imu.txt", "stdout.log", "merged.json", "committed.json", "selected.json"]
            if case == "normal":
                artifacts.append("observations.json")
            run["artifacts"] = {name: base.digest(runtime / name) for name in artifacts}
        require(base.source_hashes() == before, "source changed during build/run")
        manifest["status"] = "captured_unqualified"
    except BaseException as error:
        manifest["status"] = "failed"
        manifest["error"] = str(error)
        raise
    finally:
        base.save(area / "manifest.json", manifest)


if __name__ == "__main__":
    main()
