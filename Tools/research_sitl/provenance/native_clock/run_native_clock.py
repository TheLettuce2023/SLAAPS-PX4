#!/usr/bin/env python3
"""Build a private checked receipt-clock profile; preserve legacy scheduler hrt."""
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
spec = importlib.util.spec_from_file_location("p2d", HERE.parent / "native_merge/run_native_merge.py")
p2d = importlib.util.module_from_spec(spec)
spec.loader.exec_module(p2d)
p2b, base, require = p2d.p2b, p2d.base, p2d.require
PREFIX = "Tools/research_sitl/provenance/"
PINS = dict(p2d.PINS, **{
    PREFIX + "native_merge/NativeMerge.cpp": "0cb8a3c1c00cd4f8c63a0e78891a8be9eac1b74e5fc566dd80c05c0697f489f0",
    PREFIX + "provider/CheckedProvider.hpp": "92d1450328fe075ad9c1b49a09f00fb817d6df0344242924350d7effe7441a49",
    PREFIX + "provider/CheckedProvider.cpp": "e5d3b4c2d7a17feeaffffc3d459dcb0d9dc2c9f0b9dd0ed203dd54d11c4ff672",
})
replace_once = p2d.replace_once


def detach(source, rel):
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
    return target


def view(area):
    require(base.digest(HERE.parent / "native_merge/run_native_merge.py") ==
            "331f05bd0919b7922bb2239379ef32484541efe451d080b89b7651169387b37d", "P2D helper drift")
    for rel, digest in PINS.items(): require(base.digest(ROOT / rel) == digest, "unreviewed source: " + rel)
    source = p2d.view(area)
    for rel in set(PINS) - set(p2d.PINS): detach(source, rel)
    header = source / (PREFIX + "provider/CheckedProvider.hpp")
    value = header.read_text(); begin = value.index("#if !defined(RESEARCH_CLOSED_CLOCK_HOST)"); end = value.index("#include <array>")
    header.write_text(value[:begin] + '#include "NativeClock.hpp"\n\n' + value[end:])
    bridge = source / (PREFIX + "native_merge/NativeMerge.cpp")
    value = '#include "NativeClock.hpp"\n' + bridge.read_text()
    value = replace_once(value, "std::atomic<bool> recording{false};",
                         "std::atomic<bool> recording{false}, lifecycle_recording{false};")
    value = replace_once(value, "Hook() { in_flight.fetch_add(1); active = recording.load(); }",
                         "explicit Hook(bool lifetime = false) { in_flight.fetch_add(1); active = lifetime ? lifecycle_recording.load() : recording.load(); }")
    value = replace_once(value, "\trecording.store(true);", "\tlifecycle_recording.store(true);\n\trecording.store(true);")
    for signature in ("void research_merge_source_start(const void *instance, bool replay, bool multi) noexcept",
                      "void research_merge_source_end(const void *instance) noexcept"):
        value = replace_once(value, signature + "\n{\n\tHook hook;", signature + "\n{\n\tHook hook{true};")
    value = replace_once(value, "\tfinished = true;\n\trecording.store(false);",
                         "\tfinished = true;\n\tlifecycle_recording.store(false);\n\trecording.store(false);")
    require(value.count("hrt_absolute_time()") == 7, "receipt read inventory drift")
    for purpose in ("OwnerBefore", "OwnerAfter", "SourceStart", "SourceEnd", "SourceObservation", "StopIntent", "OwnerClose"):
        value = value.replace("hrt_absolute_time()", "research_clock_read(ClockPurpose::" + purpose + ")", 1)
    value = replace_once(value, "\tif (first_fault.load()) { return false; }",
                         "\tif (research_clock_faulted()) { fail(BridgeFault::SourceClock); }\n\tif (first_fault.load()) { return false; }")
    bridge.write_text(value)
    feeder = source / (PREFIX + "native/px4_external/src/research_provenance/research_provenance.cpp")
    value = '#include "NativeClock.hpp"\n' + feeder.read_text()
    value = replace_once(value, "\tif (!research_merge_begin(run_id))",
                         "\tif (!research_clock_begin(run_id)) { fail(Fault::Input); return -1; }\n\tif (!research_merge_begin(run_id))")
    value = replace_once(value, "if (!research_merge_pump()) { fail(Fault::Input); return -1; }",
                         "if (!research_merge_pump()) { research_merge_prepare_stop(); fail(Fault::Input); return -1; }")
    value = replace_once(value, 'if (!research_merge_finish("merged.json")) { fail(Fault::Sink); return -1; }',
                         'const bool merge_ok = research_merge_finish("merged.json");\n\tconst bool clock_ok = research_clock_finish("clock.json");\n\tif (!merge_ok || !clock_ok) { fail(Fault::Sink); return -1; }')
    feeder.write_text(value)
    cmake = source / "src/modules/ekf2/CMakeLists.txt"
    cmake.write_text(cmake.read_text() + "\ntarget_compile_definitions(modules__ekf2 PRIVATE RESEARCH_P3C_NATIVE=1)\n")
    require(base.digest(source / (PREFIX + "provider/CheckedProvider.cpp")) == PINS[PREFIX + "provider/CheckedProvider.cpp"], "checked conversion changed")
    return source


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ulog", type=Path, required=True)
    args = parser.parse_args()
    area = Path(tempfile.mkdtemp(prefix="p3c-native-", dir="/var/tmp"))
    print(area, flush=True)
    before = base.source_hashes()
    base.save(area / "source_hashes.json", before)
    source = view(area)
    build = area / "build"
    archive = area / "inputs"; archive.mkdir()
    prefixes = tuple(PREFIX + part + "/" for part in ("native", "merge", "native_merge", "provider", "native_clock"))
    for rel in before:
        if rel.startswith(prefixes):
            destination = archive / rel; destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / rel, destination)
    manifest = dict(schema="p3c-native-build/v1", status="incomplete", source=str(ROOT),
                    px4_head=base.git("rev-parse", "HEAD"), runs=[], source_hashes_sha256=base.digest(area / "source_hashes.json"),
                    overlay={rel: dict(original=digest, derived=base.digest(source / rel)) for rel, digest in PINS.items()})
    env = dict(os.environ, CCACHE_DISABLE="1", CCACHE_DIR=str(area / "ccache"), PYTHONDONTWRITEBYTECODE="1",
               GIT_CONFIG_COUNT="1", GIT_CONFIG_KEY_0="core.autocrlf", GIT_CONFIG_VALUE_0="true",
               GIT_SUBMODULES_ARE_EVIL="1", GIT_TERMINAL_PROMPT="0", GIT_ALLOW_PROTOCOL="file")
    commands = [
        ("configure", ["cmake", "-S", str(source), "-B", str(build), "-G", "Ninja", "-DCONFIG=px4_sitl_p3c-default",
                       "-DVENDOR=px4", "-DMODEL=sitl", "-DLABEL=p3c-default", f"-DPX4_BOARD_DIR={source}/boards/px4/sitl",
                       f"-DPX4_CONFIG_FILE={source}/{PREFIX}native/p2b-default.px4board",
                       f"-DEXTERNAL_MODULES_LOCATION={source}/{PREFIX}native_clock/px4_external",
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
        for case in ("normal", "read_error"):
            runtime = area / ("run-" + case); runtime.mkdir()
            stimulus = p2b.fixture(args.ulog.resolve(), runtime / "imu.txt")
            shutil.copy2(HERE / (case + ".sh"), runtime / "startup.sh")
            run_id = str(secrets.randbits(63) + 1)
            environment = {k:v for k,v in os.environ.items() if not k.startswith(("PX4_", "SIM_", "GZ_", "P2B_", "P3C_", "LD_"))
                           and k not in ("replay", "replay_mode")}
            environment["P2B_RUN_ID"] = run_id
            environment["P3C_CASE"] = case
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
            marker = "P3C_CAPTURE_COMPLETE" if case == "normal" else "P3C_EXPECTED_READ_REJECTION"
            require(run["exit_code"] == 0 and marker in log_text and "ERROR [" not in log_text, "native case incomplete")
            run["artifacts"] = {name: base.digest(runtime / name) for name in
                                ("startup.sh", "imu.txt", "stdout.log", "merged.json", "clock.json")}
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
