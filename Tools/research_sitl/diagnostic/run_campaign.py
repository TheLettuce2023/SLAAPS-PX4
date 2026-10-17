#!/usr/bin/env python3
"""Run a synthetic, output-disabled C1B campaign and bind its local evidence.

This is standalone host tooling. It never connects to PX4, Gazebo or hardware.
Output directories are new and retained on both success and failure.
"""

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

from check_campaign import CampaignError, check_campaign, source_paths


PROFILE = "c1b-owner-reference/v1"
DEFINES = ["RESEARCH_SITL_DIAGNOSTIC_OFFLINE=1",
           "RESEARCH_SITL_DIAGNOSTIC_HOST_TEST=1"]


def digest(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def source_manifest(source_root):
    return [{"path": str(path), "sha256": digest(source_root / path)}
            for path in source_paths(source_root)]


def unique_keys(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise CampaignError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def reject_constant(value):
    raise CampaignError(f"non-finite JSON constant: {value}")


def build_identity(build_dir, source_root):
    cache = {}
    for line in (build_dir / "CMakeCache.txt").read_text(encoding="utf-8").splitlines():
        if line.startswith(("//", "#")) or "=" not in line or ":" not in line.split("=", 1)[0]:
            continue
        key, value = line.split("=", 1)
        cache[key.split(":", 1)[0]] = value
    if cache.get("RESEARCH_SITL_DIAGNOSTIC_TESTS") != "ON":
        raise CampaignError("build is not explicitly opted into diagnostic tests")
    if Path(cache.get("CMAKE_HOME_DIRECTORY", "")).resolve() != source_root / "src/lib/research_acceleration":
        raise CampaignError("build cache does not name this standalone source tree")
    configuration = cache.get("CMAKE_BUILD_TYPE")
    if configuration not in ("Debug", "Release"):
        raise CampaignError("campaign requires a recorded Debug or Release build")
    flags = cache.get("CMAKE_CXX_FLAGS", "")
    if "-fsanitize=address,undefined" in flags:
        configuration = "Sanitize"
    compiler = cache.get("CMAKE_CXX_COMPILER")
    if not compiler:
        raise CampaignError("missing compiler identity")
    version = subprocess.run([compiler, "--version"], check=True,
                             capture_output=True, text=True, timeout=10)
    return {"configuration": configuration, "compiler": version.stdout.splitlines()[0],
            "defines": DEFINES}


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, allow_nan=False) + "\n", encoding="utf-8")


def run(args):
    source_root = args.source_root.resolve(strict=True)
    build_dir = args.build_dir.resolve(strict=True)
    executable = args.executable.resolve(strict=True)
    # A supplied build identity must describe the executable actually staged.
    if executable.parent != build_dir:
        raise CampaignError("executable must be directly inside its supplied build directory")
    if args.output_dir:
        output = args.output_dir.absolute()
        output.mkdir(parents=False, exist_ok=False)
    else:
        output = Path(tempfile.mkdtemp(prefix="c1b-evidence-", dir=build_dir))
    print(f"Retained evidence: {output}", file=sys.stderr)
    try:
        before = source_manifest(source_root)
        build = build_identity(build_dir, source_root)
        staged = output / "diagnostic_campaign"
        executable_hash = digest(executable)
        shutil.copy2(executable, staged)
        if digest(staged) != executable_hash:
            raise CampaignError("staged executable does not match the supplied build")
        try:
            completed = subprocess.run([str(staged)], capture_output=True, timeout=30, check=False)
        except subprocess.TimeoutExpired as error:
            (output / "raw_campaign.json").write_bytes(error.stdout or b"")
            (output / "stderr.log").write_bytes(error.stderr or b"")
            raise
        (output / "raw_campaign.json").write_bytes(completed.stdout)
        (output / "stderr.log").write_bytes(completed.stderr)
        if completed.returncode != 0:
            raise CampaignError(f"campaign exited with status {completed.returncode}")
        if digest(executable) != executable_hash or digest(staged) != executable_hash:
            raise CampaignError("executable changed during the campaign")
        if before != source_manifest(source_root):
            raise CampaignError("source set or contents changed during the campaign")
        campaign = json.loads(completed.stdout.decode("utf-8"), object_pairs_hook=unique_keys,
                              parse_constant=reject_constant)
        if not isinstance(campaign, dict) or not isinstance(campaign.get("header"), dict):
            raise CampaignError("campaign has no structured header")
        if "artifacts" in campaign["header"]:
            raise CampaignError("exporter must not invent artifact hashes")
        binary = {"path": "diagnostic_campaign", "sha256": executable_hash}
        campaign["header"]["artifacts"] = {"sources": before, "executable": binary, "build": build}
        campaign_path = output / "campaign.json"
        write_json(campaign_path, campaign)
        manifest = {"schema": "research-c1b-artifacts/v1", "profile_id": PROFILE,
                    "sources": before, "executable": binary,
                    "campaign": {"path": "campaign.json", "sha256": digest(campaign_path)},
                    "build": build, "complete": True}
        write_json(output / "manifest.json", manifest)
        summary = check_campaign(campaign, manifest, output, source_root)
        if before != source_manifest(source_root):
            raise CampaignError("source set or contents changed during checking")
        write_json(output / "checked_summary.json", summary)
        print(json.dumps(summary, sort_keys=True, allow_nan=False))
        return 0
    except Exception as error:
        # A partial directory is deliberately retained, never converted to PASS.
        write_json(output / "failure.json", {"status": "incomplete", "reason": str(error)})
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path)
    args = parser.parse_args()
    try:
        return run(args)
    except (CampaignError, OSError, ValueError, subprocess.SubprocessError) as error:
        print(f"C1B evidence incomplete: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
