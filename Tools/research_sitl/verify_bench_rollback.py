#!/usr/bin/env python3
"""Qualify the bench on the working PX4/NuttX baseline; never flash hardware.

Run under the installed Ubuntu WSL. ARM firmware is built separately in the
repository's px4-dev Docker image using the generated, hash-bound source overlay.
The active checkout, original H10 evidence and stock reference remain intact.
"""
import argparse
import difflib
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile

import verify_bench_h9 as integration

ROOT = Path(__file__).resolve().parents[2]
BASE = "e8ea98b404baf50623a519ac67ec1c03aa240859"
DONOR_BASE = "e880f1cd79e2453ef3dfaf68ee1c067b98ea9a53"
REFERENCE = ROOT / "build/bench_commissioning/stock-e8ea98b404-nuttx11-reference"
PRODUCTION = [
    "src/lib/research_actuator_command", "src/modules/research_bench_control",
    "src/lib/mixer_module", "src/lib/CMakeLists.txt", "msg/BenchServoCommand.msg",
    "msg/CMakeLists.txt", "src/modules/manual_control/ManualControl.cpp",
    "src/modules/manual_control/ManualControl.hpp",
    "msg/versioned/ManualControlSetpoint.msg", "msg/px4_msgs_old/msg/ManualControlSetpointV0.msg",
    "src/modules/mavlink/mavlink_receiver.cpp",
]
SCENARIOS = integration.SCENARIOS + ("selector_snapshot", "future", "center_bounds")


def digest(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def write_json(path, value):
    Path(path).write_text(json.dumps(value, indent=2) + "\n")


def git(*args, cwd=ROOT):
    # Windows Git avoids a very slow full-index NTFS refresh by WSL Git when
    # collecting the current working-tree donor diff. Linux private trees still
    # use native Git and their own worktree metadata.
    windows_git = Path("/mnt/c/Program Files/Git/cmd/git.exe")
    if cwd == ROOT and windows_git.is_file():
        windows_root = subprocess.check_output(["wslpath", "-w", str(ROOT)], text=True).strip()
        return subprocess.check_output([str(windows_git), "-C", windows_root, *args])
    return subprocess.check_output(["git", "-C", str(cwd), *args])


def run(command, log, **kwargs):
    if log.exists():
        raise RuntimeError("Refusing to overwrite evidence: " + str(log))
    with log.open("w") as stream:
        subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT, check=True, **kwargs)


def upstream_dependencies(source):
    for name in ("msg/versioned/ManualControlSetpoint.msg", "msg/px4_msgs_old/msg/ManualControlSetpointV0.msg"):
        destination = source / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes((ROOT / name).read_bytes())
    receiver = source / "src/modules/mavlink/mavlink_receiver.cpp"
    text = receiver.read_text()
    anchor = "\tmanual_control_setpoint.data_source = manual_control_setpoint_s::SOURCE_MAVLINK_0 + _mavlink.get_instance_id();\n"
    if text.count(anchor) != 1 or "manual_control_setpoint.source_system_id" in text:
        raise RuntimeError("Unexpected older manual-control publisher")
    receiver.write_text(text.replace(anchor, anchor + "\tmanual_control_setpoint.source_system_id = msg->sysid;\n"
                                    + "\tmanual_control_setpoint.source_component_id = msg->compid;\n"))


def prepare(out, stage, resume=False):
    if (out.exists() or stage.exists()) and not resume:
        raise RuntimeError("Preparation requires new evidence and staging directories")
    out.mkdir(parents=True, exist_ok=resume)
    stage.mkdir(parents=True, exist_ok=resume)
    receipt = json.loads((REFERENCE / "comparison-receipt.json").read_text())
    hardware = json.loads((REFERENCE / "hardware-startup-acceptance-20261019.json").read_text())
    if receipt["source_head"] != BASE or not hardware["overall_step2_checklist_passed"]:
        raise RuntimeError("Verified stock startup reference required")
    if digest(REFERENCE / receipt["artifacts"]["px4"]["path"]) != hardware["firmware_sha256"]:
        raise RuntimeError("Reference artifact binding failed")
    worktree = ROOT / ".agents/worktrees/bench-e8ea-step3"
    if worktree.exists() and not resume:
        raise RuntimeError("Refusing to reuse existing worktree")
    if not resume:
        run(["git", "-C", str(ROOT), "worktree", "add", "--detach", "--no-checkout",
             str(worktree), BASE], out / "worktree-create.log")
        subprocess.run(["git", "-C", str(worktree), "read-tree", BASE], check=True)
        shutil.copyfile(worktree / ".git", stage / "worktree.git")
        for name in ("stock-source.tar", "required-submodules-commits.json",
                     "required-submodules-recursive.json", "prepare-container.py", "prepare-nested.py",
                     "pin-nuttx-version-tags.py", "docker-image-inspect.json"):
            shutil.copyfile(REFERENCE / name, stage / name)
    if digest(stage / "stock-source.tar") != receipt["source_archive_sha256"]:
        raise RuntimeError("Base source archive mismatch")
    source = out / "px4-source"
    if resume:
        if (stage / "port-overlay.tar").exists() or (source / "src/lib/research_actuator_command").exists():
            raise RuntimeError("Resume is restricted to preparation before the donor patch was applied")
        if git("rev-parse", "HEAD", cwd=source).decode().strip() != BASE:
            raise RuntimeError("Wrong resume source")
    else:
        source.mkdir()
        with tarfile.open(stage / "stock-source.tar") as archive:
            archive.extractall(source, filter="tar")
        shutil.copyfile(stage / "worktree.git", source / ".git")
    # Preserve the H10 configured-range center semantics with the same two-line
    # upstream fix. Leave older MovingDiff/AlphaFilter and all other base code intact.
    mixer = source / "src/lib/mixer_module/mixer_module.cpp"
    text = mixer.read_text()
    old = "&& _center_value[i] >= 800\n\t    && _center_value[i] <= 2200"
    new = "&& _center_value[i] >= _min_value[i]\n\t    && _center_value[i] <= _max_value[i]"
    if text.count(old) != 1 and not (resume and text.count(new) == 1):
        raise RuntimeError("Unexpected base mixer center validation")
    mixer.write_text(text.replace(old, new))
    upstream_dependencies(source)
    patch = git("diff", "--binary", DONOR_BASE, "--", *PRODUCTION)
    (stage / "bench-port.patch").write_bytes(patch)
    subprocess.run(["git", "-C", str(source), "apply", "--check", str(stage / "bench-port.patch")], check=True)
    subprocess.run(["git", "-C", str(source), "apply", str(stage / "bench-port.patch")], check=True)
    names = git("ls-files", "-z", "--", *PRODUCTION).decode().split("\0")
    inputs = {name: digest(ROOT / name) for name in names if name}
    for name in inputs:
        if name not in ("msg/CMakeLists.txt", "src/modules/mavlink/mavlink_receiver.cpp") and digest(source / name) != inputs[name]:
            raise RuntimeError("Production port differs from reviewed donor: " + name)
    moving = "src/modules/manual_control/MovingDiff.hpp"
    if (source / moving).read_bytes() != git("show", BASE + ":" + moving):
        raise RuntimeError("Older filter implementation changed")
    board = "boards/px4/fmu-v6x/default.px4board"
    with (source / board).open("a") as stream:
        stream.write("\nCONFIG_MODULES_RESEARCH_BENCH_CONTROL=y\n")
    overlay_names = sorted(set(inputs) | {board})
    # Source content is bound independently of commit labels for this dirty build.
    records = {name: {"sha256": digest(source / name), "bytes": (source / name).stat().st_size}
               for name in overlay_names}
    write_json(stage / "port-source-manifest.json", {
        "base": BASE, "donor_head": git("rev-parse", "HEAD").decode().strip(),
        "donor_inputs": inputs, "files": records,
        "compatibility_backport": "configured-range mixer center validation; manual message v1 sender IDs and retained v0 definition; two MAVLink sender assignments",
        "unchanged_older_filter_sha256": digest(source / moving),
        "source_dirty": True, "automatic_startup_added": False,
    })
    with tarfile.open(stage / "port-overlay.tar", "w") as archive:
        for name in overlay_names:
            archive.add(source / name, arcname=name)
    # Source/dependency preparation uses committed objects, never the host's
    # generated NuttX files or any locally modified submodule working files.
    modules = json.loads((stage / "required-submodules-recursive.json").read_text())
    for module in modules:
        destination = source / module["path"]
        subprocess.run(["git", "clone", "--quiet", "--no-checkout", "--shared",
                        str(ROOT / module["path"]), str(destination)], check=True)
        subprocess.run(["git", "-C", str(destination), "checkout", "--quiet", "--detach", module["revision"]], check=True)
        if "parent" in module:
            subprocess.run(["git", "-C", str(source / module["parent"]), "submodule", "init", "--",
                            module["relative_path"]], check=True)
    # Retain the version metadata snapshot without touching host tags.
    kernel = source / "platforms/nuttx/NuttX/nuttx"
    for tag in git("tag", "--list", cwd=kernel).decode().splitlines():
        match = re.fullmatch(r"nuttx-(\d+)\.(\d+)\.(\d+).*", tag)
        if match and tuple(map(int, match.groups())) > (11, 0, 0):
            subprocess.run(["git", "-C", str(kernel), "update-ref", "-d", "refs/tags/" + tag], check=True)
    fixture = ROOT / "Tools/research_sitl/bench_active"
    shutil.copytree(fixture, source / "src/examples/bench_active_test")
    sitl = source / "boards/px4/sitl/default.px4board"
    # This executable captures real selector/owner/mixer behavior in memory and
    # never runs Gazebo. Exclude its unrelated plugins and their submodules.
    config = sitl.read_text()
    for module in ("GZ_MSGS", "GZ_BRIDGE", "GZ_PLUGINS"):
        config = config.replace("CONFIG_MODULES_SIMULATION_" + module + "=y",
                                "CONFIG_MODULES_SIMULATION_" + module + "=n")
    sitl.write_text(config)
    with sitl.open("a") as stream:
        stream.write("\nCONFIG_MODULES_RESEARCH_BENCH_CONTROL=y\nCONFIG_BOARD_NOLOCKSTEP=y\nCONFIG_EXAMPLES_BENCH_ACTIVE_TEST=y\n")
    write_json(out / "fixture-inputs.json", {str(p.relative_to(ROOT)): digest(p) for p in fixture.iterdir() if p.is_file()})
    shutil.copytree(Path("/var/tmp/research-bench-h10/googletest"), out / "googletest", symlinks=True)
    write_json(out / "preparation.json", {"base": BASE, "source": str(source), "stage": str(stage),
               "worktree_metadata": str(worktree), "archive_sha256": digest(stage / "stock-source.tar"),
               "overlay_sha256": digest(stage / "port-overlay.tar"),
               "reference_hardware_record_sha256": digest(REFERENCE / "hardware-startup-acceptance-20261019.json")})
    print("Isolated working-baseline port and hash-bound ARM overlay prepared", flush=True)


def host(out):
    library = out / "px4-source/src/lib/research_actuator_command"
    for label, kind, flags in (("debug", "Debug", ""), ("release", "Release", ""),
                               ("sanitize", "Debug", "-fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie")):
        build = out / label
        run(["cmake", "-S", str(library), "-B", str(build), "-G", "Ninja", "-DCMAKE_BUILD_TYPE=" + kind,
             "-DCMAKE_CXX_FLAGS=-Wall -Wextra -Werror -Wpedantic " + flags,
             "-DCMAKE_EXE_LINKER_FLAGS=" + ("-fsanitize=address,undefined -no-pie" if flags else "")], out / (label + "-configure.log"))
        run(["cmake", "--build", str(build), "-j", "6"], out / (label + "-build.log"))
        env = dict(os.environ, ASAN_OPTIONS="detect_leaks=1:abort_on_error=1", UBSAN_OPTIONS="halt_on_error=1")
        run(["ctest", "--test-dir", str(build), "-V", "--output-on-failure"], out / (label + "-tests.log"), env=env)
        print(label + " portable tests PASS", flush=True)
    run(["python3", "-B", "-m", "unittest", "discover", "-s", str(ROOT / "Tools/research_sitl"),
         "-p", "test_bench_acceptance.py", "-v"], out / "acceptance-checker-tests.log")


def update_dependencies(out, stage):
    """Retain the failed first build's source binding before adding sender IDs."""
    source = out / "px4-source"
    manifest = json.loads((stage / "port-source-manifest.json").read_text())
    for name, record in manifest["files"].items():
        if digest(source / name) != record["sha256"]:
            raise RuntimeError("Unexpected pre-update source: " + name)
    for name in ("port-source-manifest.json", "port-overlay.tar"):
        backup = stage / ("initial-" + name)
        if backup.exists():
            raise RuntimeError("Dependency update already recorded")
        shutil.copyfile(stage / name, backup)
    upstream_dependencies(source)
    for name in ("msg/versioned/ManualControlSetpoint.msg", "msg/px4_msgs_old/msg/ManualControlSetpointV0.msg",
                 "src/modules/mavlink/mavlink_receiver.cpp"):
        manifest["donor_inputs"][name] = digest(ROOT / name)
        manifest["files"][name] = {"sha256": digest(source / name), "bytes": (source / name).stat().st_size}
    manifest["compatibility_backport"] = "configured-range mixer center validation; manual message v1 sender IDs and retained v0 definition; two MAVLink sender assignments"
    write_json(stage / "port-source-manifest.json", manifest)
    with tarfile.open(stage / "port-overlay.tar", "w") as archive:
        for name in sorted(manifest["files"]):
            archive.add(source / name, arcname=name)
    sitl = source / "boards/px4/sitl/default.px4board"
    config = sitl.read_text()
    for module in ("GZ_MSGS", "GZ_BRIDGE", "GZ_PLUGINS"):
        config = config.replace("CONFIG_MODULES_SIMULATION_" + module + "=y",
                                "CONFIG_MODULES_SIMULATION_" + module + "=n")
    sitl.write_text(config)
    for name in ("sitl-configure.log", "sitl-full.log", "sitl-mavlink.log"):
        (out / name).rename(out / ("initial-" + name))
    print("Sender identity dependencies added; initial build logs/source binding retained", flush=True)


def sitl(out):
    source, build = out / "px4-source", out / "sitl-build"
    run(["cmake", "-S", str(source), "-B", str(build), "-G", "Ninja", "-DCONFIG=px4_sitl_default",
         "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"], out / "sitl-configure.log")
    run(["cmake", "--build", str(build), "--target", "mavlink_c_generate", "-j", "6"], out / "sitl-mavlink.log")
    run(["cmake", "--build", str(build), "-j", "6"], out / "sitl-full.log")
    print("Fresh native PX4 build PASS", flush=True)


def stock(out):
    # Reuse the proven H10 GTest-only scaffold against the new base source.
    import verify_bench_h10
    verify_bench_h10.OUT = out
    verify_bench_h10.integration.OUT = verify_bench_h10.integration.base.OUT = out
    verify_bench_h10.stock_tests()


def test(out, repetitions):
    integration.ROOT = integration.base.ROOT = out / "px4-source"
    integration.OUT = integration.base.OUT = out
    integration.SCENARIOS = SCENARIOS
    sys.argv = [sys.argv[0], "test"]
    for index in range(repetitions):
        print("Native capture suite repetition", index + 1, flush=True)
        integration.main()


def smoke(out):
    integration.base.OUT = out
    integration.base.ROOT = out / "px4-source"
    sys.argv = [sys.argv[0], "smoke"]
    integration.base.main()


def verify(out, stage):
    manifest = json.loads((stage / "port-source-manifest.json").read_text())
    source = out / "px4-source"
    for name, record in manifest["files"].items():
        if digest(source / name) != record["sha256"]:
            raise RuntimeError("Port source changed: " + name)
    for name, expected in manifest["donor_inputs"].items():
        if digest(ROOT / name) != expected:
            raise RuntimeError("Donor source changed: " + name)
    for name, expected in json.loads((out / "fixture-inputs.json").read_text()).items():
        if digest(ROOT / name) != expected or digest(source / "src/examples/bench_active_test" / Path(name).name) != expected:
            raise RuntimeError("Fixture changed: " + name)
    symbols = subprocess.check_output(["nm", "-C", str(out / "sitl-build/bin/px4")], text=True)
    for symbol in ("research_bench_control_main", "bench_active_test_main", "BenchControlOwner::cycle", "BenchOutputLease::update"):
        if symbol not in symbols:
            raise RuntimeError("Missing native symbol " + symbol)
    results = [out / "results.json.1", out / "results.json.2", out / "results.json"]
    binary_hash = digest(out / "sitl-build/bin/px4")
    for path in results:
        entries = json.loads(path.read_text())
        if {entry["scenario"] for entry in entries} != set(SCENARIOS):
            raise RuntimeError("Incomplete scenario coverage")
        for entry in entries:
            if not entry["passed"] or entry["binary_sha256"] != binary_hash:
                raise RuntimeError("Native qualification failure or binary mismatch")
    print("Production/fixture inputs and three full suites bind to the same native binary", flush=True)


def package(out, stage):
    verify(out, stage)
    if (stage / "qualification-receipt.json").exists():
        raise RuntimeError("Refusing to overwrite qualification receipt")
    source = out / "px4-source"
    manifest = json.loads((stage / "port-source-manifest.json").read_text())
    # Produce a complete reviewable patch against the actual old base, including
    # new files and compatibility backports, without staging the user's index.
    chunks = []
    with tempfile.TemporaryDirectory(prefix="patch-check-", dir=out) as temporary:
        baseline = Path(temporary)
        for name in sorted(manifest["files"]):
            result = subprocess.run(["git", "-C", str(source), "show", BASE + ":" + name], capture_output=True)
            original = result.stdout if result.returncode == 0 else b""
            current = (source / name).read_bytes()
            if result.returncode == 0:
                target = baseline / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(original)
            if current != original:
                chunks.append("diff --git a/" + name + " b/" + name + "\n")
                if result.returncode != 0:
                    chunks.append("new file mode 100644\n")
                chunks.extend(difflib.unified_diff(original.decode().splitlines(True), current.decode().splitlines(True),
                              fromfile="a/" + name if result.returncode == 0 else "/dev/null", tofile="b/" + name))
        patch = stage / "production-port-e8ea.patch"
        patch.write_text("".join(chunks))
        subprocess.run(["git", "apply", "--check", str(patch)], cwd=baseline, check=True)
        subprocess.run(["git", "apply", str(patch)], cwd=baseline, check=True)
        for name, entry in manifest["files"].items():
            if digest(baseline / name) != entry["sha256"]:
                raise RuntimeError("Complete port patch does not reproduce " + name)
    for name in manifest["files"]:
        target = stage / "ported-source" / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source / name, target)
    for name in ("src/modules/manual_control/MovingDiff.hpp", "src/lib/mathlib/math/filter/AlphaFilter.hpp",
                 "platforms/common/px4_work_queue/WorkItem.cpp", "platforms/common/px4_work_queue/ScheduledWorkItem.cpp"):
        if (source / name).read_bytes() != git("show", BASE + ":" + name):
            raise RuntimeError("Unintended base runtime change: " + name)
        target = stage / "ported-source" / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source / name, target)
    files = [p for p in out.iterdir() if p.is_file() and (".log" in p.name or ".json" in p.name or p.name.endswith(".cpp"))]
    files += list(out.glob("*/rc.capture")) + list(out.glob("*/synthetic-profile.txt"))
    files += [out / "sitl-build/bin/px4", out / "stock-test-CMakeLists.txt"]
    for label in ("debug", "release", "sanitize"):
        files += list((out / label).glob("*_test"))
    for label in ("sitl", "stock"):
        files += [out / (label + "-build") / name for name in ("CMakeCache.txt", "compile_commands.json", "px4_boardconfig.h")]
    for name in ("functional-ManualControl", "unit-ManualControlSelector", "unit-MovingDiff"):
        files += list((out / "stock-build").rglob(name))
    native = stage / "native-tests"
    native.mkdir()
    evidence = {}
    for p in sorted(set(files)):
        if p.is_file():
            relative = p.relative_to(out)
            target = native / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(p, target)
            evidence[str(relative)] = {"bytes": target.stat().st_size, "sha256": digest(target)}
    write_json(stage / "native-evidence-manifest.json", evidence)
    suites = [json.loads((out / name).read_text()) for name in ("results.json.1", "results.json.2", "results.json")]
    portable = {}
    for label in ("debug", "release", "sanitize"):
        text = (out / (label + "-tests.log")).read_text()
        counts = re.findall(r"^\d+: (\d+) .*checks, 0 failures", text, re.M)
        if "100% tests passed, 0 tests failed out of 9" not in text or sum(map(int, counts)) != 2431:
            raise RuntimeError("Portable test count/result mismatch")
        portable[label] = {"executables": 9, "checks": 2431, "passed": True}
    stock_log = (out / "stock-tests.log").read_text()
    if "100% tests passed" not in stock_log or "[  FAILED  ]" in stock_log:
        raise RuntimeError("Stock compatibility failed")
    if "Ran 19 tests" not in (out / "acceptance-checker-tests.log").read_text():
        raise RuntimeError("Physical acceptance checker coverage missing")
    smoke_log = (out / "disabled-smoke.log").read_text()
    if "H8_DISABLED_SMOKE_COMPLETE" not in smoke_log:
        raise RuntimeError("Disabled-startup smoke missing")
    arm = json.loads((stage / "candidate-receipt.json").read_text())
    for record in arm["artifacts"].values():
        if digest(stage / record["path"]) != record["sha256"]:
            raise RuntimeError("ARM artifact changed")
    temporary = Path("/tmp/px4-firmware") / arm["artifacts"]["px4"]["path"]
    temporary.parent.mkdir(parents=True, exist_ok=True)
    if temporary.exists():
        raise RuntimeError("Refusing temporary firmware overwrite")
    shutil.copyfile(stage / arm["artifacts"]["px4"]["path"], temporary)
    write_json(stage / "qualification-receipt.json", {
        "schema_version": 1, "scope": "step 3 software port qualification only; replacement image boot/output unverified",
        "base": BASE, "source_dirty": True, "native_binary_sha256": digest(out / "sitl-build/bin/px4"),
        "portable": portable, "stock_compatibility_gtests": 28, "physical_acceptance_checker_tests": 19,
        "native_repetitions": [{"scenarios": len(suite), "checks": sum(entry["checks"] for entry in suite), "passed": True} for suite in suites],
        "disabled_startup_smoke_passed": True, "complete_port_patch_reproduces_all_mapped_files": True,
        "production_port_patch_sha256": digest(patch), "source_manifest_sha256": digest(stage / "port-source-manifest.json"),
        "native_evidence_manifest_sha256": digest(stage / "native-evidence-manifest.json"),
        "arm_receipt_sha256": digest(stage / "candidate-receipt.json"), "firmware_sha256": arm["artifacts"]["px4"]["sha256"],
        "runner_sha256": digest(Path(__file__)), "fixture_inputs_sha256": digest(out / "fixture-inputs.json"),
        "working_reference_hardware_record_sha256": digest(REFERENCE / "hardware-startup-acceptance-20261019.json"),
        "source_worktree": str(source), "metadata_worktree": ".agents/worktrees/bench-e8ea-step3",
        "retained_native_root": str(out), "hardware_boot_verified": False, "physical_acceptance_granted": False,
        "outputs_authorized": False, "wsl_temporary_firmware": str(temporary),
    })
    print("Software qualification and complete source patch staged", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("prepare", "recipes", "update-dependencies", "host", "sitl", "stock", "test", "smoke", "verify", "package"))
    parser.add_argument("--evidence", type=Path, default=Path("/var/tmp/research-bench-step3-e8ea"))
    parser.add_argument("--stage", type=Path, default=ROOT / "build/bench_commissioning/bench-e8ea98b404-step3")
    parser.add_argument("--repetitions", type=int, default=3)
    parser.add_argument("--resume", action="store_true", help="resume only before the donor patch was applied")
    args = parser.parse_args()
    if args.action == "prepare":
        prepare(args.evidence, args.stage, args.resume)
    elif args.action == "recipes":
        for name in ("docker_stage.py", "build-container.sh"):
            destination = args.stage / name
            if destination.exists():
                raise RuntimeError("Refusing to overwrite staged recipe")
            shutil.copyfile(ROOT / "Tools/research_sitl/bench_rollback" / name, destination)
    elif args.action == "host":
        host(args.evidence)
    elif args.action == "update-dependencies":
        update_dependencies(args.evidence, args.stage)
    elif args.action == "sitl":
        sitl(args.evidence)
    elif args.action == "stock":
        stock(args.evidence)
    elif args.action == "test":
        test(args.evidence, args.repetitions)
    elif args.action == "smoke":
        smoke(args.evidence)
    elif args.action == "package":
        package(args.evidence, args.stage)
    else:
        verify(args.evidence, args.stage)


if __name__ == "__main__":
    main()
