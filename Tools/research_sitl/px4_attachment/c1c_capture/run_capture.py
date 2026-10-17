#!/usr/bin/env python3
"""Owned, disarmed C1C producer-capture build/run; never an admission service."""
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
import shutil
import signal
import subprocess
import tempfile
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
spec = importlib.util.spec_from_file_location("c1a_build_helpers", HERE.parent / "run_probe.py")
base = importlib.util.module_from_spec(spec)
spec.loader.exec_module(base)

BASE_HASHES = {
    "src/modules/commander/Commander.cpp": "bd270eddc3482d1d543f619bb40d799f0b0987d7e2c0a4d59804dfcd7a84432b",
    "src/modules/commander/Commander.hpp": "83aefea00ed03a5d71950598a5ca3b2a843705ad678681b7c98578101e3203b0",
    "src/modules/commander/CMakeLists.txt": "d5cdab2484fd13fa9a20d4b4dbccde118adbdc9db38eeeea74771d8e3fe8308f",
}
BUILTINS = (base.BUILTINS - {"research_probe"}) | {"research_state_capture"}
WINDOW_US = 2_000_000  # Finite diagnostic capture duration, not a freshness/control budget.


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def overlay_view(area: Path) -> tuple[Path, dict]:
    """Create private ancestors; only mechanical generated-source edits follow."""
    view = area / "source"
    view.mkdir()
    private_dirs = {"src", "src/modules", "src/modules/commander"}

    def populate(source: Path, destination: Path, prefix: str = "") -> None:
        for item in source.iterdir():
            relative = f"{prefix}/{item.name}" if prefix else item.name
            target = destination / item.name
            if relative in {".vscode", "build"}:
                target.mkdir()
            elif relative in private_dirs:
                target.mkdir()
                populate(item, target, relative)
            elif relative in BASE_HASHES:
                require(item.is_file() and not item.is_symlink(), "Base overlay file is not regular")
                require(base.digest(item) == BASE_HASHES[relative], f"Unreviewed base drift: {relative}")
                # Preserve original checkout bytes; normalize only generated private copies.
                target.write_bytes(item.read_bytes().replace(b"\r\n", b"\n"))
            else:
                target.symlink_to(item, target_is_directory=item.is_dir())

    populate(ROOT, view)
    for name in (".vscode", "build"):
        (view / name).mkdir(exist_ok=True)
    records = {}
    for relative in BASE_HASHES:
        target = view / relative
        require(target.is_file() and not target.is_symlink(), "Overlay target is not a private regular file")
        require(target.resolve().is_relative_to(view.resolve()), "Overlay target escaped private view")
        records[relative] = {"base_sha256": base.digest(ROOT / relative),
                             "normalized_sha256": base.digest(target)}
    patch = HERE / "commander_receipts.patch"
    patch_text = patch.read_text()
    old = re.findall(r"^--- a/([^\t\n]+)(?:\t[^\n]*)?$", patch_text, re.MULTILINE)
    new = re.findall(r"^\+\+\+ b/([^\t\n]+)(?:\t[^\n]*)?$", patch_text, re.MULTILINE)
    require(len(old) == len(new) == 3 and set(old) == set(new) == set(BASE_HASHES),
            "Patch must modify exactly the three reviewed Commander copies")
    require(sum(line.startswith("--- ") for line in patch_text.splitlines()) == 3
            and sum(line.startswith("+++ ") for line in patch_text.splitlines()) == 3,
            "Unrecognized extra patch targets")
    command = ["patch", "--batch", "--forward", "--fuzz=0", "--no-backup-if-mismatch", "-p1", "-i", str(patch)]
    for dry, log_name in ((True, "patch-check.log"), (False, "patch-apply.log")):
        with (area / log_name).open("w") as log:
            result = subprocess.run(command + (["--dry-run"] if dry else []), cwd=view,
                                    stdout=log, stderr=subprocess.STDOUT, timeout=30, check=False)
        require(result.returncode == 0, f"Exact private overlay failed; see {log_name}")
    for relative, record in records.items():
        record["derived_sha256"] = base.digest(view / relative)
        require(record["derived_sha256"] != record["normalized_sha256"], "Overlay did not change its declared target")
        require(base.digest(ROOT / relative) == record["base_sha256"], "Checkout base changed during overlay")
    return view, {"patch_sha256": base.digest(patch), "files": records}


def check_config(output: Path) -> dict:
    enabled = {line.split("=", 1)[0] for line in (output / "boardconfig").read_text().splitlines()
               if line.endswith("=y")}
    modules = {name for name in enabled if name.startswith("CONFIG_MODULES_")}
    drivers = {name for name in enabled if name.startswith("CONFIG_DRIVERS_")}
    require(modules == {"CONFIG_MODULES_COMMANDER"} and not drivers, "Unexpected runtime module/driver selection")
    require("CONFIG_BOARD_NOLOCKSTEP" in enabled and "CONFIG_MUORB_APPS_SYNC_TIMESTAMP" not in enabled,
            "Capture requires native non-lockstep/non-proxy clock")
    commands = json.loads((output / "compile_commands.json").read_text())
    require(not any(flag in item["command"] for item in commands for flag in
                    ("-DENABLE_LOCKSTEP_SCHEDULER", "-DCONFIG_MUORB_APPS_SYNC_TIMESTAMP",
                     "-DBOARD_LOCKSTEP", "-DPX4_SIMULATOR_TIMESTAMP_PROXY",
                     "-DRESEARCH_SITL_STATE_RECEIPT_HOST_TEST", "-DRESEARCH_SITL_STATE_CAPTURE_HOST_TEST",
                     "-DRESEARCH_SITL_DIAGNOSTIC_HOST_TEST")),
            "Forbidden clock or spoofed host diagnostic marker")
    commander = [item for item in commands if item["file"].endswith("/commander/Commander.cpp")]
    require(len(commander) == 1, "Missing or duplicate isolated Commander compile unit")
    require(Path(commander[0]["file"]).resolve() == (output.parent / "source/src/modules/commander/Commander.cpp").resolve(),
            "Commander compiled outside private overlay")
    required_sources = {(output.parent / "source/src/modules/commander/Commander.cpp").resolve(),
                        (HERE / "producer/StateCaptureProducer.cpp").resolve(),
                        (HERE / "producer/Px4StateReceiptProducer.cpp").resolve(),
                        (HERE / "px4_external/src/research_state_capture/research_state_capture.cpp").resolve()}
    marked_sources = []
    for item in commands:
        markers = [token for token in shlex.split(item["command"]) if token.startswith("-DRESEARCH_C1C_CAPTURE")]
        if markers:
            require(markers == ["-DRESEARCH_C1C_CAPTURE=1"], "Capture marker is not exact single opt-in")
            source = Path(item["file"]).resolve()
            private_commander = output.parent / "source/src/modules/commander"
            lexical = Path(os.path.abspath(item["file"]))
            private_unit = lexical.is_relative_to(private_commander)
            if private_unit:
                relative = lexical.relative_to(private_commander)
                # Unchanged Commander units are read-only checkout symlinks;
                # only the three overlay targets must resolve inside the view.
                private_unit = source in {lexical, (ROOT / "src/modules/commander" / relative).resolve()}
            require(source in required_sources or private_unit, "Capture definition escaped isolated source scope")
            marked_sources.append(source)
    require(all(marked_sources.count(source) == 1 for source in required_sources),
            "Missing or duplicate capture producer/collector compilation")
    apps = output / "platforms/posix/apps.cpp"
    builtins = set(re.findall(r'apps\["([a-zA-Z0-9_]+)"\]', apps.read_text()))
    require(builtins == BUILTINS, f"Unexpected linked builtins: {sorted(builtins)}")
    return {"enabled_modules": sorted(modules), "enabled_drivers": sorted(drivers), "builtins": sorted(builtins),
            "boardconfig_sha256": base.digest(output / "boardconfig"), "apps_sha256": base.digest(apps),
            "compile_commands_sha256": base.digest(output / "compile_commands.json")}


def build(area: Path, manifest: dict) -> Path:
    view, overlay = overlay_view(area)
    manifest["overlay"] = overlay
    output = area / "build"
    # Keep generated object paths under the owned, shell-safe source view.
    # CMake's command database can leave apostrophes unquoted in object paths
    # derived from an absolute external source path. Do not relax token audits.
    capture_source = view / HERE.relative_to(ROOT)
    env = dict(os.environ, CCACHE_DIR=str(area / "ccache"), CCACHE_DISABLE="1", PYTHONDONTWRITEBYTECODE="1",
               GIT_CONFIG_COUNT="1", GIT_CONFIG_KEY_0="core.autocrlf", GIT_CONFIG_VALUE_0="true",
               GIT_SUBMODULES_ARE_EVIL="1", GIT_TERMINAL_PROMPT="0", GIT_ALLOW_PROTOCOL="file")
    configure = ["cmake", "-S", str(view), "-B", str(output), "-G", "Ninja",
                 "-DCONFIG=px4_sitl_c1c-default", "-DVENDOR=px4", "-DMODEL=sitl", "-DLABEL=c1c-default",
                 f"-DPX4_BOARD_DIR={view}/boards/px4/sitl", f"-DPX4_CONFIG_FILE={capture_source}/c1c-default.px4board",
                 f"-DEXTERNAL_MODULES_LOCATION={capture_source}/px4_external", "-DRESEARCH_C1C_CAPTURE=ON",
                 "-DCMAKE_BUILD_TYPE=Debug", "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"]
    compile_command = ["cmake", "--build", str(output), "--target", "px4", "-j", "4"]
    manifest["build"] = {"view": str(view), "directory": str(output), "cache": str(area / "ccache"),
                         "mounts_created": False, "configure_command": configure, "build_command": compile_command}
    for label, command in (("configure", configure), ("build", compile_command)):
        base.save(area / "manifest.json", manifest)
        print(f"C1C {label}: {area / (label + '.log')}", flush=True)
        with (area / (label + ".log")).open("w") as log:
            code = base.bounded_command(command, view, env, log)
        manifest["build"][label + "_exit"] = code
        require(code == 0, f"{label} failed ({code}); retained log")
        manifest["configuration_audit"] = check_config(output)
    manifest["executable_sha256"] = base.digest(output / "bin/px4")
    return output


def capture_run(area: Path, output: Path, index: int, used_instances: set[int], used_ids: set[int]) -> dict:
    runtime = area / f"run-{index}"
    runtime.mkdir()
    # PX4's startup launcher concatenates its script path into a shell command.
    # Stage exact reviewed bytes under the owned space/apostrophe-free runtime.
    startup = runtime / "startup.sh"
    startup.write_bytes((HERE / "startup.sh").read_bytes())
    instance = secrets.randbelow(50000) + 10000
    while instance in used_instances:
        instance = secrets.randbelow(50000) + 10000
    used_instances.add(instance)
    identities = []
    for _ in range(3):
        identity = secrets.randbits(64)
        while identity == 0 or identity in used_ids:
            identity = secrets.randbits(64)
        used_ids.add(identity)
        identities.append(str(identity))
    command = [str(output / "bin/px4"), "-i", str(instance), "-w", str(runtime), "-s", str(startup)]
    record = {"schema": "research-c1c-run/v1", "command": command, "instance": instance, "runtime": str(runtime),
              "producer_lifetime_id": identities[0], "observer_lifetime_id": identities[1], "clock_epoch": identities[2],
              "admission": "not_instantiated", "research_network": "not_instantiated", "status": "incomplete",
              "startup_sha256": base.digest(startup),
              "start_monotonic_ns": time.monotonic_ns()}
    base.save(runtime / "run.json", record)
    env = {key: value for key, value in os.environ.items()
           if not key.startswith(("PX4_", "SIM_", "GZ_", "C1C_")) and key not in {"replay", "replay_mode"}}
    env.update(C1C_PRODUCER_ID=identities[0], C1C_OBSERVER_ID=identities[1], C1C_CLOCK_EPOCH=identities[2],
               C1C_CAPTURE_WINDOW_US=str(WINDOW_US))
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
            evidence = runtime / "capture.jsonl"
            record["capture_sha256"] = base.digest(evidence) if evidence.is_file() else None
            base.save(runtime / "run.json", record)
    return record


def check_bound_inputs(area: Path, manifest: dict) -> None:
    require(base.digest(area / "source_hashes.json") == manifest["source_registry_sha256"], "Source registry changed")
    before = json.loads((area / "source_hashes.json").read_text())
    require(before == base.source_hashes(), "Source inputs changed since capture build")
    require(base.digest(area / "build/bin/px4") == manifest["executable_sha256"], "Executable changed")
    require(check_config(area / "build") == manifest["configuration_audit"], "Configuration evidence changed")
    require(base.digest(HERE / "commander_receipts.patch") == manifest["overlay"]["patch_sha256"], "Overlay patch changed")
    for relative, hashes in manifest["overlay"]["files"].items():
        require(relative in BASE_HASHES and base.digest(ROOT / relative) == hashes["base_sha256"] == BASE_HASHES[relative],
                "Original Commander drift")
        normalized = hashlib.sha256((ROOT / relative).read_bytes().replace(b"\r\n", b"\n")).hexdigest()
        require(normalized == hashes["normalized_sha256"], "False normalized base hash")
        require(base.digest(area / "source" / relative) == hashes["derived_sha256"], "Derived Commander drift")
    require(set(manifest["overlay"]["files"]) == set(BASE_HASHES), "Incomplete overlay registry")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run", action="store_true", help="also capture and independently check two disarmed PX4 runs")
    args = parser.parse_args()
    require(platform.system() == "Linux", "Run in the existing native Ubuntu WSL environment")
    area = Path(tempfile.mkdtemp(prefix="c1c_px4_", dir="/var/tmp"))
    print(f"C1C retained artifacts: {area}", flush=True)
    base.save(area / "manifest.json", {"schema": "research-c1c-capture/v1", "status": "incomplete",
                                       "stage": "repository_inventory"})
    manifest = {"schema": "research-c1c-capture/v1", "status": "incomplete", "source": str(ROOT),
                "scope": "producer_capture_only_no_admission_no_output", "runs": [],
                "px4_revision": base.git("rev-parse", "HEAD"),
                "entry_status": base.git("status", "--short", "--ignore-submodules=dirty"),
                "entry_status_scope": "parent changes/gitlink revisions; Gazebo checked separately",
                "submodules": base.git("submodule", "status", "--recursive"), "kernel": platform.release(),
                "python": platform.python_version(), "window_us": WINDOW_US, "build_provenance": "fresh_area"}
    base.save(area / "manifest.json", manifest)
    before = base.source_hashes()
    base.save(area / "source_hashes.json", before)
    manifest["source_registry_sha256"] = base.digest(area / "source_hashes.json")
    manifest["gazebo_before"] = base.gazebo_identity()
    completed = False
    candidate_status = "incomplete"
    try:
        output = build(area, manifest)
        check_bound_inputs(area, manifest)
        if args.run:
            from check_capture import check_run
            used_instances: set[int] = set()
            used_ids: set[int] = set()
            for index in (1, 2):
                record = capture_run(area, output, index, used_instances, used_ids)
                manifest["runs"].append(record)
                base.save(area / "manifest.json", manifest)
                require(json.loads((Path(record["runtime"]) / "run.json").read_text()) == record,
                        "Run record differs from runner-owned launch evidence")
                base.save(area / f"checked-run-{index}.json", check_run(Path(record["runtime"])))
            candidate_status = "two_runs_checked"
        else:
            candidate_status = "build_only"
        check_bound_inputs(area, manifest)
        completed = True
    except (RuntimeError, ValueError, OSError, subprocess.SubprocessError, KeyError, TypeError) as error:
        manifest["status"] = "fail"
        manifest["failure"] = str(error)
    except BaseException:
        manifest["status"] = "fail"
        manifest["failure"] = "Interrupted before final capture acceptance"
        raise
    finally:
        after = base.source_hashes()
        manifest["changed_source_inputs"] = [name for name in sorted(before.keys() | after.keys())
                                              if before.get(name) != after.get(name)]
        manifest["gazebo_after"] = base.gazebo_identity()
        if manifest["changed_source_inputs"] or manifest["gazebo_before"] != manifest["gazebo_after"]:
            manifest["status"] = "fail"
            manifest["failure"] = "Source or Gazebo identity changed; evidence not stable"
        elif completed:
            manifest["status"] = candidate_status
        base.save(area / "manifest.json", manifest)
    print(json.dumps({"artifacts": str(area), "status": manifest["status"], "failure": manifest.get("failure")}), flush=True)
    return 1 if manifest["status"] == "fail" else 0


if __name__ == "__main__":
    raise SystemExit(main())
