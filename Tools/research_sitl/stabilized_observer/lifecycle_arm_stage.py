"""Isolated lifecycle-observer ARM firmware build; never flash hardware."""
import base64
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import zlib

SOURCE = Path("/work/px4-stabilized-observer")
DONOR = Path("/donor/px4-stabilized-observer")
OUT = Path("/output")
BASE = "e8ea98b404baf50623a519ac67ec1c03aa240859"
TARGET = "px4_fmu-v6x_default"


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def record(name, content):
    (OUT / name).write_text(json.dumps(content, indent=2) + "\n")


def git(*args, source=SOURCE):
    return subprocess.check_output(["git", "-C", str(source), "--work-tree=" + str(source), *args],
                                   text=True).rstrip("\n")


def guard(source=SOURCE, parent=False):
    expected = json.loads((OUT / ("parent-arm-overlay-inputs.json" if parent else "source-inputs.json")).read_text())
    assert git("rev-parse", "HEAD", source=source) == BASE
    for name, identity in expected.items():
        assert digest(source / name) == identity, name
    for line in git("status", "--porcelain", "--untracked-files=all", source=source).splitlines():
        assert line[3:] in expected, line
    for item in json.loads((OUT / "parent-required-submodules-recursive.json").read_text()):
        repo = source / item["path"]
        assert git("rev-parse", "HEAD", source=repo) == item["revision"], item
        assert not git("status", "--porcelain", "--untracked-files=no", source=repo), item
    assert not git("diff", "--name-only", "HEAD", "--", "ROMFS", "platforms/nuttx/init",
                   "boards/px4/fmu-v6x/init", source=source)


def prepare():
    assert not SOURCE.exists(), "A fresh, empty build volume is required"
    # The qualified donor is a retained worktree. Its common metadata is mounted
    # read-only at the original absolute path; optional index writes are disabled.
    gitfile = DONOR / ".git"
    assert gitfile.is_file() and gitfile.read_text().startswith("gitdir: ")
    assert Path(gitfile.read_text().strip().removeprefix("gitdir: ")).is_dir()
    guard(DONOR, parent=True)
    parent = json.loads((OUT / "parent-observer-arm-receipt.json").read_text())
    for suffix in (".bin", ".elf", ".px4"):
        key = "artifacts/" + TARGET + "_e8ea98b404_dirty_observer" + suffix
        assert digest(DONOR / "build" / TARGET / (TARGET + suffix)) == parent["artifacts"][key]
    # Copy source and git/submodule metadata only. No prior compiled object or
    # cached firmware artifact participates in this clean build.
    shutil.copytree(DONOR, SOURCE, symlinks=True,
                    ignore=lambda path, names: {"build"} if Path(path) == DONOR else set())
    preparation = json.loads((OUT / "preparation.json").read_text())
    assert digest(OUT / "firmware-overlay.tar") == preparation["overlay_sha256"]
    assert digest(OUT / "source-inputs.json") == preparation["source_inputs_sha256"]
    with tarfile.open(OUT / "firmware-overlay.tar") as archive:
        archive.extractall(SOURCE, filter="tar")
    guard()
    subprocess.run(["chown", "-R", "1000:1000", str(SOURCE)], check=True)
    print("Fresh lifecycle-observer source prepared; no build directory inherited", flush=True)


def build():
    guard()
    if (OUT / "build-exit-code.txt").exists():
        assert (OUT / "build-exit-code.txt").read_text().strip() != "0", "Successful build is immutable"
        attempt = 1
        while (OUT / f"build-attempt-{attempt}.log").exists():
            attempt += 1
        (OUT / "build.log").rename(OUT / f"build-attempt-{attempt}.log")
        (OUT / "build-exit-code.txt").rename(OUT / f"build-attempt-{attempt}-exit-code.txt")
    dependency = DONOR / "build" / TARGET / "src/modules/uxrce_dds_client/src/libmicroxrceddsclient_project-build/microcdr/src/microcdr"
    revision = git("rev-parse", "HEAD", source=dependency)
    assert git("rev-parse", "v2.0.1^{commit}", source=dependency) == revision
    assert not git("status", "--porcelain", "--untracked-files=no", source=dependency)
    archive = OUT / "offline-microcdr-source.tar"
    subprocess.run(["git", "-C", str(dependency), "archive", "--format=tar", "--output=" + str(archive), revision], check=True)
    record("offline-microcdr.json", {"requested_tag": "v2.0.1", "commit": revision,
                                    "source_archive_sha256": digest(archive),
                                    "source": str(dependency), "compiled_artifacts_reused": False})
    environment = dict(os.environ, CCACHE_DISABLE="1", GIT_CONFIG_COUNT="2",
                       GIT_CONFIG_KEY_1="url.file://" + str(dependency) + ".insteadOf",
                       GIT_CONFIG_VALUE_1="https://github.com/eProsima/Micro-CDR.git")
    with (OUT / "build.log").open("x") as log:
        result = subprocess.run(["make", TARGET, "-j8"], cwd=SOURCE,
                                stdout=log, stderr=subprocess.STDOUT,
                                env=environment)
    (OUT / "build-exit-code.txt").write_text(str(result.returncode) + "\n")
    if result.returncode:
        print((OUT / "build.log").read_text()[-6000:], flush=True)
        raise RuntimeError("Fresh ARM build failed; retained log and exit code")
    guard()
    print("Fresh ARM link PASS", flush=True)


def export():
    guard()
    assert (OUT / "build-exit-code.txt").read_text().strip() == "0"
    assert not (OUT / "firmware-receipt.json").exists()
    build_dir = SOURCE / "build" / TARGET
    package = json.loads((build_dir / (TARGET + ".px4")).read_text())
    image = zlib.decompress(base64.b64decode(package["image"]))
    assert package["board_id"] == 53
    assert len(image) == package["image_size"] <= package["image_maxsize"]
    assert image == (build_dir / (TARGET + ".bin")).read_bytes()
    symbols = subprocess.check_output(["arm-none-eabi-nm", "-C", str(build_dir / (TARGET + ".elf"))], text=True)
    for name in ("research_stabilized_manual_main", "StabilizedManualControl::cycle", "allocateCanards", "readObserverProfile"):
        assert name in symbols, name
    for name in ("research_bench_control_main", "stabilized_observer_test_main", "research_stabilized_sitl_main",
                 "stabilized_output_test_main"):
        assert name not in symbols, name
    config = (build_dir / "px4_boardconfig.h").read_text()
    assert "#define CONFIG_MODULES_RESEARCH_STABILIZED_MANUAL 1" in config
    assert "#define CONFIG_MODULES_RESEARCH_BENCH_CONTROL 1" not in config
    version = (build_dir / "src/lib/version/build_git_version.h").read_text()
    assert BASE in version and 'NUTTX_GIT_TAG_STR  "v11.0.0"' in version
    artifacts = OUT / "artifacts"
    artifacts.mkdir()
    for suffix in (".px4", ".bin", ".elf"):
        shutil.copy2(build_dir / (TARGET + suffix),
                     artifacts / (TARGET + "_e8ea98b404_dirty_observer_lifecycle" + suffix))
    for name in ("compile_commands.json", "CMakeCache.txt", "px4_boardconfig.h"):
        shutil.copy2(build_dir / name, OUT / name)
    (OUT / "symbols.txt").write_text(symbols)
    size = subprocess.check_output(["arm-none-eabi-size", str(build_dir / (TARGET + ".elf"))], text=True)
    (OUT / "size.txt").write_text(size)
    (OUT / "build_git_version.h").write_text(version)
    # The link map and ELF sizes expose static placement only; runtime heap,
    # stack high-water and physical scheduling still require target evidence.
    for path in build_dir.glob("*.map"):
        shutil.copy2(path, OUT / path.name)
    compiler = Path(shutil.which("arm-none-eabi-gcc")).resolve()
    record("firmware-receipt.json", {
        "scope": "output-disabled lifecycle observer with PX4IO handled-result correction",
        "base": BASE, "dirty": True, "target": TARGET, "board_id": 53,
        "clean_build": True, "build_exit_code": 0,
        "image_bytes": len(image), "image_maxsize": package["image_maxsize"],
        "flash_headroom_bytes": package["image_maxsize"] - len(image),
        "size": size, "compiler_sha256": digest(compiler),
        "compiler_version": subprocess.check_output([str(compiler), "--version"], text=True),
        "source_inputs_sha256": digest(OUT / "source-inputs.json"),
        "overlay_sha256": digest(OUT / "firmware-overlay.tar"),
        "bench_owner_linked": False, "test_publishers_linked": False,
        "hardware_operated": False, "active_stabilized_outputs": False,
        "files": {p.relative_to(OUT).as_posix(): digest(p) for p in OUT.rglob("*") if p.is_file()},
    })
    print(size)
    print("Firmware staged; flash headroom", package["image_maxsize"] - len(image), flush=True)


if __name__ == "__main__":
    {"prepare": prepare, "build": build, "export": export, "guard": guard}[sys.argv[1]]()
