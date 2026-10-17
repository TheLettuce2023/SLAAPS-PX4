#!/usr/bin/env python3
"""Isolated native closed-owner build and two disarmed, no-ingress PX4 runs."""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import platform
import re
import secrets
import shlex
import signal
import subprocess
import tempfile
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
spec = importlib.util.spec_from_file_location("c1a_bounded_helpers", HERE.parent / "run_probe.py")
base = importlib.util.module_from_spec(spec)
spec.loader.exec_module(base)

BASE_HASHES = {
    "src/modules/commander/Commander.cpp": "bd270eddc3482d1d543f619bb40d799f0b0987d7e2c0a4d59804dfcd7a84432b",
    "src/modules/commander/Commander.hpp": "83aefea00ed03a5d71950598a5ca3b2a843705ad678681b7c98578101e3203b0",
    "src/modules/commander/CMakeLists.txt": "d5cdab2484fd13fa9a20d4b4dbccde118adbdc9db38eeeea74771d8e3fe8308f",
}
BUILTINS = (base.BUILTINS - {"research_probe"}) | {"research_native_closed"}
WINDOW_US = 2_000_000  # Closed diagnostic observation window, not a control budget.
IDENTITIES = ("permitted_source", "controller_instance_id", "owner_lifetime_id",
              "native_run_id", "producer_lifetime_id", "clock_epoch")
CORE_SOURCES = {"RequestGuard.cpp", "DiagnosticGuard.cpp", "DiagnosticAdapter.cpp", "FrameMath.cpp",
                "TrustedTimeMap.cpp", "WireAdapter.cpp", "NativeClosedDiagnosticOwner.cpp"}
NATIVE_FLAGS = {"RESEARCH_SITL_DIAGNOSTIC_NATIVE", "RESEARCH_C1D_CLOSED_OWNER_TARGET"}


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def entry_status():
    # WSL Git stat() over a Windows checkout can take minutes. Only this
    # convenience report uses installed Windows Git; source hashes remain full.
    windows_git = Path("/mnt/c/Program Files/Git/cmd/git.exe")
    executable = str(windows_git) if str(ROOT).startswith("/mnt/c/") and windows_git.is_file() else "git"
    command = [executable, "-c", "core.autocrlf=true", "status", "--short", "--untracked-files=no", "--ignore-submodules=all"]
    return {"command": command, "output": subprocess.check_output(command, cwd=ROOT, text=True, timeout=60).strip()}


def source_hashes():
    """Same complete input set and SHA-256 bytes, read on the owning filesystem."""
    powershell = Path("/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe")
    windows_git = Path("/mnt/c/Program Files/Git/cmd/git.exe")
    if not (str(ROOT).startswith("/mnt/c/") and powershell.is_file() and windows_git.is_file()):
        return base.source_hashes()
    names = subprocess.check_output([str(windows_git), "ls-files", "-z", "--cached", "--others", "--exclude-standard"],
                                    cwd=ROOT, timeout=60).decode().split("\0")
    prefixes = {"src", "platforms", "boards", "cmake", "msg", "Tools", "ROMFS"}
    paths = [name for name in sorted(set(names)) if name and ("/" not in name or name.split("/", 1)[0] in prefixes)]
    request = {"root": "C:\\" + str(ROOT)[7:].replace("/", "\\"), "paths": paths}
    # Paths are data on stdin, never interpolated into executable script text.
    script = r'''$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false)
$request = [Console]::In.ReadToEnd() | ConvertFrom-Json
$result = @{}
foreach ($relative in $request.paths) {
    $full = Join-Path $request.root $relative
    if (Test-Path -LiteralPath $full -PathType Leaf) {
        $result[$relative] = (Get-FileHash -LiteralPath $full -Algorithm SHA256).Hash.ToLowerInvariant()
    }
}
$result | ConvertTo-Json -Compress
'''
    raw = subprocess.check_output([str(powershell), "-NoProfile", "-NonInteractive", "-Command", script],
                                  input=json.dumps(request).encode(), timeout=120)
    result = json.loads(raw.decode("utf-8-sig"))
    require(type(result) is dict and set(result).issubset(paths)
            and all(re.fullmatch(r"[0-9a-f]{64}", value) for value in result.values()), "Invalid native hash inventory")
    return result


def overlay_view(area):
    """Private mechanical Commander overlay, never edits original source."""
    view = area / "source"
    view.mkdir()
    private = {"src", "src/modules", "src/modules/commander"}

    def populate(source, destination, prefix=""):
        for item in source.iterdir():
            relative = f"{prefix}/{item.name}" if prefix else item.name
            target = destination / item.name
            if relative in {".vscode", "build"}:
                target.mkdir()
            elif relative in private:
                target.mkdir()
                populate(item, target, relative)
            elif relative in BASE_HASHES:
                require(item.is_file() and not item.is_symlink(), "Original overlay input is not regular")
                require(base.digest(item) == BASE_HASHES[relative], "Unreviewed Commander drift: " + relative)
                target.write_bytes(item.read_bytes().replace(b"\r\n", b"\n"))
            else:
                target.symlink_to(item, target_is_directory=item.is_dir())

    populate(ROOT, view)
    for name in (".vscode", "build"):
        (view / name).mkdir(exist_ok=True)
    records = {}
    for relative in BASE_HASHES:
        target = view / relative
        require(not target.is_symlink() and target.resolve().is_relative_to(view.resolve()), "Overlay escaped")
        records[relative] = {"base_sha256": base.digest(ROOT / relative), "normalized_sha256": base.digest(target)}
    patch = HERE / "commander_closed_owner.patch"
    content = patch.read_text()
    old = re.findall(r"^--- a/([^\t\n]+)(?:\t[^\n]*)?$", content, re.MULTILINE)
    new = re.findall(r"^\+\+\+ b/([^\t\n]+)(?:\t[^\n]*)?$", content, re.MULTILINE)
    require(len(old) == len(new) == 3 and set(old) == set(new) == set(BASE_HASHES), "Unexpected patch targets")
    require(sum(line.startswith("--- ") for line in content.splitlines()) == 3
            and sum(line.startswith("+++ ") for line in content.splitlines()) == 3, "Additional patch target")
    command = ["patch", "--batch", "--forward", "--fuzz=0", "--no-backup-if-mismatch", "-p1", "-i", str(patch)]
    for dry, label in ((True, "patch-check"), (False, "patch-apply")):
        with (area / (label + ".log")).open("w") as log:
            result = subprocess.run(command + (["--dry-run"] if dry else []), cwd=view,
                                    stdout=log, stderr=subprocess.STDOUT, timeout=30, check=False)
        require(result.returncode == 0, "Private patch failed: " + label)
    for relative, hashes in records.items():
        hashes["derived_sha256"] = base.digest(view / relative)
        require(hashes["derived_sha256"] != hashes["normalized_sha256"], "Declared overlay is unchanged")
        require(base.digest(ROOT / relative) == hashes["base_sha256"], "Original changed during patch")
    return view, {"patch_sha256": base.digest(patch), "files": records}


def check_config(output):
    enabled = {line.split("=", 1)[0] for line in (output / "boardconfig").read_text().splitlines()
               if line.endswith("=y")}
    modules = {name for name in enabled if name.startswith("CONFIG_MODULES_")}
    drivers = {name for name in enabled if name.startswith("CONFIG_DRIVERS_")}
    require(modules == {"CONFIG_MODULES_COMMANDER"} and not drivers, "Unexpected module/driver selection")
    require("CONFIG_BOARD_NOLOCKSTEP" in enabled and "CONFIG_MUORB_APPS_SYNC_TIMESTAMP" not in enabled,
            "Native diagnostic cannot use lockstep/proxy clocks")
    commands = json.loads((output / "compile_commands.json").read_text())
    forbidden = {"ENABLE_LOCKSTEP_SCHEDULER", "CONFIG_MUORB_APPS_SYNC_TIMESTAMP", "BOARD_LOCKSTEP",
                 "PX4_SIMULATOR_TIMESTAMP_PROXY", "RESEARCH_SITL_DIAGNOSTIC_HOST_TEST",
                 "RESEARCH_SITL_DIAGNOSTIC_OFFLINE", "RESEARCH_SITL_DIAGNOSTIC_NATIVE_CONTRACT_TEST",
                 "RESEARCH_SITL_DIAGNOSTIC_CAPABILITY", "RESEARCH_C1C_CAPTURE"}
    expected = {(ROOT / "src/lib/research_acceleration" / name).resolve() for name in CORE_SOURCES}
    expected |= {path.resolve() for path in (HERE / "bridge").glob("*.cpp")}
    expected.add((HERE / "px4_external/src/research_native_closed/research_native_closed.cpp").resolve())
    commander = output.parent / "source/src/modules/commander"
    marked = []
    for item in commands:
        tokens = shlex.split(item["command"])
        defines = [token[2:] for token in tokens if token.startswith("-D")]
        require(not any(token.split("=", 1)[0] in forbidden for token in defines), "Forbidden diagnostic/clock define")
        flags = [token for token in defines if token.split("=", 1)[0] in NATIVE_FLAGS]
        lexical = Path(os.path.abspath(item["file"]))
        source = lexical.resolve()
        is_commander = lexical.is_relative_to(commander)
        if is_commander:
            relative = lexical.relative_to(commander)
            require(source in {lexical, (ROOT / "src/modules/commander" / relative).resolve()}, "Commander source escaped")
        objects = [tokens[index + 1] for index, token in enumerate(tokens[:-1]) if token == "-o"]
        require(len(objects) == 1, "Missing or ambiguous compile output")
        commander_unit = "CMakeFiles/modules__commander.dir/" in objects[0]
        require(not commander_unit or is_commander, "Commander target compiled an unexpected source")
        participates = source in expected or commander_unit
        require(bool(flags) == participates, "Native marker/source mismatch: " + str(source))
        if participates:
            require(sorted(flags) == sorted(name + "=1" for name in NATIVE_FLAGS), "Native opt-in must be exact and unique")
            standards = [token for token in tokens if token.startswith("-std=")]
            require(standards and standards[-1] in {"-std=c++17", "-std=gnu++17"}, "Native class layout needs C++17")
            require("-fstack-usage" in tokens, "Missing native compiler stack-use evidence")
            require("platforms/ros2/include" not in item["command"], "Host include shim entered native build")
            marked.append(source)
    require(all(marked.count(source) == 1 for source in expected), "Missing or duplicate native source unit")
    require(marked.count(commander / "Commander.cpp") == 1, "Commander not compiled from private copy")
    apps = output / "platforms/posix/apps.cpp"
    builtins = set(re.findall(r'apps\["([a-zA-Z0-9_]+)"\]', apps.read_text()))
    require(builtins == BUILTINS, "Unexpected linked command: " + str(sorted(builtins)))
    return {"enabled_modules": sorted(modules), "enabled_drivers": sorted(drivers), "builtins": sorted(builtins),
            "native_units": len(marked), "compile_units": len(commands),
            "boardconfig_sha256": base.digest(output / "boardconfig"), "apps_sha256": base.digest(apps),
            "compile_commands_sha256": base.digest(output / "compile_commands.json")}


def build(area, manifest):
    view, overlay = overlay_view(area)
    manifest["overlay"] = overlay
    output = area / "build"
    source = view / HERE.relative_to(ROOT)
    env = dict(os.environ, CCACHE_DIR=str(area / "ccache"), CCACHE_DISABLE="1", PYTHONDONTWRITEBYTECODE="1",
               GIT_CONFIG_COUNT="1", GIT_CONFIG_KEY_0="core.autocrlf", GIT_CONFIG_VALUE_0="true",
               GIT_SUBMODULES_ARE_EVIL="1", GIT_TERMINAL_PROMPT="0", GIT_ALLOW_PROTOCOL="file")
    configure = ["cmake", "-S", str(view), "-B", str(output), "-G", "Ninja", "-DCONFIG=px4_sitl_c1d-default",
                 "-DVENDOR=px4", "-DMODEL=sitl", "-DLABEL=c1d-default", f"-DPX4_BOARD_DIR={view}/boards/px4/sitl",
                 f"-DPX4_CONFIG_FILE={source}/c1d-default.px4board",
                 f"-DEXTERNAL_MODULES_LOCATION={source}/px4_external", "-DRESEARCH_C1D_CLOSED_OWNER=ON",
                 "-DCMAKE_BUILD_TYPE=Debug", "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"]
    compile_command = ["cmake", "--build", str(output), "--target", "px4", "-j", "4"]
    manifest["build"] = {"view": str(view), "directory": str(output), "cache": str(area / "ccache"),
                         "mounts_created": False, "configure_command": configure, "build_command": compile_command}
    for label, command in (("configure", configure), ("build", compile_command)):
        base.save(area / "manifest.json", manifest)
        print(f"C1D {label}: {area / (label + '.log')}", flush=True)
        with (area / (label + ".log")).open("w") as log:
            code = base.bounded_command(command, view, env, log)
        manifest["build"][label + "_exit"] = code
        require(code == 0, f"{label} failed ({code}); retained log")
        manifest["configuration_audit"] = check_config(output)
    manifest["executable_sha256"] = base.digest(output / "bin/px4")
    stack = {str(path.relative_to(output)): base.digest(path) for path in sorted(output.rglob("*.su"))}
    require(stack, "No native stack-use artifacts")
    base.save(area / "stack_files.json", stack)
    manifest["stack_registry_sha256"] = base.digest(area / "stack_files.json")
    return output


def check_bound_inputs(area, manifest):
    require(base.digest(area / "source_hashes.json") == manifest["source_registry_sha256"], "Source registry changed")
    require(json.loads((area / "source_hashes.json").read_text()) == source_hashes(), "Build inputs changed")
    require(base.digest(area / "build/bin/px4") == manifest["executable_sha256"], "Executable changed")
    require(check_config(area / "build") == manifest["configuration_audit"], "Configuration changed")
    require(base.digest(HERE / "commander_closed_owner.patch") == manifest["overlay"]["patch_sha256"], "Patch changed")
    require(set(manifest["overlay"]["files"]) == set(BASE_HASHES), "Incomplete overlay inventory")
    for relative, hashes in manifest["overlay"]["files"].items():
        require(base.digest(ROOT / relative) == hashes["base_sha256"] == BASE_HASHES[relative], "Original Commander drift")
        normalized = hashlib.sha256((ROOT / relative).read_bytes().replace(b"\r\n", b"\n")).hexdigest()
        require(normalized == hashes["normalized_sha256"], "False normalized hash")
        require(base.digest(area / "source" / relative) == hashes["derived_sha256"], "Private Commander drift")
    require(base.digest(area / "stack_files.json") == manifest["stack_registry_sha256"], "Stack registry changed")
    actual = {str(path.relative_to(area / "build")): base.digest(path) for path in sorted((area / "build").rglob("*.su"))}
    require(actual == json.loads((area / "stack_files.json").read_text()), "Native stack artifacts changed")
    require(base.gazebo_identity() == manifest["gazebo_before"], "Separate Gazebo identity changed")


def check_run_release(area, manifest):
    """Technical review gate; no machine-generated stack certification."""
    review = json.loads((area / "native_review.json").read_text())
    require(set(review) == {"schema", "executable_sha256", "stack_registry_sha256", "source_registry_sha256",
                            "source_chain_bytes", "source_chain_limit_bytes", "minimum_marker_offset_bytes",
                            "native_profile_probes_pass", "master_review", "independent_review", "limitations"},
            "Incomplete native technical review")
    require(review["schema"] == "research-c1d-native-review/v1", "Unknown review schema")
    for key in ("executable_sha256", "stack_registry_sha256", "source_registry_sha256"):
        require(review[key] == manifest[key], "Review does not bind current evidence")
    require(type(review["source_chain_bytes"]) is int and 0 < review["source_chain_bytes"] <= 32768,
            "Native source chain exceeds diagnostic ceiling")
    require(review["source_chain_limit_bytes"] == 32768 and review["minimum_marker_offset_bytes"] == 49152,
            "Stack criterion changed")
    require(review["native_profile_probes_pass"] is True and review["master_review"] == "accepted"
            and review["independent_review"] == "accepted", "Native runtime review not accepted")
    require(type(review["limitations"]) is str and bool(review["limitations"]), "Missing stack review limitations")
    return base.digest(area / "native_review.json")


def closed_run(area, output, index, used_instances, used_ids):
    runtime = area / f"run-{index}"
    runtime.mkdir()
    startup = runtime / "startup.sh"
    startup.write_bytes((HERE / "startup.sh").read_bytes())
    os.mkfifo(runtime / "blocked.fifo", 0o600)
    instance = secrets.randbelow(50000) + 10000
    while instance in used_instances:
        instance = secrets.randbelow(50000) + 10000
    used_instances.add(instance)
    bindings = {}
    for name in IDENTITIES:
        identity = secrets.randbits(64)
        while identity == 0 or identity in used_ids:
            identity = secrets.randbits(64)
        used_ids.add(identity)
        bindings[name] = str(identity)
    command = [str(output / "bin/px4"), "-i", str(instance), "-w", str(runtime), "-s", str(startup)]
    record = {"schema": "research-c1d-run/v1", "command": command, "instance": instance, "runtime": str(runtime),
              "bindings": bindings, "scope": "closed_diagnostic_no_ingress_no_output", "status": "incomplete",
              "startup_sha256": base.digest(startup), "start_monotonic_ns": time.monotonic_ns()}
    base.save(runtime / "run.json", record)
    env = {key: value for key, value in os.environ.items()
           if not key.startswith(("PX4_", "SIM_", "GZ_", "C1A_", "C1C_", "C1D_"))
           and key not in {"replay", "replay_mode"}}
    for variable, identity in zip(("SOURCE_ID", "CONTROLLER_ID", "OWNER_ID", "RUN_ID", "PRODUCER_ID", "CLOCK_EPOCH"), IDENTITIES):
        env["C1D_" + variable] = bindings[identity]
    env["C1D_WINDOW_US"] = str(WINDOW_US)
    with (runtime / "stdout.log").open("w") as log:
        child = subprocess.Popen(command, cwd=runtime, env=env, stdin=subprocess.PIPE, stdout=log,
                                 stderr=subprocess.STDOUT, text=True, start_new_session=True)
        record["pid"] = child.pid
        record["forced_shutdown"] = False
        try:
            child.communicate("shutdown\n", timeout=20)
            record["status"] = "captured_unscored"
        except BaseException:
            record["forced_shutdown"] = True
            if child.poll() is None:
                os.killpg(child.pid, signal.SIGTERM)
                try:
                    child.communicate(timeout=5)
                except subprocess.TimeoutExpired:
                    os.killpg(child.pid, signal.SIGKILL)
                    child.communicate(timeout=5)
            raise
        finally:
            record["exit_code"] = child.returncode
            record["end_monotonic_ns"] = time.monotonic_ns()
            record["stdout_sha256"] = base.digest(runtime / "stdout.log")
            for filename, key in (("trace.json", "trace_sha256"), ("trace-repeat.json", "repeat_trace_sha256")):
                path = runtime / filename
                record[key] = base.digest(path) if path.is_file() else None
            base.save(runtime / "run.json", record)
    return record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run-built", type=Path, help="run an exact source-stable build AFTER recorded native technical review")
    args = parser.parse_args()
    require(platform.system() == "Linux" and platform.machine() == "x86_64", "Requires existing native x86_64 Ubuntu WSL")
    if args.run_built:
        area = args.run_built.resolve()
        require(area.parent == Path("/var/tmp") and area.name.startswith("c1d_px4_"), "Not an owned C1D build area")
        manifest = json.loads((area / "manifest.json").read_text())
        require(manifest["schema"] == "research-c1d-native/v1" and manifest["source"] == str(ROOT)
                and manifest["status"] == "build_only_pending_native_review" and not manifest["runs"]
                and not list(area.glob("run-*")), "Only a fresh successful unrun build may be released")
        check_bound_inputs(area, manifest)
        manifest["native_review_sha256"] = check_run_release(area, manifest)
    else:
        area = Path(tempfile.mkdtemp(prefix="c1d_px4_", dir="/var/tmp"))
        manifest = {"schema": "research-c1d-native/v1", "status": "incomplete", "source": str(ROOT), "runs": [],
                    "scope": "closed_diagnostic_no_ingress_no_output", "px4_revision": base.git("rev-parse", "HEAD"),
                    "entry_status": entry_status(),
                    "entry_status_scope": "tracked parent changes only; full build inputs hashed and Gazebo checked separately",
                    "submodules": base.git("submodule", "status", "--recursive"), "kernel": platform.release(),
                    "python": platform.python_version(), "window_us": WINDOW_US, "build_provenance": "fresh_area",
                    "gazebo_before": base.gazebo_identity()}
        base.save(area / "source_hashes.json", source_hashes())
        manifest["source_registry_sha256"] = base.digest(area / "source_hashes.json")
    print(f"C1D retained artifacts: {area}", flush=True)
    before = json.loads((area / "source_hashes.json").read_text())
    base.save(area / "manifest.json", manifest)
    candidate = None
    try:
        if args.run_built:
            from check_closed_owner import check_run
            used_instances, used_ids = set(), set()
            for index in (1, 2):
                record = closed_run(area, area / "build", index, used_instances, used_ids)
                manifest["runs"].append(record)
                base.save(area / "manifest.json", manifest)
                require(json.loads((Path(record["runtime"]) / "run.json").read_text()) == record, "Run record changed")
                require(record["startup_sha256"] == base.digest(HERE / "startup.sh"), "Startup script not reviewed bytes")
                base.save(area / f"checked-run-{index}.json", check_run(Path(record["runtime"]), record["bindings"]))
            candidate = "two_runs_checked"
        else:
            build(area, manifest)
            candidate = "build_only_pending_native_review"
        check_bound_inputs(area, manifest)
    except (RuntimeError, ValueError, OSError, subprocess.SubprocessError, KeyError, TypeError) as error:
        manifest["failure"] = str(error)
        candidate = "fail"
    except BaseException:
        manifest["failure"] = "Interrupted before completion"
        candidate = "fail"
        raise
    finally:
        after = source_hashes()
        manifest["changed_source_inputs"] = [name for name in sorted(before.keys() | after.keys()) if before.get(name) != after.get(name)]
        manifest["gazebo_after"] = base.gazebo_identity()
        if manifest["changed_source_inputs"] or manifest["gazebo_after"] != manifest["gazebo_before"]:
            candidate = "fail"
            manifest["failure"] = "Source or separate Gazebo identity changed"
        manifest["status"] = candidate or "fail"
        base.save(area / "manifest.json", manifest)
    print(json.dumps({"artifacts": str(area), "status": manifest["status"], "failure": manifest.get("failure")}), flush=True)
    return 1 if manifest["status"] == "fail" else 0


if __name__ == "__main__":
    raise SystemExit(main())
