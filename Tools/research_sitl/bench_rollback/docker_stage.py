"""Prepare, guard or export the private bench build. Never operate hardware."""
import base64
import datetime
import hashlib
import json
import os
from pathlib import Path
import runpy
import shutil
import subprocess
import sys
import tarfile
import zlib

SOURCE = Path(os.environ["PX4_DIAGNOSTIC_SOURCE"])
STAGE = Path("/output")
BASE = "e8ea98b404baf50623a519ac67ec1c03aa240859"
TARGET = "px4_fmu-v6x_default"


def digest(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def git(*args, cwd=SOURCE):
    return subprocess.check_output(["git", "-C", str(cwd), *args], text=True).rstrip("\n")


def guard():
    manifest = json.loads((STAGE / "port-source-manifest.json").read_text())
    if git("rev-parse", "HEAD") != BASE:
        raise RuntimeError("Wrong PX4 baseline")
    for name, record in manifest["files"].items():
        if digest(SOURCE / name) != record["sha256"]:
            raise RuntimeError("ARM production source mismatch: " + name)
    status = git("status", "--porcelain", "--untracked-files=all")
    for line in status.splitlines():
        # Do not strip the leading status column; paths themselves contain no spaces.
        name = line[3:]
        if name not in manifest["files"]:
            raise RuntimeError("Unexpected modified/untracked source: " + line)
    for module in json.loads((STAGE / "required-submodules-recursive.json").read_text()):
        directory = SOURCE / module["path"]
        if git("rev-parse", "HEAD", cwd=directory) != module["revision"]:
            raise RuntimeError("Wrong submodule: " + module["path"])
        if git("status", "--porcelain", "--untracked-files=no", cwd=directory):
            raise RuntimeError("Modified submodule: " + module["path"])
    if git("diff", "--name-only", "HEAD", "--", "ROMFS", "platforms/nuttx/init", "boards/px4/fmu-v6x/init"):
        raise RuntimeError("Startup changed")
    return status


def prepare():
    if SOURCE != Path("/work/px4-bench-e8ea"):
        raise RuntimeError("Unexpected private source location")
    runpy.run_path("/input/stage/prepare-container.py")
    runpy.run_path("/input/stage/prepare-nested.py")
    with tarfile.open(STAGE / "port-overlay.tar") as archive:
        archive.extractall(SOURCE, filter="tar")
    runpy.run_path("/input/stage/pin-nuttx-version-tags.py")
    subprocess.run(["chown", "-R", "1000:1000", str(SOURCE)], check=True)
    # Git's porcelain output can begin with a space; avoid strip() in the guard.
    print("Private ARM source prepared", flush=True)


def refresh():
    initial = json.loads((STAGE / "initial-port-source-manifest.json").read_text())
    for name, record in initial["files"].items():
        if digest(SOURCE / name) != record["sha256"]:
            raise RuntimeError("Unexpected pre-update ARM source: " + name)
    with tarfile.open(STAGE / "port-overlay.tar") as archive:
        archive.extractall(SOURCE, filter="tar")
    subprocess.run(["chown", "-R", "1000:1000", str(SOURCE)], check=True)
    guard()
    print("ARM overlay refreshed with sender identity dependencies", flush=True)


def export():
    if (STAGE / "candidate-receipt.json").exists():
        raise RuntimeError("Refusing to overwrite candidate receipt")
    status = guard()
    if int((STAGE / "build-exit-code.txt").read_text()) != 0:
        raise RuntimeError("ARM build did not pass")
    build = SOURCE / "build" / TARGET
    package = json.loads((build / (TARGET + ".px4")).read_text())
    image = zlib.decompress(base64.b64decode(package["image"]))
    if package["board_id"] != 53 or len(image) != package["image_size"]:
        raise RuntimeError("Wrong package target/image size")
    if image != (build / (TARGET + ".bin")).read_bytes() or len(image) > package["image_maxsize"]:
        raise RuntimeError("Image/bin mismatch or flash overflow")
    symbols = subprocess.check_output(["arm-none-eabi-nm", "-C", str(build / (TARGET + ".elf"))], text=True)
    required = ["nsh_main", "board_app_initialize", "mavlink_main", "cdcacm_autostart_main",
                "research_bench_control_main", "BenchControlOwner::cycle", "BenchOutputLease::update"]
    for name in required:
        if name not in symbols:
            raise RuntimeError("Missing ARM symbol " + name)
    if "bench_active_test_main" in symbols:
        raise RuntimeError("Memory-only fixture leaked into ARM image")
    version = (build / "src/lib/version/build_git_version.h").read_text()
    if BASE not in version or 'NUTTX_GIT_TAG_STR  "v11.0.0"' not in version:
        raise RuntimeError("Wrong PX4/NuttX version metadata")
    kernel_version = (SOURCE / "platforms/nuttx/NuttX/nuttx/.version").read_text()
    if 'CONFIG_VERSION_STRING="11.0.0"' not in kernel_version:
        raise RuntimeError("Wrong kernel version")
    config = (build / "px4_boardconfig.h").read_text()
    if "#define CONFIG_MODULES_RESEARCH_BENCH_CONTROL 1" not in config:
        raise RuntimeError("Bench option missing")
    modules = json.loads((STAGE / "required-submodules-recursive.json").read_text())
    microcdr = build / "src/modules/uxrce_dds_client/src/libmicroxrceddsclient_project-build/microcdr/src/microcdr"
    microcdr_commit = git("rev-parse", "HEAD", cwd=microcdr)
    if microcdr_commit != "3d1b17703c7cf4f22def2910bc845bdb5152d7b5":
        raise RuntimeError("Unexpected Micro-CDR revision")
    members = subprocess.check_output(["arm-none-eabi-ar", "t", str(SOURCE / "platforms/nuttx/NuttX/apps/libapps.a")], text=True)
    tables = [line for line in members.splitlines() if line.startswith("builtin_list.")]
    if len(tables) != 1 or "px4-bench-e8ea" not in tables[0]:
        raise RuntimeError("Stale/duplicate NuttX built-in table")
    (STAGE / "arm-native-members.log").write_text(members)
    (STAGE / "arm-source-status.log").write_text(status + "\n")
    timestamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    prefix = TARGET + "_bench_e8ea98b404_nuttx11_dirty_" + timestamp
    artifacts = {}
    for extension in ("px4", "elf", "bin", "map"):
        destination = STAGE / (prefix + "." + extension)
        if destination.exists():
            raise RuntimeError("Refusing artifact overwrite")
        shutil.copyfile(build / (TARGET + "." + extension), destination)
        artifacts[extension] = {"path": destination.name, "bytes": destination.stat().st_size, "sha256": digest(destination)}
    for name in ("CMakeCache.txt", "compile_commands.json", "px4_boardconfig.h", "parameters.xml",
                 "src/lib/version/build_git_version.h", TARGET + ".sbom.spdx.json"):
        shutil.copyfile(build / name, STAGE / Path(name).name)
    (STAGE / "nuttx.version").write_text(kernel_version)
    receipt = {
        "schema_version": 1, "prepared_utc": timestamp, "purpose": "manual servo bench port on verified stock baseline; software candidate only",
        "target": TARGET, "board_id": 53, "source_base": BASE, "source_dirty": True,
        "source_manifest_sha256": digest(STAGE / "port-source-manifest.json"),
        "source_archive_sha256": digest(STAGE / "stock-source.tar"), "overlay_sha256": digest(STAGE / "port-overlay.tar"),
        "container_image": "px4io/px4-dev:v1.17.0", "container_image_id": os.environ["PX4_CONTAINER_IMAGE_ID"],
        "compiler": subprocess.check_output(["arm-none-eabi-gcc", "--version"], text=True).splitlines()[0],
        "compiler_sha256": digest(shutil.which("arm-none-eabi-gcc")), "build_exit_code": 0,
        "submodules": modules, "microcdr_commit": microcdr_commit,
        "nuttx_version": "11.0.0", "nuttx_version_tag_snapshot_sha256": digest(STAGE / "nuttx-version-tag-snapshot.json"),
        "artifacts": artifacts, "image_bytes": len(image), "image_max_bytes": package["image_maxsize"],
        "remaining_bytes": package["image_maxsize"] - len(image), "required_arm_symbols": required,
        "fixture_in_arm_image": False, "automatic_startup_added": False,
        "retained_worktree": ".agents/worktrees/bench-e8ea-step3",
        "isolated_source_volume": "px4-bench-e8ea-step3-20261019", "isolated_source_path": str(SOURCE),
        "recipe_sha256": {name: digest(STAGE / name) for name in ("docker_stage.py", "build-container.sh", "prepare-container.py", "prepare-nested.py", "pin-nuttx-version-tags.py")},
        "hardware_boot_verified": False, "physical_acceptance_granted": False, "outputs_authorized": False,
    }
    (STAGE / "candidate-receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps(receipt, indent=2), flush=True)


if __name__ == "__main__":
    action = sys.argv[1]
    if action == "prepare":
        prepare()
    elif action == "refresh":
        refresh()
    elif action == "guard":
        print(guard(), flush=True)
    elif action == "export":
        export()
    else:
        raise SystemExit("Unknown action")
