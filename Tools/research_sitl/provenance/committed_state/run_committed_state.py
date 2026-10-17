#!/usr/bin/env python3
"""Build a private committed EKF output/provenance observer; no admission path."""

import argparse
import importlib.util
import json
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
spec = importlib.util.spec_from_file_location("p2d", HERE.parent / "native_merge/run_native_merge.py")
p2d = importlib.util.module_from_spec(spec)
spec.loader.exec_module(p2d)
p2b = p2d.p2b
base = p2d.base
require = p2d.require
PINS = dict(p2d.PINS)


def replace_once(text, before, after):
    require(text.count(before) == 1, "private P2E seam drift: " + before[:80])
    return text.replace(before, after)


def source_sensor_provenance(ulog_path):
    require(base.digest(ulog_path) == p2b.ULOG_HASH, "archived ULog differs")
    ulog = p2b.ULog(str(ulog_path), message_name_filter_list=["sensor_selection", "sensor_combined"])
    require(not ulog.file_corruption, "corrupt ULog")
    selections = [d for d in ulog.data_list if d.name == "sensor_selection" and d.multi_id == 0]
    combined = [d for d in ulog.data_list if d.name == "sensor_combined" and d.multi_id == 0]
    require(len(selections) == 1 and len(combined) == 1, "missing or ambiguous recorded sensor provenance")
    selection = selections[0].data
    samples = combined[0].data
    require(len(selection["timestamp"]) == 1 and len(samples["timestamp"]) >= 1200,
            "unsupported recorded sensor provenance")
    accel_id = int(selection["accel_device_id"][0])
    gyro_id = int(selection["gyro_device_id"][0])
    accel_counts = {int(x) for x in samples["accel_calibration_count"][:1200]}
    gyro_counts = {int(x) for x in samples["gyro_calibration_count"][:1200]}
    require(0 < accel_id < 1 << 32 and 0 < gyro_id < 1 << 32
            and len(accel_counts) == 1 and len(gyro_counts) == 1, "unstable fixture sensor provenance")
    return dict(accelerometer_device_id=accel_id, gyroscope_device_id=gyro_id,
                accelerometer_calibration_count=accel_counts.pop(),
                gyroscope_calibration_count=gyro_counts.pop(), source_sha256=p2b.ULOG_HASH,
                selection_topic="sensor_selection", selection_instance=0,
                sample_topic="sensor_combined", sample_instance=0)


def view(area):
    require(base.digest(HERE.parent / "native_merge/run_native_merge.py") ==
            "331f05bd0919b7922bb2239379ef32484541efe451d080b89b7651169387b37d",
            "P2D helper drift")
    source = p2d.view(area)
    cpp = source / "src/modules/ekf2/EKF2.cpp"
    value = cpp.read_text()
    value = replace_once(value,
                         "\tresearch_merge_source_start(this, _replay_mode, _multi_mode);",
                         "\tresearch_merge_source_start(this, _replay_mode, _multi_mode);\n"
                         "\tresearch_committed_source_start(this, _replay_mode, _multi_mode);")
    value = replace_once(value,
                         "\tresearch_merge_source_end(this);",
                         "\tresearch_merge_source_end(this);\n\tresearch_committed_source_end(this);")
    value = replace_once(value, "\t\tif (_ekf.update()) {",
                         "\t\tconst bool research_update_committed = _ekf.update();\n\n"
                         "\t\tif (research_update_committed) {")
    attitude_publish = "\t\tatt.timestamp = _replay_mode ? timestamp : hrt_absolute_time();\n\t\t_attitude_pub.publish(att);"
    attitude_hook = """\t\tresearch_committed_attitude(this, CommittedAttitudeObservation{
\t\t\ttimestamp, {att.q[0], att.q[1], att.q[2], att.q[3]}, att.quat_reset_counter});

"""
    value = replace_once(value, attitude_publish,
                         "\t\tatt.timestamp = _replay_mode ? timestamp : hrt_absolute_time();\n"
                         + attitude_hook + "\t\t_attitude_pub.publish(att);")
    local_publish = "\t_local_position_pub.publish(lpos);"
    local_hook = """\tresearch_committed_local_position(this, CommittedLocalPositionObservation{
\t\ttimestamp,
\t\t{lpos.x, lpos.y, lpos.z},
\t\t{lpos.vx, lpos.vy, lpos.vz},
\t\t{lpos.ax, lpos.ay, lpos.az},
\t\tlpos.xy_valid, lpos.v_xy_valid, lpos.z_valid, lpos.v_z_valid,
\t\tlpos.xy_reset_counter, lpos.vxy_reset_counter, lpos.z_reset_counter,
\t\tlpos.vz_reset_counter, lpos.heading_reset_counter});

"""
    value = replace_once(value, local_publish, local_hook + local_publish)
    attitude_call = "\t\tPublishAttitude(now); // publish attitude immediately (uses quaternion from output predictor)"
    iteration_hook = """

\t\tresearch_committed_iteration(this, EstimatorCommitObservation{
\t\t\tnow, _ekf.control_status().value, _ekf.fault_status().value,
\t\t\t_device_id_accel, _device_id_gyro, _accel_calibration_count, _gyro_calibration_count,
\t\t\t_ekf.get_quat_reset_count(), _ekf.get_posNE_reset_count(), _ekf.get_velNE_reset_count(),
\t\t\t_ekf.get_posD_reset_count(), _ekf.get_velD_reset_count(), research_update_committed,
\t\t\t_replay_mode, _multi_mode, _ekf.attitude_valid(), _ekf.isLocalHorizontalPositionValid(),
\t\t\t_ekf.isLocalHorizontalPositionValid(), _ekf.isLocalVerticalPositionValid(),
\t\t\t_ekf.isLocalVerticalVelocityValid()});"""
    value = replace_once(value, attitude_call, attitude_call + iteration_hook)
    cpp.write_text('#include "CommittedState.hpp"\n' + value)
    cmake = source / "src/modules/ekf2/CMakeLists.txt"
    cmake.write_text(cmake.read_text() + """
target_compile_definitions(modules__ekf2 PRIVATE RESEARCH_P2E_COMMITTED_STATE=1)
target_include_directories(modules__ekf2 PRIVATE "${PX4_SOURCE_DIR}/Tools/research_sitl/provenance/committed_state")
""")

    feeder = source / (PREFIX + "native/px4_external/src/research_provenance/research_provenance.cpp")
    value = feeder.read_text()
    value = '#include "CommittedState.hpp"\n' + value
    value = replace_once(value, "#include <uORB/topics/sensor_combined.h>",
                         "#include <uORB/topics/sensor_combined.h>\n#include <uORB/topics/sensor_selection.h>")
    value = replace_once(value, "bool bootstrapped{}, fed{}, exported{};",
                         "bool bootstrapped{}, fed{}, exported{}, sensors_bound{};\n"
                         "uint32_t bound_accelerometer{}, bound_gyroscope{};")
    begin = "\tif (!research_merge_begin(run_id)) { fail(Fault::Input); return -1; }"
    value = replace_once(value, begin,
                         begin + "\n\tif (!research_committed_begin(run_id)) { fail(Fault::Input); return -1; }")
    feed_start = "\tfed = true;\n\tuORB::Publication<sensor_combined_s> publication{ORB_ID(sensor_combined)};"
    feed_replacement = """\tfed = true;
\tif (!sensors_bound) { fail(Fault::Input); return -1; }
\tuORB::Publication<sensor_selection_s> sensor_publication{ORB_ID(sensor_selection)};
\tsensor_selection_s sensor_selection{};
\tsensor_selection.timestamp = hrt_absolute_time();
\tsensor_selection.accel_device_id = bound_accelerometer;
\tsensor_selection.gyro_device_id = bound_gyroscope;
\tif (!sensor_publication.publish(sensor_selection)) { fail(Fault::Input); return -1; }
\tpx4_usleep(FixtureStepUs);
\tuORB::Publication<sensor_combined_s> publication{ORB_ID(sensor_combined)};"""
    value = replace_once(value, feed_start, feed_replacement)
    prepare = "\treturn research_merge_prepare_stop() ? 0 : -1;"
    value = replace_once(value, prepare,
                         "\tconst bool committed_ready = research_committed_prepare_stop();\n"
                         "\tconst bool merge_ready = research_merge_prepare_stop();\n"
                         "\treturn committed_ready && merge_ready ? 0 : -1;")
    finish = '\tif (!research_merge_finish("merged.json")) { fail(Fault::Sink); return -1; }'
    value = replace_once(value, finish,
                         '\tconst bool committed_ok = research_committed_finish("committed.json");\n'
                         '\tconst bool merge_ok = research_merge_finish("merged.json");\n'
                         '\tif (!committed_ok || !merge_ok) { fail(Fault::Sink); return -1; }')
    export_marker = "int export_records(const char *path)"
    bind_function = """int bind_sensors(const char *accelerometer, const char *gyroscope)
{
\tuint64_t accel{}, gyro{};
\tif (!bootstrapped || fed || sensors_bound || !positive(accelerometer, accel) || !positive(gyroscope, gyro)
\t    || accel >= (uint64_t{1} << 32) || gyro >= (uint64_t{1} << 32)) { return -1; }
\tbound_accelerometer = static_cast<uint32_t>(accel);
\tbound_gyroscope = static_cast<uint32_t>(gyro);
\tsensors_bound = true;
\treturn 0;
}

"""
    value = replace_once(value, export_marker, bind_function + export_marker)
    command = '\tif (argc == 2 && strcmp(argv[1], "feed") == 0) { return feed(); }'
    value = replace_once(value, command,
                         '\tif (argc == 4 && strcmp(argv[1], "sensors") == 0) { return bind_sensors(argv[2], argv[3]); }\n\n'
                         + command)
    feeder.write_text(value)
    return source


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ulog", type=Path, required=True)
    args = parser.parse_args()
    ulog = args.ulog.resolve()
    sensor_provenance = source_sensor_provenance(ulog)
    area = Path(tempfile.mkdtemp(prefix="p2e-committed-", dir="/var/tmp"))
    print("P2E_AREA=" + str(area), flush=True)
    before = base.source_hashes()
    base.save(area / "source_hashes.json", before)
    source = view(area)
    build = area / "build"
    archive = area / "inputs"
    archive.mkdir()
    prefixes = tuple(PREFIX + part + "/" for part in ("native", "merge", "native_merge", "committed_state"))
    for rel in before:
        if rel.startswith(prefixes):
            destination = archive / rel
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / rel, destination)
    manifest = dict(schema="p2e-committed-build/v1", status="incomplete", source=str(ROOT),
                    px4_head=base.git("rev-parse", "HEAD"), runs=[], sensor_provenance=sensor_provenance,
                    source_hashes_sha256=base.digest(area / "source_hashes.json"),
                    overlay={rel: dict(original=digest, derived=base.digest(source / rel))
                             for rel, digest in PINS.items()})
    env = dict(os.environ, CCACHE_DISABLE="1", CCACHE_DIR=str(area / "ccache"), PYTHONDONTWRITEBYTECODE="1",
               GIT_CONFIG_COUNT="1", GIT_CONFIG_KEY_0="core.autocrlf", GIT_CONFIG_VALUE_0="true",
               GIT_SUBMODULES_ARE_EVIL="1", GIT_TERMINAL_PROMPT="0", GIT_ALLOW_PROTOCOL="file")
    commands = [
        ("configure", ["cmake", "-S", str(source), "-B", str(build), "-G", "Ninja",
                       "-DCONFIG=px4_sitl_p2e-default", "-DVENDOR=px4", "-DMODEL=sitl", "-DLABEL=p2e-default",
                       f"-DPX4_BOARD_DIR={source}/boards/px4/sitl",
                       f"-DPX4_CONFIG_FILE={source}/{PREFIX}native/p2b-default.px4board",
                       f"-DEXTERNAL_MODULES_LOCATION={source}/{PREFIX}committed_state/px4_external",
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
                           if not key.startswith(("PX4_", "SIM_", "GZ_", "P2B_", "P2E_", "LD_"))
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
            marker = "P2E_CAPTURE_COMPLETE" if case == "normal" else "P2E_EXPECTED_RESTART_REJECTION"
            require(run["exit_code"] == 0 and marker in log_text and "ERROR [" not in log_text,
                    "native case incomplete")
            artifacts = ["startup.sh", "imu.txt", "stdout.log", "merged.json", "committed.json"]
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
