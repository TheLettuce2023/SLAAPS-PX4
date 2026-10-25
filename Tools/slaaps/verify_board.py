#!/usr/bin/env python3
"""Verify SLAAPS board artifacts and current production sources; never flash."""
import argparse
import base64
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import zlib

ROOT = Path(__file__).resolve().parents[2]
TARGET = "px4_fmu-v6x_slaaps"


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def bound_path(directory, name):
    path = (directory / name).resolve()
    assert path.is_relative_to(directory) and path.is_file(), name
    return path


def verify_elf(data, image, origin, capacity):
    header = struct.unpack_from("<16sHHIIIIIHHHHHH", data)
    assert header[0][:7] == b"\x7fELF\x01\x01\x01" and header[2] == 40, "Require little-endian ELF32 ARM"
    offset, size, count = header[5], header[9], header[10]
    assert size == 32 and offset + size * count <= len(data)
    segments, end = 0, origin
    for index in range(count):
        kind, file_offset, _, address, file_size, memory_size, _, _ = struct.unpack_from("<IIIIIIII", data, offset + index * size)
        if kind != 1 or not file_size or not origin <= address < origin + capacity:
            continue
        assert file_size <= memory_size and file_offset + file_size <= len(data)
        first, last = address - origin, address - origin + file_size
        assert last <= len(image) <= capacity
        assert data[file_offset:file_offset + file_size] == image[first:last], "ELF/bin load mismatch"
        segments += 1
        end = max(end, address + file_size)
    assert segments and end - origin == len(image), "ELF/bin flash extent mismatch"
    return segments


def defined_elf_functions(data):
    """Read definitions from the checked ARM ELF, not a potentially stale nm log."""
    header = struct.unpack_from("<16sHHIIIIIHHHHHH", data)
    assert header[0][:7] == b"\x7fELF\x01\x01\x01" and header[2] == 40
    offset, size, count = header[6], header[11], header[12]
    assert size == 40 and offset + size * count <= len(data)
    sections = [struct.unpack_from("<IIIIIIIIII", data, offset + index * size) for index in range(count)]
    functions = set()
    for section in sections:
        if section[1] != 2: # SHT_SYMTAB
            continue
        start, length, linked, entry_size = section[4], section[5], section[6], section[9]
        assert entry_size == 16 and length % entry_size == 0 and start + length <= len(data) and linked < count
        strings = sections[linked]
        assert strings[1] == 3 and strings[4] + strings[5] <= len(data)
        names = data[strings[4]:strings[4] + strings[5]]
        for position in range(start, start + length, entry_size):
            name, _, _, info, _, index = struct.unpack_from("<IIIBBH", data, position)
            if info & 15 != 2 or not 0 < index < count: # Defined STT_FUNC
                continue
            if sections[index][2] & 6 != 6: # SHF_ALLOC | SHF_EXECINSTR
                continue
            assert name < len(names)
            end = names.find(b"\0", name)
            assert end >= name
            functions.add(names[name:end].decode("ascii"))
    assert functions, "ELF has no defined function symbols"
    return functions


def verify_identity_implementation(stage, receipt, manifest, data):
    profile = "boards/px4/fmu-v6x/nuttx-config/slaaps/defconfig"
    entry = manifest["inputs"].get(profile)
    if entry is None:
        return dict(required=False, implementation="not_required_by_snapshot")
    source = bound_path(ROOT, profile)
    assert digest(source) == entry["sha256"]
    if not re.search(r"^CONFIG_STM32H7_RNG=y$", source.read_text(), re.M):
        return dict(required=False, implementation="not_required_by_snapshot")
    configuration = receipt.get("nuttx_configuration")
    provenance = "builder_receipt"
    if configuration is None:
        # Explicit post-link capture supports immutable receipts produced before
        # the builder exported these files. It remains bound to this artifact.
        captured = json.loads(bound_path(stage, "nuttx-configuration.json").read_text())
        assert captured["board_receipt_sha256"] == digest(stage / "board-result.json")
        configuration = captured["nuttx_configuration"]
        provenance = "receipt_bound_postlink_capture"
    text = {}
    for label in ("header", "config"):
        item = configuration[label]
        path = bound_path(stage, item["path"])
        assert digest(path) == item["sha256"]
        text[label] = path.read_text()
    required = ("STM32H7_RNG", "STM32H7_HSI48", "ARCH_HAVE_RNG", "DEV_RANDOM", "DEV_URANDOM_ARCH")
    for name in required:
        assert re.search(r"^#define CONFIG_" + name + r" 1$", text["header"], re.M), name
        assert re.search(r"^CONFIG_" + name + r"=y$", text["config"], re.M), name
    assert not re.search(r"^#define CONFIG_DEV_URANDOM_XORSHIFT128\b", text["header"], re.M)
    assert not re.search(r"^CONFIG_DEV_URANDOM_XORSHIFT128=y$", text["config"], re.M)
    required_symbols = ("getrandom", "devrandom_register", "devurandom_register", "stm32_rng_read", "stm32_rng_interrupt")
    functions = defined_elf_functions(data)
    for name in required_symbols:
        assert any(symbol == name or symbol.startswith(name + ".") for symbol in functions), "Missing linked hardware RNG function: " + name
    return dict(required=True, implementation="configured_and_linked_stm32h7_rng",
                configuration_provenance=provenance, configuration=configuration,
                verified_defined_elf_functions=list(required_symbols), hardware_entropy_qualified=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("stage", type=Path)
    args = parser.parse_args()
    stage = args.stage.resolve()
    result_path = stage / "independent-verification.json"
    assert not result_path.exists(), "Successful verification receipts are immutable"
    receipt_path = stage / "board-result.json"
    receipt = json.loads(receipt_path.read_text())
    assert receipt["passed"] and receipt["target"] == TARGET
    assert (stage / "exit-code.txt").read_text().strip() == "0"
    manifest_path = bound_path(stage, receipt["source_manifest"])
    assert digest(manifest_path) == receipt["source_manifest_sha256"]
    assert digest(stage / "build_board_snapshot.py") == receipt["builder_sha256"]
    manifest = json.loads(manifest_path.read_text())
    version = stage / "build_git_version.h"
    assert digest(version) == receipt["version_header_sha256"]
    tag = re.search(r'#define PX4_GIT_TAG_STR\s+"([^"]+)"', version.read_text())
    assert tag and tag[1] == receipt["expected_git_tag"] and tag[1] != "v0.0.0"
    artifacts = {}
    for name, item in receipt["files"].items():
        path = bound_path(stage, name)
        assert digest(path) == item["sha256"] and path.stat().st_size == item["size"]
        assert path.suffix not in artifacts
        artifacts[path.suffix] = path
    assert set(artifacts) == {".px4", ".bin", ".elf"}
    package = json.loads(artifacts[".px4"].read_text())
    prototype_path = ROOT / "boards/px4/fmu-v6x/firmware.prototype"
    linker_path = ROOT / "boards/px4/fmu-v6x/nuttx-config/scripts/script.ld"
    required = [prototype_path.relative_to(ROOT).as_posix(), linker_path.relative_to(ROOT).as_posix()]
    prototype = json.loads(prototype_path.read_text())
    flash = re.search(r"FLASH\s+\(rx\)\s*:\s*ORIGIN\s*=\s*(0x[0-9a-fA-F]+),\s*LENGTH\s*=\s*(\d+)K", linker_path.read_text())
    assert flash
    origin, capacity = int(flash[1], 16), int(flash[2]) * 1024
    assert package["board_id"] == prototype["board_id"] == 53
    assert package["magic"] == prototype["magic"] == "PX4FWv1"
    assert package["image_maxsize"] == prototype["image_maxsize"] == capacity
    assert package["git_hash"] == manifest["repositories"]["."]
    image = zlib.decompress(base64.b64decode(package["image"]))
    assert len(image) == package["image_size"] and image == artifacts[".bin"].read_bytes()
    assert tag[1].encode() + b"\0" in image, "Embedded version string missing from flash image"
    segments = verify_elf(artifacts[".elf"].read_bytes(), image, origin, capacity)
    identity = verify_identity_implementation(stage, receipt, manifest, artifacts[".elf"].read_bytes())
    changed = subprocess.check_output(["git", "ls-files", "-m", "-o", "--exclude-standard"], cwd=ROOT, text=True).splitlines()
    required += [name for name in changed if name.startswith(("src/", "platforms/", "boards/", "msg/", "ROMFS/", "cmake/")) or name == "CMakeLists.txt"]
    hashes = {}
    removed = []
    for name in sorted(set(required)):
        path = (ROOT / name).resolve()
        assert path.is_relative_to(ROOT), name
        if not path.exists():
            assert name not in manifest["inputs"], "Removed source remains in board snapshot: " + name
            removed.append(name)
            continue
        path = bound_path(ROOT, name)
        assert manifest["inputs"][name]["kind"] == "file"
        assert digest(path) == manifest["inputs"][name]["sha256"], name
        hashes[name] = digest(path)
    result = dict(passed=True, scope="board_package_elf_capacity_and_current_changed_production_sources",
                  board_id=53, embedded_git_tag=tag[1], image_bytes=len(image), flash_capacity=capacity,
                  flash_headroom=capacity-len(image), flash_load_segments=segments,
                  board_receipt_sha256=digest(receipt_path), verifier_sha256=digest(Path(__file__)),
                  identity_implementation=identity,
                  checked_source_hashes=hashes, removed_sources=removed, hardware_operated=False, boot_verified=False)
    verifier_snapshot = stage / "verify_board.py"
    assert not verifier_snapshot.exists()
    verifier_snapshot.write_bytes(Path(__file__).read_bytes())
    result_path.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({key:value for key,value in result.items() if key != "checked_source_hashes"}), flush=True)


if __name__ == "__main__":
    main()
