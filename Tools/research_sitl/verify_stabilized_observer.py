#!/usr/bin/env python3
"""Isolated working-baseline observer builds/tests. No hardware communication."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[2]
PARENT = ROOT / "build/bench_commissioning/bench-e8ea98b404-step3"
NATIVE_PARENT = Path("/var/tmp/research-bench-step3-e8ea/px4-source")
BASE = "e8ea98b404baf50623a519ac67ec1c03aa240859"


def digest(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def record(path, value):
    path.write_text(json.dumps(value, indent=2) + "\n")


def run(command, log, **kwargs):
    # Preserve failed attempts when retrying the same build after a diagnosed fix.
    destination = log
    counter = 0
    while destination.exists():
        counter += 1
        destination = log.with_name(log.name + "." + str(counter))
    with destination.open("w") as stream:
        result = subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT, **kwargs)
    if result.returncode:
        print(destination.read_text()[-5000:], flush=True)
        raise RuntimeError(f"Exit {result.returncode}: {destination}")


def prepare(out):
    out.mkdir(exist_ok=False)
    source = out / "px4-source"
    parent = json.loads((PARENT / "candidate-receipt.json").read_text())
    assert parent["source_base"] == BASE
    assert digest(PARENT / "stock-source.tar") == parent["source_archive_sha256"]
    assert digest(PARENT / "port-overlay.tar") == parent["overlay_sha256"]
    run(["git", "clone", "--shared", "--no-checkout", str(NATIVE_PARENT), str(source)], out / "clone.log")
    subprocess.run(["git", "-C", str(source), "update-ref", "--no-deref", "HEAD", BASE], check=True)
    subprocess.run(["git", "-C", str(source), "read-tree", BASE], check=True)
    for archive_name in ("stock-source.tar", "port-overlay.tar"):
        with tarfile.open(PARENT / archive_name) as archive:
            archive.extractall(source, filter="tar")
    modules = json.loads((PARENT / "required-submodules-recursive.json").read_text())
    for entry in modules:
        if "parent" in entry:
            continue  # Already included by the parent's recursive source copy.
        src = NATIVE_PARENT / entry["path"]
        dst = source / entry["path"]
        shutil.copytree(src, dst, symlinks=True, dirs_exist_ok=True)
    for entry in modules:
        actual = subprocess.check_output(["git", "-C", str(source / entry["path"]), "rev-parse", "HEAD"], text=True).strip()
        assert actual == entry["revision"], entry["path"]
    # New implementation is overlaid on the qualified older baseline; the
    # previously flashed image, parent source and evidence are never changed.
    overlays = []
    for folder in ("src/lib/research_stabilized_manual", "src/modules/research_stabilized_manual"):
        for item in (ROOT / folder).iterdir():
            if item.is_file():
                target = source / folder / item.name
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(item, target)
                overlays.append(str(item.relative_to(ROOT)))
    message = "msg/StabilizedObserverStatus.msg"
    shutil.copy2(ROOT / message, source / message)
    overlays.append(message)
    library = source / "src/lib/CMakeLists.txt"
    library.write_text(library.read_text() + "\nadd_subdirectory(research_stabilized_manual EXCLUDE_FROM_ALL)\n")
    messages = source / "msg/CMakeLists.txt"
    text = messages.read_text()
    assert text.count("\tBenchServoCommand.msg\n") == 1
    messages.write_text(text.replace("\tBenchServoCommand.msg\n", "\tBenchServoCommand.msg\n\tStabilizedObserverStatus.msg\n"))
    board = source / "boards/px4/fmu-v6x/default.px4board"
    board.write_text(board.read_text().replace("CONFIG_MODULES_RESEARCH_BENCH_CONTROL=y", "CONFIG_MODULES_RESEARCH_BENCH_CONTROL=n")
                     + "\nCONFIG_MODULES_RESEARCH_STABILIZED_MANUAL=y\n")
    # An ARM overlay includes the full inherited bench backport but disables
    # its module; the observer's CMake also rejects any combined configuration.
    inherited = json.loads((PARENT / "port-source-manifest.json").read_text())
    names = sorted(set(inherited["files"]) | set(overlays) | {"src/lib/CMakeLists.txt", "msg/CMakeLists.txt"})
    record(out / "arm-overlay-inputs.json", {name: digest(source / name) for name in names})
    with tarfile.open(out / "observer-overlay.tar", "w") as archive:
        for name in names:
            archive.add(source / name, arcname=name)
    fixture = source / "src/examples/stabilized_observer_test"
    shutil.copytree(ROOT / "Tools/research_sitl/stabilized_observer", fixture)
    sitl = source / "boards/px4/sitl/default.px4board"
    text = sitl.read_text()
    for name in ("GZ_MSGS", "GZ_BRIDGE", "GZ_PLUGINS"):
        text = text.replace("CONFIG_MODULES_SIMULATION_" + name + "=y", "CONFIG_MODULES_SIMULATION_" + name + "=n")
    sitl.write_text(text + "\nCONFIG_MODULES_RESEARCH_STABILIZED_MANUAL=y\nCONFIG_BOARD_NOLOCKSTEP=y\nCONFIG_EXAMPLES_STABILIZED_OBSERVER_TEST=y\n")
    record(out / "preparation.json", {"base": BASE, "source": str(source), "parent_receipt_sha256": digest(PARENT / "candidate-receipt.json"),
           "overlay_sha256": digest(out / "observer-overlay.tar"), "donor_files": {name: digest(ROOT / name) for name in overlays},
           "physical_authority": False, "output_publication": False})
    print(out, flush=True)


def host(out):
    source = out / "px4-source/src/lib/research_stabilized_manual"
    for label, kind, flags in (("debug", "Debug", ""), ("release", "Release", ""),
                               ("sanitize", "Debug", "-fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie")):
        build = out / label
        run(["cmake", "-S", str(source), "-B", str(build), "-G", "Ninja", "-DCMAKE_BUILD_TYPE=" + kind,
             "-DCMAKE_CXX_FLAGS=" + flags, "-DCMAKE_EXE_LINKER_FLAGS=" + ("-no-pie" if flags else "")], out / (label + "-configure.log"))
        run(["cmake", "--build", str(build), "-j", "4"], out / (label + "-build.log"))
        run(["ctest", "--test-dir", str(build), "-V", "--output-on-failure"], out / (label + "-test.log"),
            env=dict(os.environ, ASAN_OPTIONS="halt_on_error=1:detect_leaks=1", UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1"))
        print(label + " PASS", flush=True)


def refresh(out):
    """One diagnosed portability correction; preserve the first build inputs."""
    source = out / "px4-source"
    manifest = json.loads((out / "arm-overlay-inputs.json").read_text())
    for name, expected in manifest.items():
        assert digest(source / name) == expected, name
    for name in ("arm-overlay-inputs.json", "observer-overlay.tar", "preparation.json"):
        backup = out / ("initial-" + name)
        assert not backup.exists()
        shutil.copy2(out / name, backup)
    preparation = json.loads((out / "preparation.json").read_text())
    for name in ("src/lib/research_stabilized_manual/CanardAllocator.cpp", "src/lib/research_stabilized_manual/StabilizedManualControl.cpp"):
        shutil.copy2(ROOT / name, source / name)
        manifest[name] = digest(source / name)
        preparation["donor_files"][name] = manifest[name]
    record(out / "arm-overlay-inputs.json", manifest)
    with tarfile.open(out / "observer-overlay.tar", "w") as archive:
        for name in sorted(manifest):
            archive.add(source / name, arcname=name)
    preparation["overlay_sha256"] = digest(out / "observer-overlay.tar")
    preparation["portability_fix"] = "C math min/max with explicit precision; no controller or allocator algorithm change"
    record(out / "preparation.json", preparation)


def native(out):
    run(["cmake", "-S", str(out / "px4-source"), "-B", str(out / "native"), "-G", "Ninja",
         "-DCONFIG=px4_sitl_default", "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"], out / "native-configure.log")
    run(["cmake", "--build", str(out / "native"), "-j", "8"], out / "native-build.log",
        env=dict(os.environ, GIT_SUBMODULES_ARE_EVIL="1"))
    print("native build PASS", flush=True)


def test(out):
    work = Path(tempfile.mkdtemp(prefix="observation-", dir=out))
    profile = work / "synthetic-observer.profile"
    subprocess.run([str(out / "debug/stabilized_observer_profile_test"), str(profile)], check=True)
    init = work / "rc.test"
    init.write_text("#!/bin/sh\nset -e\n. px4-alias.sh\nparam select parameters.bson\nstabilized_observer_test "
                    + str(profile) + " || true\nshutdown\n")
    log = work / "console.log"
    run([str(out / "native/bin/px4"), "-d", "-s", str(init)], log, cwd=work, timeout=90)
    import re
    result = re.search(r"OBSERVER_RESULT checks=(\d+) failures=(\d+)", log.read_text())
    assert result and int(result[1]) > 100 and int(result[2]) == 0, log
    record(work / "result.json", {"checks": int(result[1]), "failures": int(result[2]), "px4_sha256": digest(out / "native/bin/px4"),
           "profile_sha256": digest(profile), "console_sha256": digest(log), "hardware": False, "physics": False})
    print("native observation PASS " + str(work), flush=True)


def seal(out):
    if (out / "observer-verification-manifest.json").exists():
        raise RuntimeError("Refusing to overwrite sealed evidence")
    preparation = json.loads((out / "preparation.json").read_text())
    source = out / "px4-source"
    for name, expected in preparation["donor_files"].items():
        assert digest(ROOT / name) == digest(source / name) == expected, name
    overlay = json.loads((out / "arm-overlay-inputs.json").read_text())
    for name, expected in overlay.items():
        assert digest(source / name) == expected, name
    for name in ("ObserverIntegrationTest.cpp", "CMakeLists.txt", "Kconfig"):
        assert digest(ROOT / "Tools/research_sitl/stabilized_observer" / name) == digest(source / "src/examples/stabilized_observer_test" / name), name
    native_hash = digest(out / "native/bin/px4")
    accepted = []
    for result_file in out.glob("observation-*/result.json"):
        result = json.loads(result_file.read_text())
        if result["px4_sha256"] == native_hash and result["failures"] == 0:
            assert digest(result_file.parent / "console.log") == result["console_sha256"]
            accepted.append(str(result_file.relative_to(out)))
    assert accepted, "No passing integration test on the current executable"
    # Execute the actual module CMake guard, rather than merely search its text.
    conflict = subprocess.run(["cmake", "-DCONFIG_MODULES_RESEARCH_BENCH_CONTROL=ON", "-P",
                               str(source / "src/modules/research_stabilized_manual/CMakeLists.txt")], capture_output=True, text=True)
    assert conflict.returncode != 0 and "require separate images" in conflict.stderr
    (out / "exclusive-build-test.log").write_text(conflict.stdout + conflict.stderr)
    symbols = subprocess.check_output(["nm", "-C", str(out / "native/bin/px4")], text=True)
    assert "research_stabilized_manual_main" in symbols and "research_bench_control_main" not in symbols
    host_results = {}
    for label in ("debug", "release", "sanitize"):
        latest = max(out.glob(label + "-test.log*"), key=lambda p: p.stat().st_mtime_ns)
        text = latest.read_text()
        assert "100% tests passed" in text and "193 checks, 0 failures" in text
        assert "123083" in text
        host_results[label] = str(latest.relative_to(out))
    inputs = dict(overlay)
    for name in ("ObserverIntegrationTest.cpp", "CMakeLists.txt", "Kconfig"):
        inputs["src/examples/stabilized_observer_test/" + name] = digest(source / "src/examples/stabilized_observer_test" / name)
    for name in ("src/lib/rate_control/rate_control.cpp", "src/lib/rate_control/rate_control.hpp", "boards/px4/sitl/default.px4board"):
        inputs[name] = digest(source / name)
    snapshot = out / "verified-source"
    for name, expected in inputs.items():
        target = snapshot / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source / name, target)
        assert digest(target) == expected
    record(out / "verified-source-inputs.json", inputs)
    shutil.copy2(Path(__file__), out / "verify_stabilized_observer.py")
    files = list(out.glob("*.json")) + list(out.glob("*.log*")) + list(out.glob("*.tar"))
    files += [out / "verify_stabilized_observer.py", out / "native/bin/px4", out / "native/compile_commands.json",
              out / "native/CMakeCache.txt", out / "native/px4_boardconfig.h"]
    files += [p for p in snapshot.rglob("*") if p.is_file()]
    for folder in out.glob("observation-*"):
        files += [p for p in folder.iterdir() if p.is_file()]
    for label in host_results:
        files += [out / label / "stabilized_manual_test", out / label / "stabilized_observer_profile_test"]
    manifest = {"schema_version": 1, "scope": "working-baseline output-disabled observer software verification",
                "base": BASE, "host_results": host_results, "accepted_native_results": accepted,
                "native_px4_sha256": native_hash, "hardware_tested": False, "physical_plant_validated": False,
                "actuator_publication": False, "bench_module_excluded": True,
                "files": {str(p.relative_to(out)): digest(p) for p in sorted(set(files))}}
    record(out / "observer-verification-manifest.json", manifest)
    print("Sealed " + digest(out / "observer-verification-manifest.json"), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("prepare", "refresh", "host", "native", "test", "seal"))
    parser.add_argument("--evidence", type=Path, required=True)
    args = parser.parse_args()
    out = args.evidence.resolve()
    if out.parent != Path("/var/tmp") or not out.name.startswith("research-stabilized-observer-"):
        raise RuntimeError("Private /var/tmp observer evidence directory required")
    globals()[args.action](out)


if __name__ == "__main__":
    main()
