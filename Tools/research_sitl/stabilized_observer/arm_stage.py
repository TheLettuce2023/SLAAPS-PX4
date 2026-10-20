"""Run inside px4-dev for the isolated observation image. Never flash."""
import base64
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
    expected = json.loads((STAGE / "arm-overlay-inputs.json").read_text())
    assert git("rev-parse", "HEAD") == BASE
    for name, value in expected.items():
        assert digest(SOURCE / name) == value, name
    for line in git("status", "--porcelain", "--untracked-files=all").splitlines():
        assert line[3:] in expected, line
    for item in json.loads((STAGE / "required-submodules-recursive.json").read_text()):
        repo = SOURCE / item["path"]
        assert git("rev-parse", "HEAD", cwd=repo) == item["revision"], item
        assert not git("status", "--porcelain", "--untracked-files=no", cwd=repo), item
    assert not git("diff", "--name-only", "HEAD", "--", "ROMFS", "platforms/nuttx/init", "boards/px4/fmu-v6x/init")


def prepare():
    assert SOURCE == Path("/work/px4-stabilized-observer")
    assert not (SOURCE / ".git").exists()
    runpy.run_path("/input/stage/prepare-container.py")
    runpy.run_path("/input/stage/prepare-nested.py")
    with tarfile.open(STAGE / "observer-overlay.tar") as archive:
        archive.extractall(SOURCE, filter="tar")
    runpy.run_path("/input/stage/pin-nuttx-version-tags.py")
    subprocess.run(["chown", "-R", "1000:1000", str(SOURCE)], check=True)
    guard()
    print("Isolated observer ARM sources prepared", flush=True)


def refresh():
    previous = json.loads((STAGE / "initial-arm-overlay-inputs.json").read_text())
    for name, expected in previous.items():
        assert digest(SOURCE / name) == expected, name
    with tarfile.open(STAGE / "observer-overlay.tar") as archive:
        archive.extractall(SOURCE, filter="tar")
    subprocess.run(["chown", "-R", "1000:1000", str(SOURCE)], check=True)
    guard()
    print("Verified observer portability update applied", flush=True)


def export():
    assert not (STAGE / "observer-arm-receipt.json").exists()
    guard()
    assert int((STAGE / "build-exit-code.txt").read_text()) == 0
    build = SOURCE / "build" / TARGET
    package = json.loads((build / (TARGET + ".px4")).read_text())
    image = zlib.decompress(base64.b64decode(package["image"]))
    assert package["board_id"] == 53
    assert len(image) == package["image_size"] <= package["image_maxsize"]
    assert image == (build / (TARGET + ".bin")).read_bytes()
    symbols = subprocess.check_output(["arm-none-eabi-nm", "-C", str(build / (TARGET + ".elf"))], text=True)
    for name in ("research_stabilized_manual_main", "StabilizedManualControl::cycle", "allocateCanards", "readObserverProfile"):
        assert name in symbols, name
    for name in ("research_bench_control_main", "stabilized_observer_test_main", "research_stabilized_sitl_main"):
        assert name not in symbols, name
    config = (build / "px4_boardconfig.h").read_text()
    assert "#define CONFIG_MODULES_RESEARCH_STABILIZED_MANUAL 1" in config
    assert "#define CONFIG_MODULES_RESEARCH_BENCH_CONTROL 1" not in config
    version = (build / "src/lib/version/build_git_version.h").read_text()
    assert BASE in version and 'NUTTX_GIT_TAG_STR  "v11.0.0"' in version
    output = STAGE / "artifacts"
    output.mkdir()
    for suffix in (".px4", ".bin", ".elf"):
        shutil.copy2(build / (TARGET + suffix), output / (TARGET + "_e8ea98b404_dirty_observer" + suffix))
    for name in ("compile_commands.json", "CMakeCache.txt", "px4_boardconfig.h"):
        shutil.copy2(build / name, STAGE / name)
    (STAGE / "symbols.txt").write_text(symbols)
    (STAGE / "size.txt").write_text(subprocess.check_output(["arm-none-eabi-size", str(build / (TARGET + ".elf"))], text=True))
    (STAGE / "build_git_version.h").write_text(version)
    compiler = shutil.which("arm-none-eabi-gcc")
    receipt = {"schema_version": 1, "scope": "output-disabled stabilized observer; not hardware accepted",
               "base": BASE, "dirty": True, "target": TARGET, "board_id": package["board_id"], "build_exit_code": 0,
               "image_bytes": len(image), "flash_headroom_bytes": package["image_maxsize"] - len(image),
               "overlay_sha256": digest(STAGE / "observer-overlay.tar"), "source_manifest_sha256": digest(STAGE / "arm-overlay-inputs.json"),
               "compiler_sha256": digest(compiler), "bench_owner_linked": False, "test_publishers_linked": False,
               "hardware_tested": False, "actuator_publication": False,
               "artifacts": {str(p.relative_to(STAGE)): digest(p) for p in output.iterdir()}}
    (STAGE / "observer-arm-receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps(receipt, indent=2))


if __name__ == "__main__":
    globals()[sys.argv[1]]()
