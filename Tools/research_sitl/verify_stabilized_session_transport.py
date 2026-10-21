#!/usr/bin/env python3
"""Verify the private Gazebo session codec; no simulator or hardware launched."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "Tools/research_sitl/stabilized_session"


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(command, log, **kwargs):
    with log.open("x") as stream:
        result = subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT, **kwargs)
    if result.returncode:
        print(log.read_text()[-7000:], flush=True)
        raise RuntimeError(f"Failed: {log}")


def main():
    out = Path(tempfile.mkdtemp(prefix="research-stabilized-session-transport-", dir="/var/tmp"))
    print(out, flush=True)
    inputs = {str(p.relative_to(ROOT)): digest(p) for p in SOURCE.iterdir() if p.is_file()}
    results = {}
    dependencies = {}
    for label, kind, flags in (("debug", "Debug", ""), ("release", "Release", ""),
                              ("sanitize", "Debug", "-fsanitize=address,undefined -fno-omit-frame-pointer")):
        build = out / label
        run(["cmake", "-S", str(SOURCE), "-B", str(build), "-G", "Ninja",
             "-DCMAKE_BUILD_TYPE=" + kind, "-DCMAKE_CXX_FLAGS=" + flags], out / (label + "-configure.log"))
        run(["cmake", "--build", str(build), "-j", "4"], out / (label + "-build.log"))
        run(["ctest", "--test-dir", str(build), "-V", "--output-on-failure"], out / (label + "-test.log"),
            env=dict(os.environ, ASAN_OPTIONS="detect_leaks=1:halt_on_error=1", UBSAN_OPTIONS="halt_on_error=1"))
        text = (out / (label + "-test.log")).read_text()
        assert "100% tests passed" in text and "0 failures" in text
        results[label] = [line.strip() for line in text.splitlines() if "Session transport:" in line or "Plant endpoint:" in line or "Remote completion:" in line]
        assert len(results[label]) == 3
        deps = subprocess.check_output(["ninja", "-C", str(build), "-t", "deps"], text=True)
        (out / (label + "-dependencies.log")).write_text(deps)
        for line in deps.splitlines():
            if line.startswith("    "):
                path = Path(line.strip())
                path = path if path.is_absolute() else build / path
                path = path.resolve()
                if path.is_file():
                    dependencies[str(path)] = digest(path)
        print(label, results[label], flush=True)
    for name, identity in inputs.items():
        assert digest(ROOT / name) == identity
        target = out / "source" / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / name, target)
    assert all(digest(Path(name)) == identity for name, identity in dependencies.items())
    (out / "compiled-inputs.json").write_text(json.dumps(dependencies, indent=2) + "\n")
    shutil.copy2(__file__, out / Path(__file__).name)
    files = {str(p.relative_to(out)): digest(p) for p in out.rglob("*") if p.is_file()}
    manifest = dict(scope="private_Gazebo_codec_and_plant_endpoint_transactions", passed=True, results=results,
                    production_integrated=False, dynamic_fixture_integrated=False,
                    hardware_operated=False, files=files)
    receipt = out / "transport-manifest.json"
    receipt.write_text(json.dumps(manifest, indent=2) + "\n")
    assert all(digest(out / name) == identity for name, identity in files.items())
    print("Sealed", len(files), "files;", len(dependencies), "compiled inputs;", digest(receipt), flush=True)


if __name__ == "__main__":
    main()
