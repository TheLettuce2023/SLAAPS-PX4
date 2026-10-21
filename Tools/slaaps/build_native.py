#!/usr/bin/env python3
"""Build a fresh SLAAPS native target from the current checkout, without a plant."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "Tools/research_sitl"))
from build_stabilized_manual_checkout import stage_repository


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(out, label, command, env=None):
    print(label, flush=True)
    with (out / (label + ".log")).open("w") as stream:
        result = subprocess.run([str(arg) for arg in command], stdout=stream, stderr=subprocess.STDOUT, env=env)
    if result.returncode:
        print((out / (label + ".log")).read_text()[-16000:], flush=True)
        raise RuntimeError(f"{label} failed ({result.returncode})")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--resume", type=Path, help="Resume an unsealed failed build in its retained source snapshot")
    parser.add_argument("--refresh-file", action="append", default=[],
                        help="Explicitly replace a source in --resume or --test-from and record its previous digest")
    parser.add_argument("--test-from", type=Path,
                        help="Fork a sealed source snapshot into a wall-clock supervisor test build")
    parser.add_argument("--wall-clock-test", action="store_true",
                        help="Build the current checkout directly as the explicit native test target")
    args = parser.parse_args()
    assert not (args.wall_clock_test and (args.resume or args.test_from))
    assert not (args.resume and args.test_from)
    if args.test_from:
        original = args.test_from.resolve()
        sealed = json.loads((original / "result.json").read_text())
        assert digest(original / "source-manifest.json") == sealed["source_manifest_sha256"]
        record = json.loads((original / "source-manifest.json").read_text())
        for name, metadata in record["inputs"].items():
            if metadata["kind"] == "file":
                assert digest(original / "source" / name) == metadata["sha256"], name
        out = Path(tempfile.mkdtemp(prefix="slaaps-native-", dir="/var/tmp"))
        print(f"Native test build: {out}", flush=True)
        shutil.copytree(original / "source", out / "source", symlinks=True)
        test_overlay = ("boards/px4/sitl/slaaps-test.px4board", "src/modules/slaaps/Kconfig",
                        "src/modules/slaaps/CMakeLists.txt", "src/modules/slaaps/SlaapsRoutingTest.cpp",
                        "Tools/slaaps/configuration/VehicleConfigurationTest.cpp")
        for name in dict.fromkeys((*test_overlay, *args.refresh_file)):
            path = (ROOT / name).resolve()
            assert path.is_relative_to(ROOT) and path.is_file(), name
            name = path.relative_to(ROOT).as_posix()
            record.setdefault("test_overlay", []).append(dict(path=name,
                before=record["inputs"].get(name, {}).get("sha256"), after=digest(ROOT / name)))
            (out / "source" / name).parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / name, out / "source" / name)
            record["inputs"][name] = dict(kind="file", sha256=digest(ROOT / name), mode="100644")
        record.update(scope="sealed_source_wall_clock_supervisor_test", parent_build=str(original),
                      parent_receipt_sha256=digest(original / "result.json"), config="px4_sitl_slaaps-test")
        (out / "source-manifest.json").write_text(json.dumps(record, indent=2) + "\n")
        shutil.copy2(__file__, out / "build_native.py")
    elif args.resume:
        out = args.resume.resolve()
        assert out.parent == Path("/var/tmp") and out.name.startswith("slaaps-native-")
        assert not (out / "result.json").exists(), "Sealed native builds are immutable"
        record = json.loads((out / "source-manifest.json").read_text())
        for name in args.refresh_file:
            path = (ROOT / name).resolve()
            assert path.is_relative_to(ROOT) and path.is_file()
            assert name in record["inputs"] and record["inputs"][name]["kind"] == "file"
            target = out / "source" / name
            assert digest(target) == record["inputs"][name]["sha256"]
            record.setdefault("revisions", []).append(dict(path=name, before=digest(target), after=digest(path)))
            shutil.copyfile(path, target)
            record["inputs"][name]["sha256"] = digest(target)
        (out / "source-manifest.json").write_text(json.dumps(record, indent=2) + "\n")
    else:
        assert not args.refresh_file, "Source refresh requires an unsealed --resume build"
        out = Path(tempfile.mkdtemp(prefix="slaaps-native-", dir="/var/tmp"))
        print(f"Native build: {out}", flush=True)
        inputs, repositories = {}, {}
        source = out / "source"
        stage_repository(ROOT, source, out, inputs, repositories)
        # Required working-tree sources include deliberately untracked prior
        # research libraries. Do not silently replace them with committed files.
        overlays = (
            "src/lib/slaaps", "src/modules/slaaps", "Tools/slaaps/configuration",
            "src/lib/research_stabilized_manual", "src/lib/research_actuator_command",
            "src/lib/mixer_module", "src/modules/commander/HealthAndArmingChecks",
            "msg", "boards/px4/sitl", "boards/px4/fmu-v6x",
            "ROMFS/px4fmu_common/init.d",
        )
        for directory in overlays:
            for path in sorted((ROOT / directory).rglob("*")):
                if not path.is_file() or "__pycache__" in path.parts:
                    continue
                if path.is_symlink():
                    raise RuntimeError(f"Unexpected overlay symlink: {path}")
                name = path.relative_to(ROOT)
                target = source / name
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(path, target)
                inputs[name.as_posix()] = dict(kind="file", sha256=digest(target), mode="100644")
        record = dict(scope="current_checkout_native_slaaps_no_plant", inputs=inputs, repositories=repositories)
        if args.wall_clock_test:
            record["config"] = "px4_sitl_slaaps-test"
        (out / "source-manifest.json").write_text(json.dumps(record, indent=2) + "\n")
        shutil.copy2(__file__, out / "build_native.py")
    source, build = out / "source", out / "build"
    for name, metadata in record["inputs"].items():
        if metadata["kind"] == "file":
            assert digest(source / name) == metadata["sha256"], name
        else:
            assert os.readlink(source / name) == metadata["target"], name
    env = dict(os.environ, GIT_SUBMODULES_ARE_EVIL="1")
    if not (build / "build.ninja").exists():
        run(out, "configure", ["cmake", "-S", source, "-B", build, "-G", "Ninja",
                              "-DCONFIG=" + record.get("config", "px4_sitl_slaaps"),
                              "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"], env)
    run(out, "mavlink", ["cmake", "--build", build, "--target", "mavlink_c_generate", "-j", "6"], env)
    run(out, "compile", ["cmake", "--build", build, "-j", "6"], env)
    binary = build / "bin/px4"
    assert binary.is_file()
    symbols = subprocess.check_output(["nm", "-C", str(binary)], text=True)
    for required in ("slaaps_main", "Slaaps::Run()", "SlaapsChecks::checkAndReport", "slaaps::Px4ConfigurationSource::capture"):
        assert required in symbols, required
    if record.get("config") == "px4_sitl_slaaps-test":
        assert "slaaps_control_test_main" in symbols, "Native control suite must be linked"
    for name, metadata in record["inputs"].items():
        if metadata["kind"] == "file":
            assert digest(source / name) == metadata["sha256"], name
    receipt = dict(passed=True, scope="fresh_native_link_only", binary_sha256=digest(binary),
                   source_manifest_sha256=digest(out / "source-manifest.json"),
                   runtime_verified=False, board_firmware=False, hardware_operated=False)
    edf_object = build / "src/lib/slaaps/CMakeFiles/slaaps_configuration.dir/EdfOwner.cpp.o"
    if (source / "src/lib/slaaps/EdfOwner.cpp").exists():
        assert edf_object.is_file(), "Common EDF owner must compile with native PX4 flags"
        receipt["edf_owner_object_sha256"] = digest(edf_object)
        receipt["edf_owner_supervisor_lifecycle_verified"] = False # Link evidence alone cannot establish runtime ownership.
    (out / "result.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(f"NATIVE LINK PASS {out}", flush=True)


if __name__ == "__main__":
    main()
