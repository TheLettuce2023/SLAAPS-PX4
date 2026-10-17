#!/usr/bin/env python3
"""Retain H7 host/build evidence; never runs PX4 or operates hardware."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess


def digest(path):
    return hashlib.file_digest(path.open("rb"), "sha256").hexdigest()


def run(command, log, cwd=None, env=None):
    if log.exists():
        number = 1
        while log.with_name(log.name + f".previous-{number}").exists():
            number += 1
        shutil.copy2(log, log.with_name(log.name + f".previous-{number}"))
    with log.open("w") as stream:
        subprocess.run(command, cwd=cwd, env=env, stdout=stream,
                       stderr=subprocess.STDOUT, check=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("action", choices=["snapshot", "host", "px4", "manifest"])
    parser.add_argument("--evidence", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    git = shutil.which("git.exe") or "git"
    out = args.evidence.resolve()
    out.mkdir(parents=True, exist_ok=True)
    library = root / "src/lib/research_actuator_command"
    if args.action == "snapshot":
        if (out / "workspace-checkpoint.json").exists():
            raise RuntimeError("Refusing to overwrite the preservation checkpoint")
        paths = subprocess.check_output(
            [git, "ls-files", "-z", "--modified", "--others", "--exclude-standard"], cwd=root
        ).decode().split("\0")
        records = {p: digest(root / p) for p in paths if p and (root / p).is_file()}
        (out / "workspace-checkpoint.json").write_text(json.dumps(records, indent=2) + "\n")
        run([git, "status", "--short", "--untracked-files=all"], out / "workspace-status.txt", root)
        # Recover exact pre-edit anchors from the retained H6 source, checked
        # against the pre-H7 graph, without touching the user's checkout.
        graph = json.loads((root / "docs/RESEARCH_CODE_GRAPH.json").read_text())
        original = out / "original-anchors"
        original.mkdir(exist_ok=True)
        for node in graph["nodes"]:
            if node["path"].startswith("src/lib/research_actuator_command/"):
                source = Path("/var/tmp/research-manual-h6/px4-source") / node["path"]
                if digest(source) != node["sha256"]:
                    raise RuntimeError(f"Original mismatch: {source}")
                shutil.copy2(source, original / source.name)
        print(f"Checkpoint: {len(records)} files; original library anchors hash-verified")
    elif args.action == "host":
        run(["astyle", "--options=" + str(root / "Tools/astyle/astylerc"),
             *[str(library / name) for name in [
                 "ManualControlInput.hpp", "ManualControlInput.cpp",
                 "Px4ManualControlInput.hpp", "Px4ManualControlInput.cpp",
                 "BenchControlOwner.hpp", "BenchControlOwner.cpp", "BenchControlOwnerTest.cpp",
                 "Px4BenchControlRuntime.hpp", "Px4BenchControlRuntime.cpp"]]], out / "astyle.log")
        source = out / "source"
        shutil.copytree(library, source, dirs_exist_ok=True)
        for label, build_type, extra in [
            ("debug", "Debug", ""), ("release", "Release", ""),
            ("sanitize", "Debug", "-fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie")
        ]:
            build = out / label
            flags = "-Wall -Wextra -Werror -Wpedantic " + extra
            link = "-fsanitize=address,undefined -no-pie" if extra else ""
            run(["cmake", "-S", str(source), "-B", str(build), "-G", "Ninja",
                 "-DCMAKE_BUILD_TYPE=" + build_type, "-DCMAKE_CXX_FLAGS=" + flags,
                 "-DCMAKE_EXE_LINKER_FLAGS=" + link], out / f"{label}-configure.log")
            run(["cmake", "--build", str(build), "-j", "4"], out / f"{label}-build.log")
            environment = dict(os.environ)
            if extra:
                environment["ASAN_OPTIONS"] = "detect_leaks=1:abort_on_error=1"
                environment["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
            run(["ctest", "--test-dir", str(build), "-V", "--output-on-failure"], out / f"{label}-test.log", env=environment)
            print(label + " PASS", flush=True)
    elif args.action == "px4":
        source = out / "px4-source"
        if not source.exists():
            run(["cp", "-a", "/var/tmp/research-manual-h6/px4-source", str(source)], out / "source-copy.log")
        # The retained native tree supplies submodules and LF build scripts.
        # Overlay current modified tracked build inputs and this complete library.
        changed = subprocess.check_output([git, "diff", "--name-only", "HEAD", "-z"], cwd=root).decode().split("\0")
        for relative in changed:
            if relative and (root / relative).is_file():
                destination = source / relative
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(root / relative, destination)
        shutil.copytree(library, source / "src/lib/research_actuator_command", dirs_exist_ok=True)
        run(["arm-none-eabi-g++", "--version"], out / "arm-compiler.log")
        for label, config in [("sitl", "px4_sitl_default"), ("v6x", "px4_fmu-v6x_default")]:
            build = out / (label + "-build")
            run(["cmake", "-S", str(source), "-B", str(build), "-G", "Ninja",
                 "-DCONFIG=" + config, "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"], out / f"{label}-configure.log")
            # Explicitly order the pre-existing MAVLink generation prerequisite.
            run(["cmake", "--build", str(build), "--target", "mavlink_c_generate", "-j", "4"], out / f"{label}-mavlink.log")
            run(["cmake", "--build", str(build), "-j", "4"], out / f"{label}-full.log")
            run(["cmake", "--build", str(build), "--target", "research_actuator_command", "-j", "4"], out / f"{label}-library.log")
            executable = build / ("bin/px4" if label == "sitl" else "px4_fmu-v6x_default.elf")
            tool = "nm" if label == "sitl" else "arm-none-eabi-nm"
            symbols = subprocess.check_output([tool, "-C", str(executable)]).decode()
            if "research_actuator_command::" in symbols:
                raise RuntimeError("Research library unexpectedly linked into default firmware")
            (out / f"{label}-isolation.txt").write_text("Zero research_actuator_command symbols in default ELF\n")
            if label == "v6x":
                run(["arm-none-eabi-size", str(executable)], out / "v6x-linked-size.log")
                objects = sorted((build / "src/lib/research_actuator_command").rglob("*.obj"))
                run(["arm-none-eabi-size", *map(str, objects)], out / "v6x-unlinked-object-size.log")
            print(label + " PASS", flush=True)
    else:
        shutil.copy2(Path(__file__), out / "verify_bench_h7.py")
        for path in library.iterdir():
            if path.is_file():
                for view in [out / "source", out / "px4-source/src/lib/research_actuator_command"]:
                    if digest(path) != digest(view / path.name):
                        raise RuntimeError(f"Source view mismatch: {view / path.name}")
        checkpoint = json.loads((out / "workspace-checkpoint.json").read_text())
        allowed = {
            "Tools/research_sitl/verify_bench_h7.py", "docs/MANUAL_BENCH_CONTROL_H7.md",
            "docs/PROJECT_CONTEXT.md", "docs/PHASE_GATE_STATUS.md",
            "docs/EDF_CANARD_CONTROL_FOUNDATION.md", "docs/RESEARCH_CODE_GRAPH.json",
            *["src/lib/research_actuator_command/" + name for name in [
                "CMakeLists.txt", "ManualControlInput.hpp", "ManualControlInput.cpp",
                "Px4ManualControlInput.hpp", "Px4ManualControlInput.cpp",
                "BenchControlOwner.hpp", "BenchControlOwner.cpp", "BenchControlOwnerTest.cpp",
                "Px4BenchControlRuntime.hpp", "Px4BenchControlRuntime.cpp"]]
        }
        preserved = 0
        for relative, expected in checkpoint.items():
            if relative not in allowed:
                if digest(root / relative) != expected:
                    raise RuntimeError(f"Unrelated file changed: {relative}")
                preserved += 1
        (out / "preservation.txt").write_text(f"{preserved} unrelated checkpoint files unchanged\n")
        files = list(out.glob("*.log*")) + list(out.glob("*-isolation.txt"))
        files += [out / "verify_bench_h7.py", out / "workspace-checkpoint.json", out / "workspace-status.txt"]
        files += [out / "preservation.txt"]
        files += [out / f"{label}-build/compile_commands.json" for label in ["sitl", "v6x"]]
        files += list((out / "source").glob("*"))
        files += list((out / "v6x-build").glob("px4_fmu-v6x_default.*"))
        files += [out / "sitl-build/bin/px4"]
        files += list((out / "v6x-build/src/lib/research_actuator_command").rglob("*.obj"))
        records = {str(p.relative_to(out)): {"sha256": digest(p), "bytes": p.stat().st_size}
                   for p in sorted(set(files)) if p.is_file()}
        (out / "manifest.json").write_text(json.dumps(records, indent=2) + "\n")
        print(digest(out / "manifest.json"))


if __name__ == "__main__":
    main()
