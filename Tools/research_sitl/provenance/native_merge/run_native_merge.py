#!/usr/bin/env python3
"""Build a private typed native observation/merge profile; no admission path."""
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
spec = importlib.util.spec_from_file_location("p2b", HERE.parent / "native/run_capture.py")
p2b = importlib.util.module_from_spec(spec)
spec.loader.exec_module(p2b)
base = p2b.base
PREFIX = "Tools/research_sitl/provenance/"
PINS = dict(p2b.BASE_HASHES, **{
    PREFIX + "merge/ReceiptMerge.hpp": "4e519248101afe4af4b1c8de470f008b0592ad58ca5f090545db3f6444c87e11",
    PREFIX + "merge/ReceiptMerge.cpp": "d47051f5fba63acdcaea5428c65e4d24868b09e0f21e78d73d6a423ab39e4318",
    PREFIX + "native/px4_external/src/research_provenance/research_provenance.cpp": "e347806f1a01c58ad30fc6bb670807b1de31153266b1d132b2f361cfb65aa82f",
})
require = p2b.require


def replace_once(text, before, after):
    require(text.count(before) == 1, "private seam drift: " + before[:70])
    return text.replace(before, after)


def view(area):
    require(base.digest(HERE.parent / "native/run_capture.py") ==
            "42233d4b41d49ba128b5b63a55809e3b8459fdc1564986fa3e46cf18b8681de5", "P2B helper drift")
    for rel, digest in PINS.items():
        require(base.digest(ROOT / rel) == digest, "unreviewed source: " + rel)
    source = p2b.view(area)
    for rel in PINS:
        if not rel.startswith(PREFIX):
            continue
        # Detach only owned source-view symlinks; original paths are untouched.
        relative = Path(rel)
        for prefix in reversed(relative.parents):
            if str(prefix) == ".": continue
            target = source / prefix
            if target.is_symlink():
                require(target.resolve() == (ROOT / prefix).resolve(), "private ancestor escaped")
                target.unlink(); target.mkdir()
                for child in (ROOT / prefix).iterdir():
                    (target / child.name).symlink_to(child, target_is_directory=child.is_dir())
        target = source / rel
        require(target.is_symlink() and target.resolve() == (ROOT / rel).resolve(), "private source escaped")
        target.unlink(); target.write_bytes((ROOT / rel).read_bytes())
    header = source / (PREFIX + "merge/ReceiptMerge.hpp")
    original = header.read_text()
    begin = original.index("#if !defined(RESEARCH_CLOSED_MERGE_HOST)")
    end = original.index("#include <array>")
    guard = '''#if !defined(RESEARCH_P2D_NATIVE) || !defined(__PX4_POSIX) || !defined(__linux__) \\
    || defined(__PX4_NUTTX) || defined(ENABLE_LOCKSTEP_SCHEDULER) || defined(CONFIG_MUORB_APPS_SYNC_TIMESTAMP) \\
    || defined(RESEARCH_C1E_NEGATIVE_OWNER_TARGET) || defined(RESEARCH_SITL_DIAGNOSTIC_NATIVE)
#error "Private P2D native observation merge only"
#endif

'''
    header.write_text(original[:begin] + guard + original[end:])
    cpp = source / "src/modules/ekf2/EKF2.cpp"
    value = '#include "NativeMerge.hpp"\n' + cpp.read_text()
    value = replace_once(value, "{\n\tinitFusionControl();\n\tAdvertiseTopics();",
                         "{\n\tresearch_merge_source_start(this, _replay_mode, _multi_mode);\n\tinitFusionControl();\n\tAdvertiseTopics();")
    value = replace_once(value, "EKF2::~EKF2()\n{", "EKF2::~EKF2()\n{\n\tresearch_merge_source_end(this);")
    seam = "\t\tPublishAttitude(now); // publish attitude immediately (uses quaternion from output predictor)"
    call = '''\t\tresearch_merge_capture(this, EstimatorObservation{now, _replay_mode, _multi_mode,
            _ekf.attitude_valid(), _ekf.isLocalHorizontalPositionValid(), _ekf.isLocalVerticalPositionValid(),
            _ekf.isLocalVerticalVelocityValid(), _ekf.control_status().value, _ekf.fault_status().value});

'''
    cpp.write_text(replace_once(value, seam, call + seam))
    cmake = source / "src/modules/ekf2/CMakeLists.txt"
    cmake.write_text(cmake.read_text() + '''
target_compile_definitions(modules__ekf2 PRIVATE RESEARCH_P2D_NATIVE=1)
target_include_directories(modules__ekf2 PRIVATE "${PX4_SOURCE_DIR}/Tools/research_sitl/provenance/native_merge")
''')
    feeder = source / (PREFIX + "native/px4_external/src/research_provenance/research_provenance.cpp")
    value = '#include "NativeMerge.hpp"\n' + feeder.read_text()
    value = replace_once(value, "\tactive.store(true);",
                         "\tif (!research_merge_begin(run_id)) { fail(Fault::Input); return -1; }\n\tactive.store(true);")
    value = replace_once(value, "\t\tpx4_usleep(FixtureStepUs);",
                         "\t\tif (!research_merge_pump()) { fail(Fault::Input); return -1; }\n\t\tpx4_usleep(FixtureStepUs);")
    value = replace_once(value, "\treturn 0;\n}\n\nint export_records",
                         "\treturn research_merge_prepare_stop() ? 0 : -1;\n}\n\nint export_records")
    value = replace_once(value, "\texported = true;",
                         '\texported = true;\n\tif (!research_merge_finish("merged.json")) { fail(Fault::Sink); return -1; }')
    feeder.write_text(value)
    require(base.digest(source / (PREFIX + "merge/ReceiptMerge.cpp")) == PINS[PREFIX + "merge/ReceiptMerge.cpp"],
            "merge algorithm changed")
    return source


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ulog", type=Path, required=True)
    args = parser.parse_args()
    area = Path(tempfile.mkdtemp(prefix="p2d-native-", dir="/var/tmp"))
    print(area, flush=True)
    before = base.source_hashes()
    base.save(area / "source_hashes.json", before)
    source = view(area)
    build = area / "build"
    archive = area / "inputs"; archive.mkdir()
    prefixes = tuple(PREFIX + part + "/" for part in ("native", "merge", "native_merge"))
    for rel in before:
        if rel.startswith(prefixes):
            destination = archive / rel; destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / rel, destination)
    manifest = dict(schema="p2d-native-build/v1", status="incomplete", source=str(ROOT),
                    px4_head=base.git("rev-parse", "HEAD"), runs=[], source_hashes_sha256=base.digest(area / "source_hashes.json"),
                    overlay={rel: dict(original=digest, derived=base.digest(source / rel)) for rel, digest in PINS.items()})
    env = dict(os.environ, CCACHE_DISABLE="1", CCACHE_DIR=str(area / "ccache"), PYTHONDONTWRITEBYTECODE="1",
               GIT_CONFIG_COUNT="1", GIT_CONFIG_KEY_0="core.autocrlf", GIT_CONFIG_VALUE_0="true",
               GIT_SUBMODULES_ARE_EVIL="1", GIT_TERMINAL_PROMPT="0", GIT_ALLOW_PROTOCOL="file")
    commands = [
        ("configure", ["cmake", "-S", str(source), "-B", str(build), "-G", "Ninja", "-DCONFIG=px4_sitl_p2d-default",
                       "-DVENDOR=px4", "-DMODEL=sitl", "-DLABEL=p2d-default", f"-DPX4_BOARD_DIR={source}/boards/px4/sitl",
                       f"-DPX4_CONFIG_FILE={source}/{PREFIX}native/p2b-default.px4board",
                       f"-DEXTERNAL_MODULES_LOCATION={source}/{PREFIX}native_merge/px4_external",
                       "-DCMAKE_BUILD_TYPE=Debug", "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"]),
        ("build", ["cmake", "--build", str(build), "--target", "px4", "-j", "4"])]
    try:
        for name, command in commands:
            print(name, flush=True)
            with (area / (name + ".log")).open("w") as log:
                code = base.bounded_command(command, source, env, log)
            manifest[name] = dict(command=command, exit_code=code, log_sha256=base.digest(area / (name + ".log")))
            require(code == 0, name + " failed")
            manifest["configuration_audit"] = p2b.audit(build)
            base.save(area / "manifest.json", manifest)
        manifest["executable_sha256"] = base.digest(build / "bin/px4")
        for case in ("normal", "restart"):
            runtime = area / ("run-" + case); runtime.mkdir()
            stimulus = p2b.fixture(args.ulog.resolve(), runtime / "imu.txt")
            shutil.copy2(HERE / (case + ".sh"), runtime / "startup.sh")
            run_id = str(secrets.randbits(63) + 1)
            environment = {k:v for k,v in os.environ.items() if not k.startswith(("PX4_", "SIM_", "GZ_", "P2B_", "LD_"))
                           and k not in ("replay", "replay_mode")}
            environment["P2B_RUN_ID"] = run_id
            command = [str(build / "bin/px4"), "-i", str(secrets.randbelow(50000) + 10000),
                       "-w", str(runtime), "-s", str(runtime / "startup.sh")]
            run = dict(case=case, run_id=run_id, command=command, fixture=stimulus, forced_shutdown=False)
            manifest["runs"].append(run)
            with (runtime / "stdout.log").open("w") as log:
                child = subprocess.Popen(command, cwd=runtime, env=environment, stdin=subprocess.PIPE,
                                         stdout=log, stderr=subprocess.STDOUT, text=True, start_new_session=True)
                try:
                    child.communicate("shutdown\n", timeout=30)
                except BaseException:
                    run["forced_shutdown"] = True
                    if child.poll() is None:
                        os.killpg(child.pid, signal.SIGTERM)
                        try: child.communicate(timeout=5)
                        except subprocess.TimeoutExpired:
                            os.killpg(child.pid, signal.SIGKILL); child.communicate(timeout=5)
                    raise
                run["exit_code"] = child.returncode
            log_text = (runtime / "stdout.log").read_text()
            marker = "P2D_CAPTURE_COMPLETE" if case == "normal" else "P2D_EXPECTED_RESTART_REJECTION"
            require(run["exit_code"] == 0 and marker in log_text and "ERROR [" not in log_text, "native case incomplete")
            run["artifacts"] = {name: base.digest(runtime / name) for name in
                                ("startup.sh", "imu.txt", "stdout.log", "merged.json")}
            if case == "normal": run["artifacts"]["observations.json"] = base.digest(runtime / "observations.json")
        require(base.source_hashes() == before, "source changed during build/run")
        manifest["status"] = "captured_unqualified"
    except BaseException as error:
        manifest["status"] = "failed"; manifest["error"] = str(error)
        raise
    finally:
        base.save(area / "manifest.json", manifest)


if __name__ == "__main__":
    main()
