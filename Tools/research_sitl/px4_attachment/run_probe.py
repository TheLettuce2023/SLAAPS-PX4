#!/usr/bin/env python3
"""Isolated, no-output PX4 C1A build and passive diagnostic runner (Linux)."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import secrets
import shutil
import signal
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
CONFIG_MODULES = {"CONFIG_MODULES_COMMANDER"}
CONFIG_DRIVERS: set[str] = set()
BUILTINS = {"commander", "param", "shutdown", "listener", "uorb", "ver", "research_probe",
            "list_tasks", "list_files", "sleep"}  # Last three: platforms/common/apps.cpp.in.


def digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def git(*args: str, cwd: Path = ROOT) -> str:
    return subprocess.check_output(
        ["git", "-c", "core.autocrlf=true", *args], cwd=cwd, text=True
    ).strip()


def source_hashes() -> dict[str, str]:
    # Includes dirty/untracked build inputs, not unrelated docs/CAD or ignored
    # generated artifacts. Entry workspace preservation is audited separately.
    paths = subprocess.check_output(
        ["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard"],
        cwd=ROOT,
    ).decode().split("\0")
    prefixes = {"src", "platforms", "boards", "cmake", "msg", "Tools", "ROMFS"}
    return {name: digest(ROOT / name) for name in sorted(set(paths))
            if name and ("/" not in name or name.split("/", 1)[0] in prefixes)
            and (ROOT / name).is_file()}


def save(path: Path, value: dict) -> None:
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n")


def gazebo_identity() -> dict:
    gz = ROOT / "Tools/simulation/gz"
    return {"revision": git("rev-parse", "HEAD", cwd=gz),
            "status": git("status", "--short", cwd=gz),
            "cessna_sdf_sha256": digest(gz / "models/rc_cessna/model.sdf")}


def bounded_command(command: list[str], cwd: Path, env: dict, log) -> int:
    process = subprocess.Popen(command, cwd=cwd, env=env, stdout=log,
                               stderr=subprocess.STDOUT, start_new_session=True)
    try:
        return process.wait(timeout=900)
    except BaseException:
        # Clean up only the new command's process group, including compilers.
        if process.poll() is None:
            os.killpg(process.pid, signal.SIGTERM)
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait(timeout=5)
        raise


def check_config(build: Path) -> dict:
    config = (build / "boardconfig").read_text()
    enabled = {line.split("=", 1)[0] for line in config.splitlines()
               if line.endswith("=y")}
    modules = {x for x in enabled if x.startswith("CONFIG_MODULES_")}
    drivers = {x for x in enabled if x.startswith("CONFIG_DRIVERS_")}
    if modules != CONFIG_MODULES or drivers != CONFIG_DRIVERS:
        raise RuntimeError(f"Unexpected selected modules/drivers: {modules}, {drivers}")
    if "CONFIG_BOARD_NOLOCKSTEP" not in enabled:
        raise RuntimeError("C1A requires explicit no-lockstep build")
    commands = json.loads((build / "compile_commands.json").read_text())
    if any(macro in c["command"] for c in commands for macro in
           ("-DENABLE_LOCKSTEP_SCHEDULER", "-DCONFIG_MUORB_APPS_SYNC_TIMESTAMP")):
        raise RuntimeError("Unexpected lockstep/proxy-clock compile definition")
    if "CONFIG_MUORB_APPS_SYNC_TIMESTAMP" in enabled:
        raise RuntimeError("Unexpected proxy timestamp configuration")
    apps = build / "platforms/posix/apps.cpp"
    builtins = set(re.findall(r'apps\["([a-zA-Z0-9_]+)"\]', apps.read_text()))
    if builtins != BUILTINS:
        raise RuntimeError(f"Unexpected linked builtins: {sorted(builtins)}")
    return {"enabled_modules": sorted(modules), "enabled_drivers": sorted(drivers),
            "builtins": sorted(builtins), "apps_sha256": digest(apps),
            "boardconfig_sha256": digest(build / "boardconfig"),
            "compile_commands_sha256": digest(build / "compile_commands.json")}


def create_view(area: Path) -> Path:
    view = area / "source"
    if view.exists():
        if view.is_symlink() or view.resolve().parent != area:
            raise RuntimeError("Source-view directory escaped")
        # Resume only this runner's exact source view; never adopt an arbitrary
        # build tree or silently retarget its symlinks.
        for item in ROOT.iterdir():
            candidate = view / item.name
            if item.name not in {".vscode", "build"}:
                if not candidate.is_symlink() or candidate.resolve() != item.resolve():
                    raise RuntimeError(f"Source-view ownership mismatch: {candidate}")
        for name in (".vscode", "build"):
            if (view / name).is_symlink() or (view / name).resolve().parent != view:
                raise RuntimeError("Private source-view directory escaped")
        return view
    view.mkdir()
    for item in ROOT.iterdir():
        if item.name not in {".vscode", "build"}:
            (view / item.name).symlink_to(item, target_is_directory=item.is_dir())
    (view / ".vscode").mkdir()
    (view / "build").mkdir()
    return view


def build(area: Path, manifest: dict) -> Path:
    view = create_view(area)
    output = area / "build"
    attachment = view / "Tools/research_sitl/px4_attachment"
    env = dict(os.environ, CCACHE_DIR=str(area / "ccache"), CCACHE_DISABLE="1",
               PYTHONDONTWRITEBYTECODE="1", GIT_CONFIG_COUNT="1",
               GIT_CONFIG_KEY_0="core.autocrlf", GIT_CONFIG_VALUE_0="true",
               GIT_SUBMODULES_ARE_EVIL="1", GIT_TERMINAL_PROMPT="0",
               GIT_ALLOW_PROTOCOL="file")
    # No mounts, sudo, package install, fetch or stock launcher; one build at a time.
    configure = ["cmake", "-S", str(view), "-B", str(output), "-G", "Ninja",
                 "-DCONFIG=px4_sitl_c1-default", "-DVENDOR=px4", "-DMODEL=sitl",
                 "-DLABEL=c1-default", f"-DPX4_BOARD_DIR={view}/boards/px4/sitl",
                 f"-DPX4_CONFIG_FILE={attachment}/c1-default.px4board",
                 f"-DEXTERNAL_MODULES_LOCATION={attachment}/px4_external",
                 "-DCMAKE_BUILD_TYPE=Debug", "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"]
    compile_command = ["cmake", "--build", str(output), "--target", "px4", "-j", "2"]
    manifest["build"] = {"view": str(view), "directory": str(output),
                         "cache": str(area / "ccache"), "mounts_created": False,
                         "configure_command": configure, "build_command": compile_command}
    save(area / "manifest.json", manifest)
    for name, command in (("configure", configure), ("build", compile_command)):
        print(f"C1A {name}; log: {area / (name + '.log')}", flush=True)
        with (area / (name + ".log")).open("w") as log:
            exit_code = bounded_command(command, view, env, log)
        manifest["build"][name + "_exit"] = exit_code
        save(area / "manifest.json", manifest)
        if exit_code:
            raise RuntimeError(f"{name} failed ({exit_code}); retain log")
        if name == "configure":
            # Catch unexpected builtins before spending time compiling them,
            # and long before a PX4 process can start.
            manifest["configuration_audit"] = check_config(output)
            save(area / "manifest.json", manifest)
    manifest["configuration_audit"] = check_config(output)
    manifest["executable_sha256"] = digest(output / "bin/px4")
    return output


def probe_run(area: Path, output: Path, index: int) -> dict:
    runtime = area / f"run-{index}"
    runtime.mkdir()
    instance = secrets.randbelow(50000) + 10000
    # PX4 itself holds its instance lock. Never remove a lock or attach to an
    # existing process; a collision is a failed run, not permission to reuse it.
    script = area / "source/Tools/research_sitl/px4_attachment/startup.sh"
    command = [str(output / "bin/px4"), "-i", str(instance), "-w", str(runtime),
               "-s", str(script)]
    record = {"command": command, "instance": instance, "runtime": str(runtime),
              "admission": "not_instantiated", "research_network": "not_instantiated",
              "start_monotonic_ns": time.monotonic_ns(), "status": "incomplete"}
    save(runtime / "run.json", record)
    # Clear simulator/replay/parameter overrides; use only the private startup.
    env = {k: v for k, v in os.environ.items()
           if not k.startswith(("PX4_", "SIM_", "GZ_")) and k not in {"replay", "replay_mode"}}
    with (runtime / "stdout.log").open("w") as log:
        process = subprocess.Popen(command, cwd=runtime, env=env, stdin=subprocess.PIPE,
                                   stdout=log, stderr=subprocess.STDOUT, text=True,
                                   start_new_session=True)
        record["pid"] = process.pid
        try:
            # Queued for the shell after bounded startup/probe completes.
            process.communicate("shutdown\n", timeout=20)
            record["exit_code"] = process.returncode
            record["forced_shutdown"] = False
        except subprocess.TimeoutExpired:
            # Only this runner's newly created process group is in scope.
            os.killpg(process.pid, signal.SIGTERM)
            record["forced_shutdown"] = True
            try:
                process.communicate(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.communicate(timeout=5)
            record["exit_code"] = process.returncode
        except BaseException:
            record["forced_shutdown"] = True
            if process.poll() is None:
                os.killpg(process.pid, signal.SIGTERM)
                try:
                    process.communicate(timeout=5)
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid, signal.SIGKILL)
                    process.communicate(timeout=5)
            record["exit_code"] = process.returncode
            raise
        finally:
            record["end_monotonic_ns"] = time.monotonic_ns()
            record["stdout_sha256"] = digest(runtime / "stdout.log")
            save(runtime / "run.json", record)
    return record


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run", action="store_true", help="also run two passive PX4 probes")
    parser.add_argument("--resume-build", type=Path,
                        help="resume this runner's failed pre-runtime build, archiving prior evidence")
    args = parser.parse_args()
    if platform.system() != "Linux":
        raise SystemExit("Run in the existing Ubuntu WSL distribution")
    if args.resume_build:
        area = args.resume_build.resolve()
        if area.parent != Path("/var/tmp") or not area.name.startswith("c1a_px4_"):
            raise SystemExit("Resume target must be an explicitly owned /var/tmp/c1a_px4_* area")
        previous = json.loads((area / "manifest.json").read_text())
        if (previous.get("schema") != "research-c1a/v1" or previous.get("status") != "fail"
                or previous.get("source") != str(ROOT) or previous.get("runs") != []
                or previous.get("px4_revision") != git("rev-parse", "HEAD")
                or list(area.glob("run-*"))):
            raise SystemExit("Only a failed same-source pre-runtime build may be resumed")
        if previous.get("build", {}).get("directory") != str(area / "build"):
            raise SystemExit("Previous build ownership mismatch")
        output = area / "build"
        if output.is_symlink() or not output.is_dir() or output.resolve().parent != area:
            raise SystemExit("Previous build directory escaped")
        cache = (output / "CMakeCache.txt").read_text()
        expected_home = f"CMAKE_HOME_DIRECTORY:INTERNAL={area / 'source'}"
        if expected_home not in cache.splitlines():
            raise SystemExit("Previous cached source ownership mismatch")
        create_view(area)  # Validate before modifying even retained build metadata.
        archive = Path(tempfile.mkdtemp(prefix="failed-attempt-", dir=area))
        for relative in ("manifest.json", "source_hashes.json", "configure.log", "build.log",
                         "build/boardconfig", "build/platforms/posix/apps.cpp",
                         "build/compile_commands.json", "build/CMakeCache.txt"):
            source = area / relative
            if source.is_file():
                destination = archive / relative
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, destination)
        print(f"C1A previous failure archived: {archive}", flush=True)
    else:
        area = Path(tempfile.mkdtemp(prefix="c1a_px4_", dir="/var/tmp"))
    print(f"C1A retained artifacts: {area}", flush=True)
    save(area / "manifest.json", {"schema": "research-c1a/v1", "status": "incomplete",
                                 "stage": "source_inventory"})
    manifest = {"schema": "research-c1a/v1", "status": "incomplete",
                "scope": "passive_no_output_no_admission", "source": str(ROOT),
                "px4_revision": git("rev-parse", "HEAD"),
                "gz_revision": git("rev-parse", "HEAD", cwd=ROOT / "Tools/simulation/gz"),
                "entry_status": git("status", "--short", "--ignore-submodules=dirty"),
                "entry_status_scope": "parent changes/gitlink revisions; submodule dirty trees excluded",
                "submodules": git("submodule", "status", "--recursive"),
                "kernel": platform.release(),
                "python": platform.python_version(), "runs": []}
    manifest["build_provenance"] = ({"kind": "incremental_retry", "prior_failure": str(archive)}
                                    if args.resume_build else {"kind": "fresh_area"})
    manifest["gazebo_before"] = gazebo_identity()
    before = source_hashes()
    save(area / "source_hashes.json", before)
    save(area / "manifest.json", manifest)
    try:
        output = build(area, manifest)
        if args.run:
            for index in range(1, 3):
                manifest["runs"].append(probe_run(area, output, index))
                save(area / "manifest.json", manifest)
            # A separate evidence checker must score the probe's actual rows.
            manifest["status"] = "built_runs_unscored"
        else:
            manifest["status"] = "build_only"
    except (RuntimeError, OSError, subprocess.SubprocessError) as error:
        manifest["status"] = "fail"
        manifest["failure"] = str(error)
    finally:
        after = source_hashes()
        manifest["changed_source_inputs"] = [name for name in sorted(before.keys() | after.keys())
                                              if before.get(name) != after.get(name)]
        if manifest["changed_source_inputs"]:
            manifest["status"] = "fail"
            manifest["failure"] = "Source changed during the build/run; evidence not source-stable"
        manifest["gazebo_after"] = gazebo_identity()
        if manifest["gazebo_before"] != manifest["gazebo_after"]:
            manifest["status"] = "fail"
            manifest["failure"] = "Gazebo identity/status or stock model changed"
        save(area / "manifest.json", manifest)
    print(json.dumps({"artifacts": str(area), "status": manifest["status"],
                      "failure": manifest.get("failure")}), flush=True)
    return 1 if manifest["status"] == "fail" else 0


if __name__ == "__main__":
    raise SystemExit(main())
