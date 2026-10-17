#!/usr/bin/env python3
"""Build/run a private EKF observation profile; no admission or control modules."""

import argparse
import hashlib
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
import time

from pyulog import ULog

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
spec = importlib.util.spec_from_file_location("passive_build", ROOT / "Tools/research_sitl/px4_attachment/run_probe.py")
base = importlib.util.module_from_spec(spec)
spec.loader.exec_module(base)
BASE_HASHES = {
    "src/modules/ekf2/EKF2.cpp": "1c1e2bca5b9e33aaee146c078b95f5b6e2bd87734223f72a2e0505b300d94ef5",
    "src/modules/ekf2/CMakeLists.txt": "fccb276512fc59150318b1d2fe689c6fca63c2f6cb24972b209a9b62cd1b74dd",
}
ULOG_HASH = "62665d68f92ceb47068a5b5f16a1407998e0f2cf08049e98fc55cf61e90c8143"
BUILTINS = {"ekf2", "research_provenance", "param", "shutdown", "uorb", "ver", "list_tasks", "list_files", "sleep"}


def require(condition, reason):
    if not condition:
        raise RuntimeError(reason)


def view(area):
    result = area / "source"
    result.mkdir()
    private = {"src", "src/modules", "src/modules/ekf2"}
    def populate(source, target, prefix=""):
        for item in source.iterdir():
            rel = prefix + item.name
            dest = target / item.name
            if rel in ("build", ".vscode"):
                dest.mkdir()
            elif rel in private:
                dest.mkdir(); populate(item, dest, rel + "/")
            elif rel in BASE_HASHES:
                require(base.digest(item) == BASE_HASHES[rel], "unreviewed overlay source: " + rel)
                dest.write_text(item.read_text())
            else:
                dest.symlink_to(item, target_is_directory=item.is_dir())
    populate(ROOT, result)
    cpp = result / "src/modules/ekf2/EKF2.cpp"
    original = cpp.read_text()
    seam = "\t\tPublishAttitude(now); // publish attitude immediately (uses quaternion from output predictor)"
    require(original.count(seam) == 1, "EKF capture seam drift")
    call = """\t\tresearch_capture_ekf(this, now, _replay_mode, _multi_mode, _ekf.attitude_valid(),
\t\t\t_ekf.isLocalHorizontalPositionValid(), _ekf.isLocalVerticalPositionValid(),
\t\t\t_ekf.isLocalVerticalVelocityValid(), _ekf.control_status().value, _ekf.fault_status().value);

"""
    cpp.write_text('#include "Receipt.hpp"\n' + original.replace(seam, call + seam))
    cmake = result / "src/modules/ekf2/CMakeLists.txt"
    cmake.write_text(cmake.read_text() + """
target_compile_definitions(modules__ekf2 PRIVATE RESEARCH_PROVENANCE_CAPTURE=1)
target_include_directories(modules__ekf2 PRIVATE "${PX4_SOURCE_DIR}/Tools/research_sitl/provenance/native")
target_link_libraries(modules__ekf2 PRIVATE modules__research_provenance)
""")
    return result


def audit(build):
    enabled = {x.split("=", 1)[0] for x in (build / "boardconfig").read_text().splitlines() if x.endswith("=y")}
    require({x for x in enabled if x.startswith("CONFIG_MODULES_")} == {"CONFIG_MODULES_EKF2"}, "unexpected modules")
    require(not any(x.startswith("CONFIG_DRIVERS_") for x in enabled), "driver selected")
    require("CONFIG_BOARD_NOLOCKSTEP" in enabled and "CONFIG_EKF2_MULTI_INSTANCE" not in enabled, "wrong clock/multi mode")
    commands = json.loads((build / "compile_commands.json").read_text())
    forbidden = ("-DENABLE_LOCKSTEP_SCHEDULER", "-DCONFIG_MUORB_APPS_SYNC_TIMESTAMP", "RESEARCH_C1E_NEGATIVE", "RESEARCH_SITL_DIAGNOSTIC")
    require(not any(token in c["command"] for token in forbidden for c in commands), "forbidden profile definition")
    apps = set(re.findall(r'apps\["([a-zA-Z0-9_]+)"\]', (build / "platforms/posix/apps.cpp").read_text()))
    require(apps == BUILTINS, "unexpected linked builtins: " + repr(apps))
    return {"builtins": sorted(apps), "boardconfig_sha256": base.digest(build / "boardconfig"),
            "compile_commands_sha256": base.digest(build / "compile_commands.json"),
            "admission": "not_linked", "control_modules": "not_linked", "output_drivers": "not_linked"}


def fixture(source, target):
    require(base.digest(source) == ULOG_HASH, "archived ULog differs")
    ulog = ULog(str(source), message_name_filter_list=["sensor_combined"])
    require(not ulog.file_corruption, "corrupt ULog")
    dataset = [d for d in ulog.data_list if d.name == "sensor_combined" and d.multi_id == 0]
    require(len(dataset) == 1, "missing/ambiguous IMU source")
    data = dataset[0].data
    count = 1200
    require(len(data["timestamp"]) >= count, "short IMU fixture")
    ints = ["gyro_integral_dt", "accelerometer_integral_dt"]
    floats = [f"gyro_rad[{i}]" for i in range(3)] + [f"accelerometer_m_s2[{i}]" for i in range(3)]
    meta = ["accelerometer_clipping", "gyro_clipping", "accel_calibration_count", "gyro_calibration_count"]
    lines = []
    for i in range(count):
        require(int(data["accelerometer_timestamp_relative"][i]) == 0, "unsupported relative IMU timestamp")
        require(all(int(data[key][i]) == 4000 for key in ints), "unsupported integration interval")
        values = [str(int(data[k][i])) for k in ints] + [format(float(data[k][i]), ".9g") for k in floats]
        values += [str(int(data[k][i])) for k in meta]
        lines.append(" ".join(values))
    target.write_text("\n".join(lines) + "\n")
    require(base.digest(source) == ULOG_HASH, "ULog changed during fixture extraction")
    return {"source_sha256": ULOG_HASH, "topic": "sensor_combined", "instance": 0,
            "first_index": 0, "count": count, "fixture_sha256": base.digest(target),
            "time_transform": "new hrt timestamp at each publication; recorded values and integration dt",
            "physical_sensor_evidence": False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ulog", required=True, type=Path)
    parser.add_argument("--resume-build", type=Path, help="resume only this runner's failed pre-runtime build")
    args = parser.parse_args()
    area = args.resume_build.resolve() if args.resume_build else Path(tempfile.mkdtemp(prefix="p2b-native-", dir="/var/tmp"))
    print("P2B_AREA=" + str(area), flush=True)
    before = base.source_hashes()
    if args.resume_build:
        require(area.parent == Path("/var/tmp") and area.name.startswith("p2b-native-"), "not an owned build area")
        old = json.loads((area / "manifest.json").read_text())
        require(old["schema"] == "p2b-native-build/v1" and old["status"] == "failed" and old["source"] == str(ROOT)
                and old["runs"] == [] and not list(area.glob("run-*")), "only failed pre-runtime builds may resume")
        prior = json.loads((area / "source_hashes.json").read_text())
        for rel in set(before) | set(prior):
            if not rel.startswith("Tools/research_sitl/provenance/native/"):
                require(before.get(rel) == prior.get(rel), "unrelated source drift: " + rel)
        cache = area / "build/CMakeCache.txt"
        if cache.exists():
            require(f"CMAKE_HOME_DIRECTORY:INTERNAL={area / 'source'}" in cache.read_text().splitlines(), "wrong cached source")
        else:
            require(not (area / "build/build.ninja").exists() and not (area / "build/bin/px4").exists(),
                    "completed build without source cache")
        for rel in BASE_HASHES:
            require(base.digest(ROOT / rel) == BASE_HASHES[rel], "original source changed")
            require(base.digest(area / "source" / rel) == old["overlay"][rel]["derived"], "private source drift")
        archive = Path(tempfile.mkdtemp(prefix="failed-attempt-", dir=area))
        for name in ("manifest.json", "source_hashes.json", "configure.log", "build.log"):
            if (area / name).exists(): shutil.copy2(area / name, archive / name)
    manifest = {"schema": "p2b-native-build/v1", "status": "incomplete", "source": str(ROOT),
                "px4_head": base.git("rev-parse", "HEAD"), "runs": []}
    base.save(area / "source_hashes.json", before)
    manifest["source_hashes_sha256"] = base.digest(area / "source_hashes.json")
    source = area / "source" if args.resume_build else view(area)
    build = area / "build"; attachment = source / HERE.relative_to(ROOT)
    inputs = Path(tempfile.mkdtemp(prefix="source-inputs-", dir=area))
    for p in HERE.rglob("*"):
        if p.is_file():
            target = inputs / p.relative_to(ROOT); target.parent.mkdir(parents=True, exist_ok=True); shutil.copy2(p, target)
    manifest["native_source_archive"] = str(inputs)
    manifest["overlay"] = {rel: {"original": value, "derived": base.digest(source / rel)} for rel, value in BASE_HASHES.items()}
    env = dict(os.environ, CCACHE_DISABLE="1", CCACHE_DIR=str(area / "ccache"), PYTHONDONTWRITEBYTECODE="1",
               GIT_CONFIG_COUNT="1", GIT_CONFIG_KEY_0="core.autocrlf", GIT_CONFIG_VALUE_0="true",
               GIT_SUBMODULES_ARE_EVIL="1", GIT_TERMINAL_PROMPT="0", GIT_ALLOW_PROTOCOL="file")
    commands = [
        ("configure", ["cmake", "-S", str(source), "-B", str(build), "-G", "Ninja", "-DCONFIG=px4_sitl_p2b-default",
                       "-DVENDOR=px4", "-DMODEL=sitl", "-DLABEL=p2b-default", f"-DPX4_BOARD_DIR={source}/boards/px4/sitl",
                       f"-DPX4_CONFIG_FILE={attachment}/p2b-default.px4board",
                       f"-DEXTERNAL_MODULES_LOCATION={attachment}/px4_external", "-DCMAKE_BUILD_TYPE=Debug",
                       "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"]),
        ("build", ["cmake", "--build", str(build), "--target", "px4", "-j", "4"])]
    try:
        for name, command in commands:
            print(name + ": " + str(area / (name + ".log")), flush=True)
            with (area / (name + ".log")).open("w") as log:
                code = base.bounded_command(command, source, env, log)
            manifest[name] = {"command": command, "exit_code": code}
            require(code == 0, name + " failed; see retained log")
            manifest["configuration_audit"] = audit(build)
            base.save(area / "manifest.json", manifest)
        manifest["executable_sha256"] = base.digest(build / "bin/px4")
        runtime = area / "run-1"; runtime.mkdir()
        manifest["fixture"] = fixture(args.ulog.resolve(), runtime / "imu.txt")
        (runtime / "startup.sh").write_bytes((HERE / "startup.sh").read_bytes())
        run_id = secrets.randbits(63) + 1
        environment = {k:v for k,v in os.environ.items() if not k.startswith(("PX4_", "SIM_", "GZ_", "P2B_", "LD_")) and k not in ("replay", "replay_mode")}
        environment["P2B_RUN_ID"] = str(run_id)
        command = [str(build / "bin/px4"), "-i", str(secrets.randbelow(50000) + 10000), "-w", str(runtime), "-s", str(runtime / "startup.sh")]
        run = {"run_id": str(run_id), "command": command, "forced_shutdown": False}
        run["clock_context"] = {"boot_id": Path("/proc/sys/kernel/random/boot_id").read_text().strip(),
                                 "parent_time_namespace": os.readlink("/proc/self/ns/time"),
                                 "provider": "CLOCK_MONOTONIC", "interposition_environment_removed": True,
                                 "before_launch_ns": time.clock_gettime_ns(time.CLOCK_MONOTONIC)}
        manifest["runs"].append(run)
        with (runtime / "stdout.log").open("w") as log:
            child = subprocess.Popen(command, cwd=runtime, env=environment, stdin=subprocess.PIPE,
                                     stdout=log, stderr=subprocess.STDOUT, text=True, start_new_session=True)
            try:
                run["clock_context"]["child_time_namespace"] = os.readlink(f"/proc/{child.pid}/ns/time")
                child.communicate("shutdown\n", timeout=25)
            except BaseException:
                run["forced_shutdown"] = True
                if child.poll() is None:
                    os.killpg(child.pid, signal.SIGTERM)
                    try: child.communicate(timeout=5)
                    except subprocess.TimeoutExpired:
                        os.killpg(child.pid, signal.SIGKILL); child.communicate(timeout=5)
                raise
            run["exit_code"] = child.returncode
            run["clock_context"]["after_exit_ns"] = time.clock_gettime_ns(time.CLOCK_MONOTONIC)
        run["stdout_sha256"] = base.digest(runtime / "stdout.log")
        require(run["exit_code"] == 0 and "P2B_CAPTURE_COMPLETE" in (runtime / "stdout.log").read_text(), "native capture incomplete")
        require("ERROR [" not in (runtime / "stdout.log").read_text(), "native capture reported errors")
        run["observations_sha256"] = base.digest(runtime / "observations.json")
        require(base.source_hashes() == json.loads((area / "source_hashes.json").read_text()), "source changed during build/run")
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
