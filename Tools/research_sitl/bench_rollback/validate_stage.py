"""Independently check retained bench firmware/evidence; no hardware access."""
import base64
import hashlib
import json
from pathlib import Path
import struct
import sys
import zlib


def digest(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main():
    root = Path(__file__).resolve().parents[3]
    stage = Path(sys.argv[1]) if len(sys.argv) > 1 else root / "build/bench_commissioning/bench-e8ea98b404-step3"
    arm = json.loads((stage / "candidate-receipt.json").read_text())
    qualification = json.loads((stage / "qualification-receipt.json").read_text())
    source = json.loads((stage / "port-source-manifest.json").read_text())
    evidence = json.loads((stage / "native-evidence-manifest.json").read_text())
    assert arm["source_base"] == qualification["base"] == source["base"] == "e8ea98b404baf50623a519ac67ec1c03aa240859"
    assert arm["source_dirty"] and qualification["source_dirty"]
    assert arm["build_exit_code"] == 0 and arm["board_id"] == 53
    assert arm["source_manifest_sha256"] == qualification["source_manifest_sha256"] == digest(stage / "port-source-manifest.json")
    assert arm["source_archive_sha256"] == digest(stage / "stock-source.tar")
    assert arm["overlay_sha256"] == digest(stage / "port-overlay.tar")
    assert qualification["arm_receipt_sha256"] == digest(stage / "candidate-receipt.json")
    assert qualification["native_evidence_manifest_sha256"] == digest(stage / "native-evidence-manifest.json")
    assert qualification["production_port_patch_sha256"] == digest(stage / "production-port-e8ea.patch")
    assert qualification["runner_sha256"] == digest(root / "Tools/research_sitl/verify_bench_rollback.py")
    for name, expected in arm["recipe_sha256"].items():
        assert digest(stage / name) == expected, name
    for name, entry in source["files"].items():
        assert digest(stage / "ported-source" / name) == entry["sha256"], name
    for name, entry in evidence.items():
        file = stage / "native-tests" / name
        assert file.stat().st_size == entry["bytes"] and digest(file) == entry["sha256"], name
    for entry in arm["artifacts"].values():
        file = stage / entry["path"]
        assert file.stat().st_size == entry["bytes"] and digest(file) == entry["sha256"]
    package = json.loads((stage / arm["artifacts"]["px4"]["path"]).read_text())
    image = zlib.decompress(base64.b64decode(package["image"]))
    assert image == (stage / arm["artifacts"]["bin"]["path"]).read_bytes()
    assert len(image) == package["image_size"] == arm["image_bytes"]
    assert package["board_id"] == 53 and package["image_maxsize"] == arm["image_max_bytes"] == 1966080
    assert len(image) <= package["image_maxsize"]
    vectors = struct.unpack("<8I", image[:32])
    assert 0x24000000 <= vectors[0] < 0x24080000
    assert vectors[1] & 1 and 0x08020000 <= vectors[1] < 0x08020000 + len(image)
    slot_crc = zlib.crc32(image + b"\xff" * (package["image_maxsize"] - len(image)), 0xffffffff) ^ 0xffffffff
    binary_hash = digest(stage / "native-tests/sitl-build/bin/px4")
    assert binary_hash == qualification["native_binary_sha256"]
    for name in ("results.json.1", "results.json.2", "results.json"):
        results = json.loads((stage / "native-tests" / name).read_text())
        assert len(results) == 20 and sum(r["checks"] for r in results) == 3632
        assert all(r["passed"] and r["binary_sha256"] == binary_hash for r in results)
    assert qualification["disabled_startup_smoke_passed"] and not arm["automatic_startup_added"]
    assert not arm["fixture_in_arm_image"]
    for receipt in (arm, qualification):
        assert not receipt["hardware_boot_verified"] and not receipt["outputs_authorized"] and not receipt["physical_acceptance_granted"]
    result = {
        "schema_version": 1, "outcome": "software_candidate_and_qualification_evidence_consistent",
        "arm_receipt_sha256": digest(stage / "candidate-receipt.json"),
        "qualification_receipt_sha256": digest(stage / "qualification-receipt.json"),
        "native_evidence_files_verified": len(evidence), "ported_source_files_verified": len(source["files"]),
        "image_bytes": len(image), "remaining_flash_bytes": package["image_maxsize"] - len(image),
        "first_eight_vectors_hex": [f"0x{value:08x}" for value in vectors], "expected_slot_crc_hex": f"0x{slot_crc:08x}",
        "validator_sha256": digest(Path(__file__)), "hardware_boot_verified": False, "outputs_authorized": False,
    }
    output = stage / "artifact-validation.json"
    if output.exists():
        raise RuntimeError("Refusing to overwrite validation evidence")
    output.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
