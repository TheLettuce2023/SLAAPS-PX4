#!/usr/bin/env python3
"""Build and audit the isolated C1E native diagnostic profile; never run PX4."""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import platform
import re
import shlex
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]


def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError("Cannot load reviewed local helper: " + str(path))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


closed = load_module("c1d_build_helpers", HERE.parent / "c1d_closed/run_closed_owner.py")
base = closed.base
w2 = load_module("c1e_w2_generator", HERE.parents[1] / "diagnostic_wire/test_generated_mavlink.py")

BASE_HASHES = {
    "src/modules/commander/Commander.cpp": "bd270eddc3482d1d543f619bb40d799f0b0987d7e2c0a4d59804dfcd7a84432b",
    "src/modules/commander/Commander.hpp": "83aefea00ed03a5d71950598a5ca3b2a843705ad678681b7c98578101e3203b0",
    "src/modules/commander/CMakeLists.txt": "d5cdab2484fd13fa9a20d4b4dbccde118adbdc9db38eeeea74771d8e3fe8308f",
}
BUILTINS = (base.BUILTINS - {"research_probe"}) | {"research_native_ingress"}
CORE_SOURCES = {"RequestGuard.cpp", "DiagnosticGuard.cpp", "DiagnosticAdapter.cpp", "FrameMath.cpp",
                "TrustedTimeMap.cpp", "WireAdapter.cpp", "NativeIngressDiagnosticOwner.cpp"}
NATIVE_FLAGS = {"RESEARCH_SITL_DIAGNOSTIC_NATIVE", "RESEARCH_C1E_NEGATIVE_OWNER_TARGET"}
PARAMETER_METADATA = (
    "src/modules/navigator/geofence_params.yaml", "src/modules/sensors/sensor_params.yaml",
    "src/modules/sensors/vehicle_gps_position/module.yaml", "src/modules/rc_update/params.yaml",
    "src/modules/logger/module.yaml", "src/modules/navigator/rtl_params.yaml",
)
FORBIDDEN_FLAGS = {"ENABLE_LOCKSTEP_SCHEDULER", "CONFIG_MUORB_APPS_SYNC_TIMESTAMP", "BOARD_LOCKSTEP",
                   "PX4_SIMULATOR_TIMESTAMP_PROXY", "RESEARCH_SITL_DIAGNOSTIC_HOST_TEST",
                   "RESEARCH_SITL_DIAGNOSTIC_OFFLINE", "RESEARCH_SITL_DIAGNOSTIC_NATIVE_CONTRACT_TEST",
                   "RESEARCH_SITL_DIAGNOSTIC_CAPABILITY", "RESEARCH_C1D_CLOSED_OWNER_TARGET",
                   "RESEARCH_C1E_NEGATIVE_CONTRACT_TEST", "RESEARCH_C1C_CAPTURE"}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def overlay_view(area: Path) -> tuple[Path, dict]:
    """Copy exactly three reviewed Commander files; symlink other PX4 inputs."""
    view = area / "source"
    view.mkdir()
    private = {"src", "src/modules", "src/modules/commander"}

    def populate(source: Path, destination: Path, prefix: str = "") -> None:
        for item in source.iterdir():
            relative = f"{prefix}/{item.name}" if prefix else item.name
            target = destination / item.name
            if relative in {".vscode", "build"}:
                target.mkdir()
            elif relative in private:
                target.mkdir()
                populate(item, target, relative)
            elif relative in BASE_HASHES:
                require(item.is_file() and not item.is_symlink(), "Commander overlay input is not regular")
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
        records[relative] = {"base_sha256": base.digest(ROOT / relative),
                             "normalized_sha256": base.digest(target)}

    patch = HERE / "commander_negative_ingress.patch"
    content = patch.read_text()
    old = re.findall(r"^--- a/([^\t\n]+)(?:\t[^\n]*)?$", content, re.MULTILINE)
    new = re.findall(r"^\+\+\+ b/([^\t\n]+)(?:\t[^\n]*)?$", content, re.MULTILINE)
    require(len(old) == len(new) == 3 and set(old) == set(new) == set(BASE_HASHES),
            "Unexpected private patch targets")
    require(sum(line.startswith("--- ") for line in content.splitlines()) == 3
            and sum(line.startswith("+++ ") for line in content.splitlines()) == 3,
            "Additional private patch target")
    command = ["patch", "--batch", "--forward", "--fuzz=0", "--no-backup-if-mismatch", "-p1", "-i", str(patch)]
    for dry, label in ((True, "patch-check"), (False, "patch-apply")):
        log_path = area / (label + ".log")
        with log_path.open("w") as log:
            result = subprocess.run(command + (["--dry-run"] if dry else []), cwd=view,
                                    stdout=log, stderr=subprocess.STDOUT, timeout=30, check=False)
        require(result.returncode == 0, "Private Commander patch failed: " + label)
        require(not re.search(r"\b(?:offset|fuzz)\b", log_path.read_text(), re.IGNORECASE),
                "Private Commander patch required offset/fuzz: " + label)
    for relative, hashes in records.items():
        hashes["derived_sha256"] = base.digest(view / relative)
        require(hashes["derived_sha256"] != hashes["normalized_sha256"], "Declared overlay is unchanged")
        require(base.digest(ROOT / relative) == hashes["base_sha256"], "Original Commander source changed")
    return view, {"patch_sha256": base.digest(patch), "files": records}


def generate_dialect(area: Path) -> tuple[Path, dict]:
    """Run the W2 pinned generator in this build area, retaining both bindings."""
    require("MAV_IGNORE_CRC" not in os.environ, "CRC bypass environment is forbidden")
    w2.pinned_clean(w2.MAVLINK, w2.PIN_MAVLINK)
    w2.pinned_clean(w2.PYMAVLINK, w2.PIN_PYMAVLINK)
    w2.validate_xml()
    generated = area / "generated"
    generated.mkdir()
    c_output, python_output = w2.generate(generated)
    w2.check_header(c_output)
    files = {str(path.relative_to(generated)): base.digest(path)
             for path in sorted(generated.rglob("*")) if path.is_file()}
    require(files and python_output.is_file(), "Generated bindings are incomplete")
    record = {"mavlink_pin": w2.PIN_MAVLINK, "pymavlink_pin": w2.PIN_PYMAVLINK,
              "xml_sha256": base.digest(w2.XML), "schema_sha256": base.digest(w2.SCHEMA),
              "mavgen_sha256": base.digest(w2.MAVGEN), "harness_sha256": base.digest(Path(w2.__file__)),
              "c_output": str(c_output), "python_output": str(python_output), "files": files}
    base.save(area / "generated_files.json", record)
    record["registry_sha256"] = base.digest(area / "generated_files.json")
    return c_output, record


def check_config(output: Path, view: Path) -> dict:
    enabled = {line.split("=", 1)[0] for line in (output / "boardconfig").read_text().splitlines()
               if line.endswith("=y")}
    modules = {name for name in enabled if name.startswith("CONFIG_MODULES_")}
    drivers = {name for name in enabled if name.startswith("CONFIG_DRIVERS_")}
    require(modules == {"CONFIG_MODULES_COMMANDER"} and not drivers, "Unexpected selected modules/drivers")
    require("CONFIG_BOARD_NOLOCKSTEP" in enabled and "CONFIG_MUORB_APPS_SYNC_TIMESTAMP" not in enabled,
            "Private native build must not use lockstep/proxy clocks")
    ninja = (output / "build.ninja").read_text()
    require(all(str(view / relative) in ninja for relative in PARAMETER_METADATA),
            "C1E Commander-only profile is missing required parameter metadata")
    parameter_header = (output / "src/lib/parameters/px4_parameters.hpp").read_text()
    require("GF_ACTION," in parameter_header and "SENS_IMU_MODE," in parameter_header,
            "C1E Commander parameter identifiers were not generated")

    commands = json.loads((output / "compile_commands.json").read_text())
    core = ROOT / "src/lib/research_acceleration"
    expected = {(core / name).resolve() for name in CORE_SOURCES}
    bridge = HERE / "bridge"
    expected |= {(bridge / name).resolve() for name in
                 ("NativeIngressSourceBridge.cpp", "NativeIngressWorker.cpp", "NativeIngressJson.cpp")}
    expected.add((HERE / "px4_external/src/research_native_ingress/research_native_ingress.cpp").resolve())
    expected.add((ROOT / "Tools/research_sitl/diagnostic_wire/native_parser/DiagnosticSignedMavlink.cpp").resolve())
    expected.add((HERE.parent / "c1d_closed/bridge/NativeClosedJson.cpp").resolve())
    require(all(path.is_file() for path in expected), "Missing declared C1E native source")
    commander = view / "src/modules/commander"
    marked = []
    for item in commands:
        tokens = shlex.split(item["command"])
        defines = [token[2:] for token in tokens if token.startswith("-D")]
        require(not any(token.split("=", 1)[0] in FORBIDDEN_FLAGS for token in defines),
                "Forbidden diagnostic/clock compile definition")
        flags = [token for token in defines if token.split("=", 1)[0] in NATIVE_FLAGS]
        lexical = Path(os.path.abspath(item["file"]))
        source = lexical.resolve()
        is_commander = lexical.is_relative_to(commander)
        if is_commander:
            relative = lexical.relative_to(commander)
            require(source in {lexical, (ROOT / "src/modules/commander" / relative).resolve()},
                    "Commander source escaped private overlay")
        objects = [tokens[index + 1] for index, token in enumerate(tokens[:-1]) if token == "-o"]
        require(len(objects) == 1, "Missing or ambiguous compile output")
        commander_unit = "CMakeFiles/modules__commander.dir/" in objects[0]
        require(not commander_unit or is_commander, "Commander compiled an unexpected source")
        participates = source in expected or commander_unit
        require(bool(flags) == participates, "C1E marker/source mismatch: " + str(source))
        if participates:
            require(sorted(flags) == sorted(name + "=1" for name in NATIVE_FLAGS),
                    "C1E native opt-in must be exact and unique")
            standards = [token for token in tokens if token.startswith("-std=")]
            require(standards and standards[-1] in {"-std=c++17", "-std=gnu++17"},
                    "C1E native class layout requires C++17")
            require("-fstack-usage" in tokens, "Missing native compiler stack-use evidence")
            require("platforms/ros2/include" not in item["command"], "Host include shim entered native build")
            marked.append(source)
    require(all(marked.count(source) == 1 for source in expected), "Missing or duplicate C1E native source unit")
    require(marked.count(commander / "Commander.cpp") == 1, "Commander not compiled from private copy")
    apps = output / "platforms/posix/apps.cpp"
    builtins = set(re.findall(r'apps\["([a-zA-Z0-9_]+)"\]', apps.read_text()))
    require(builtins == BUILTINS, "Unexpected linked builtins: " + str(sorted(builtins)))
    return {"enabled_modules": sorted(modules), "enabled_drivers": sorted(drivers),
            "builtins": sorted(builtins), "native_units": len(marked), "compile_units": len(commands),
            "boardconfig_sha256": base.digest(output / "boardconfig"),
            "parameter_metadata": list(PARAMETER_METADATA),
            "parameters_header_sha256": base.digest(output / "src/lib/parameters/px4_parameters.hpp"),
            "apps_sha256": base.digest(apps), "compile_commands_sha256": base.digest(output / "compile_commands.json")}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--area", type=Path, help="new, empty directory directly under /var/tmp")
    args = parser.parse_args()
    require(platform.system() == "Linux" and platform.machine() == "x86_64",
            "C1E native build requires x86_64 Linux/Ubuntu WSL")
    if args.area:
        area = args.area.resolve()
        require(area.parent == Path("/var/tmp") and area.name.startswith("c1e_px4_"),
                "Build area must be a new /var/tmp/c1e_px4_* directory")
        area.mkdir(mode=0o700, exist_ok=False)
    else:
        area = Path(tempfile.mkdtemp(prefix="c1e_px4_", dir="/var/tmp"))
    before = closed.source_hashes()
    manifest = {"schema": "research-c1e-native-build/v1", "status": "incomplete",
                "scope": "compile_only_negative_native_no_runtime", "source": str(ROOT),
                "px4_revision": base.git("rev-parse", "HEAD"), "entry_status": closed.entry_status(),
                "entry_status_scope": "tracked parent changes only; all build inputs include untracked files",
                "submodules": base.git("submodule", "status", "--recursive"),
                "kernel": platform.release(), "python": platform.python_version(),
                "gazebo_before": base.gazebo_identity(), "runs": []}
    base.save(area / "source_hashes.json", before)
    manifest["source_registry_sha256"] = base.digest(area / "source_hashes.json")
    base.save(area / "manifest.json", manifest)
    print(f"C1E compile-only artifacts: {area}", flush=True)
    candidate = "fail"
    try:
        generated_c, generator = generate_dialect(area)
        manifest["generator"] = generator
        view, overlay = overlay_view(area)
        manifest["overlay"] = overlay
        output = area / "build"
        source = view / HERE.relative_to(ROOT)
        env = dict(os.environ, CCACHE_DIR=str(area / "ccache"), CCACHE_DISABLE="1",
                   PYTHONDONTWRITEBYTECODE="1", GIT_CONFIG_COUNT="1", GIT_CONFIG_KEY_0="core.autocrlf",
                   GIT_CONFIG_VALUE_0="true", GIT_SUBMODULES_ARE_EVIL="1", GIT_TERMINAL_PROMPT="0",
                   GIT_ALLOW_PROTOCOL="file")
        configure = ["cmake", "-S", str(view), "-B", str(output), "-G", "Ninja",
                     "-DCONFIG=px4_sitl_c1e-default", "-DVENDOR=px4", "-DMODEL=sitl", "-DLABEL=c1e-default",
                     f"-DPX4_BOARD_DIR={view}/boards/px4/sitl",
                     f"-DPX4_CONFIG_FILE={source}/c1e-default.px4board",
                     f"-DEXTERNAL_MODULES_LOCATION={source}/px4_external",
                     "-DRESEARCH_C1E_NEGATIVE_OWNER=ON", f"-DC1E_GENERATED_MAVLINK_DIR={generated_c}",
                     "-DCMAKE_BUILD_TYPE=Debug", "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"]
        compile_command = ["cmake", "--build", str(output), "--target", "px4", "-j", "4"]
        manifest["build"] = {"view": str(view), "directory": str(output), "cache": str(area / "ccache"),
                             "configure_command": configure, "build_command": compile_command}
        for label, command in (("configure", configure), ("build", compile_command)):
            base.save(area / "manifest.json", manifest)
            print(f"C1E {label}: {area / (label + '.log')}", flush=True)
            with (area / (label + ".log")).open("w") as log:
                exit_code = base.bounded_command(command, view, env, log)
            manifest["build"][label + "_exit"] = exit_code
            require(exit_code == 0, f"{label} failed ({exit_code}); retained log")
        manifest["configuration_audit"] = check_config(output, view)
        manifest["executable_sha256"] = base.digest(output / "bin/px4")
        stack = {str(path.relative_to(output)): base.digest(path) for path in sorted(output.rglob("*.su"))}
        require(stack, "No native stack-use artifacts")
        base.save(area / "stack_files.json", stack)
        manifest["stack_registry_sha256"] = base.digest(area / "stack_files.json")
        probe_output = area / "native_profile_probes.json"
        probe_command = ["python3", str(source / "check_native_profile.py"),
                         "--build-dir", str(output), "--output", str(probe_output)]
        manifest["native_profile_probe_command"] = probe_command
        with (area / "native_profile_probes.log").open("w") as log:
            probe_exit = base.bounded_command(probe_command, view, env, log)
        manifest["native_profile_probe_exit"] = probe_exit
        require(probe_exit == 0 and json.loads(probe_output.read_text())["pass"],
                "Actual native profile probes failed; retained probe artifacts")
        manifest["native_profile_probes_sha256"] = base.digest(probe_output)
        require(base.digest(area / "generated_files.json") == generator["registry_sha256"],
                "Generated binding registry changed")
        candidate = "build_only_pending_native_review"
    except (RuntimeError, ValueError, OSError, subprocess.SubprocessError, KeyError, TypeError) as error:
        manifest["failure"] = str(error)
    except BaseException:
        manifest["failure"] = "Interrupted before build completion"
        raise
    finally:
        after = closed.source_hashes()
        manifest["changed_source_inputs"] = [name for name in sorted(before.keys() | after.keys())
                                             if before.get(name) != after.get(name)]
        manifest["gazebo_after"] = base.gazebo_identity()
        if manifest["changed_source_inputs"] or manifest["gazebo_after"] != manifest["gazebo_before"]:
            candidate = "fail"
            manifest["source_stability_failure"] = "Source or separate Gazebo identity changed during build"
            manifest.setdefault("failure", manifest["source_stability_failure"])
        manifest["status"] = candidate
        base.save(area / "manifest.json", manifest)
    print(json.dumps({"artifacts": str(area), "status": manifest["status"],
                      "failure": manifest.get("failure")}), flush=True)
    return 0 if manifest["status"] == "build_only_pending_native_review" else 1


if __name__ == "__main__":
    raise SystemExit(main())
