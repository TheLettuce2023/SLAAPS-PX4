#!/usr/bin/env python3
"""Export a sealed native source snapshot and build it in an isolated PX4 Docker volume."""

import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import shutil
import subprocess
import tarfile

TARGET = "px4_fmu-v6x_slaaps"


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify(source, record):
    for name, entry in record["inputs"].items():
        path = source / name
        if entry["kind"] == "file":
            assert path.is_file() and digest(path) == entry["sha256"], name
        else:
            assert path.is_symlink() and os.readlink(path) == entry["target"], name


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("operation", choices=("export", "prepare", "refresh", "build"))
    parser.add_argument("--native", type=Path)
    parser.add_argument("--output", type=Path, default=Path("/output"))
    parser.add_argument("--workspace", type=Path)
    parser.add_argument("--refresh-file", action="append", default=[])
    parser.add_argument("--remove-file", action="append", default=[])
    args = parser.parse_args()
    out = args.output.resolve()
    if args.operation == "export":
        native = args.native.resolve()
        receipt = json.loads((native / "result.json").read_text())
        assert digest(native / "source-manifest.json") == receipt["source_manifest_sha256"]
        record = json.loads((native / "source-manifest.json").read_text())
        verify(native / "source", record)
        out.mkdir(parents=True, exist_ok=False)
        shutil.copy2(native / "source-manifest.json", out / "source-manifest.json")
        shutil.copy2(__file__, out / "build_board_snapshot.py")
        with tarfile.open(out / "source.tar", "w") as archive:
            for name in record["inputs"]:
                archive.add(native / "source" / name, arcname=name, recursive=False)
            for name in record["repositories"]:
                archive.add(native / "source" / name / ".git", arcname=str(Path(name) / ".git"))
        (out / "export.json").write_text(json.dumps(dict(native=str(native),
            parent_receipt_sha256=digest(native / "result.json"), archive_sha256=digest(out / "source.tar"),
            source_manifest_sha256=digest(out / "source-manifest.json")), indent=2) + "\n")
        print(f"Exported {out}", flush=True)
        return
    source = Path("/work/source")
    record = json.loads((out / "source-manifest.json").read_text())
    export = json.loads((out / "export.json").read_text())
    assert digest(out / "source-manifest.json") == export["source_manifest_sha256"]
    if args.operation == "prepare":
        assert not source.exists(), "Use a fresh build volume"
        assert digest(out / "source.tar") == export["archive_sha256"]
        source.mkdir()
        with tarfile.open(out / "source.tar") as archive:
            archive.extractall(source, filter="tar")
        verify(source, record)
        subprocess.run(["chown", "-R", "1000:1000", str(source)], check=True)
        return
    manifest_path = out / "board-source-manifest.json"
    if manifest_path.exists():
        record = json.loads(manifest_path.read_text())
    if args.operation == "refresh":
        assert not (out / "board-result.json").exists()
        assert args.workspace and (args.refresh_file or args.remove_file)
        verify(source, record)
        workspace = args.workspace.resolve()
        for name in args.remove_file:
            path = (workspace / name).resolve()
            target = (source / name).resolve()
            assert path.is_relative_to(workspace) and not path.exists(), name
            assert target.is_relative_to(source) and target.is_file(), name
            previous = record["inputs"][name]
            assert previous["kind"] == "file" and digest(target) == previous["sha256"], name
            record.setdefault("board_revisions", []).append(dict(path=name, before=digest(target), after=None))
            target.unlink()
            del record["inputs"][name]
        for name in args.refresh_file:
            path = (workspace / name).resolve()
            assert path.is_relative_to(workspace) and path.is_file()
            target = source / name
            previous = record["inputs"].get(name)
            assert previous is None or previous["kind"] == "file"
            assert previous is not None or not target.exists()
            record.setdefault("board_revisions", []).append(dict(path=name, before=digest(target) if previous else None, after=digest(path)))
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, target)
            record["inputs"][name] = dict(kind="file", sha256=digest(target), mode="100644")
        manifest_path.write_text(json.dumps(record, indent=2) + "\n")
        return
    if not manifest_path.exists():
        shutil.copy2(out / "source-manifest.json", manifest_path)
    verify(source, record)
    assert not (out / "board-result.json").exists(), "Successful board evidence is immutable"
    # Offline dependency source from the retained, read-only working-baseline
    # build. No compiled donor object participates in this fresh target.
    donor = Path("/donor/px4-stabilized-observer/build/px4_fmu-v6x_default/src/modules/uxrce_dds_client/src/libmicroxrceddsclient_project-build/microcdr/src/microcdr")
    assert donor.is_dir()
    env = dict(os.environ, CCACHE_DISABLE="1", GIT_CONFIG_COUNT="2",
               GIT_CONFIG_KEY_1="url.file://" + str(donor) + ".insteadOf",
               GIT_CONFIG_VALUE_1="https://github.com/eProsima/Micro-CDR.git")
    expected_tag = subprocess.check_output(
        ["git", "describe", "--exclude", "ext/*", "--tags", "--match", "v[0-9]*"],
        cwd=source, env=env, text=True).strip()
    assert expected_tag.startswith("v") and expected_tag != "v0.0.0", "Resolve Git alternates before building"
    build = source / "build" / TARGET
    if (build / "CMakeCache.txt").exists():
        # Reconfigure a derived build so a previously cached fallback tag cannot
        # survive repair of the container's Git common-directory mount.
        with (out / "reconfigure.log").open("w") as log:
            subprocess.run(["cmake", "-S", str(source), "-B", str(build), "-G", "Ninja", "-DCONFIG=" + TARGET],
                           cwd=source, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
    if (out / "build.log").exists():
        attempt = 1
        while (out / f"build-attempt-{attempt}.log").exists():
            attempt += 1
        (out / "build.log").rename(out / f"build-attempt-{attempt}.log")
    with (out / "build.log").open("w") as log:
        result = subprocess.run(["make", TARGET, "-j6"], cwd=source, env=env, stdout=log, stderr=subprocess.STDOUT)
    (out / "exit-code.txt").write_text(str(result.returncode) + "\n")
    if result.returncode:
        print((out / "build.log").read_text()[-10000:], flush=True)
        raise RuntimeError("Board build failed; source and log retained")
    verify(source, record)
    build = source / "build" / TARGET
    version = build / "src/lib/version/build_git_version.h"
    actual_tag = re.search(r'#define PX4_GIT_TAG_STR\s+"([^"]+)"', version.read_text())
    assert actual_tag and actual_tag[1] == expected_tag, "Embedded version differs from resolved source tag"
    shutil.copy2(version, out / "build_git_version.h")
    symbols = subprocess.check_output(["arm-none-eabi-nm", "-C", str(build / (TARGET + ".elf"))], text=True)
    for symbol in ("slaaps_main", "MixingOutput::reportSlaapsInventory", "Slaaps::updateRouting"):
        assert symbol in symbols, symbol
    assert "slaaps_routing_test_main" not in symbols
    artifacts = out / "artifacts"
    artifacts.mkdir(exist_ok=True)
    identity = record["repositories"]["."][:10] + "_dirty_" + out.name
    files = {}
    for suffix in (".px4", ".bin", ".elf"):
        destination = artifacts / (TARGET + "_" + identity + suffix)
        assert not destination.exists()
        shutil.copy2(build / (TARGET + suffix), destination)
        files[str(destination.relative_to(out))] = dict(sha256=digest(destination), size=destination.stat().st_size)
    (out / "symbols.txt").write_text(symbols)
    shutil.copy2(build / "compile_commands.json", out / "compile_commands.json")
    kernel = source / "platforms/nuttx/NuttX/nuttx"
    configuration = {}
    for label, original, name in (("header", kernel / "include/nuttx/config.h", "nuttx_config.h"),
                                  ("config", kernel / ".config", "nuttx_dotconfig")):
        assert original.is_file(), original
        shutil.copy2(original, out / name)
        configuration[label] = dict(path=name, sha256=digest(out / name))
    (out / "board-result.json").write_text(json.dumps(dict(passed=True, target=TARGET,
        source_manifest="board-source-manifest.json", source_manifest_sha256=digest(manifest_path),
        builder_sha256=digest(Path(__file__)), files=files,
        nuttx_configuration=configuration,
        expected_git_tag=expected_tag, version_header_sha256=digest(out / "build_git_version.h"),
        hardware_operated=False, board_boot_verified=False, working_baseline_port=False), indent=2) + "\n")
    print("Board link PASS; no flashing or boot verification", flush=True)


if __name__ == "__main__":
    main()
