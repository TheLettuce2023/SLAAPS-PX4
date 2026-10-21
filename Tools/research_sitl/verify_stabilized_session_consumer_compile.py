#!/usr/bin/env python3
"""Compile the actual private PX4 consumer against verified native PX4 inputs."""
import hashlib
import argparse
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[2]
BASE = Path("/var/tmp/research-stabilized-output-native-health-20261020")
EXPECTED = "d1ad43871180a22b3d1eb7d0cf827418a2369fa49d288f6140ee20f2b1374b4e"
def digest(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", choices=("Px4SessionConsumer.cpp", "Px4SessionSITL.cpp"), default="Px4SessionConsumer.cpp")
    args = parser.parse_args()
    receipt = BASE / "runtime-manifest.json"
    assert digest(receipt) == EXPECTED
    manifest = json.loads(receipt.read_text())
    for name, identity in manifest["files"].items():
        assert digest(BASE / name) == identity
    out = Path(tempfile.mkdtemp(prefix="research-stabilized-session-consumer-", dir="/var/tmp"))
    print(out, flush=True)
    overlay = out / "overlay"
    for folder in ("src/lib/research_stabilized_manual", "src/modules/research_stabilized_manual"):
        for path in (ROOT / folder).glob("*.hpp"):
            target = overlay / path.relative_to(ROOT / "src")
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, target)
    path = ROOT / "platforms/common/uORB/ResearchSubscription.hpp"
    target = overlay / "uORB/ResearchSubscription.hpp"
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(path, target)
    source = ROOT / "Tools/research_sitl/stabilized_session" / args.source
    commands = json.loads((BASE / "native/compile_commands.json").read_text())
    entry, = [e for e in commands if e["file"].endswith("/ScheduledOutputTest.cpp")]
    command = []
    skip = False
    for arg in shlex.split(entry["command"]):
        if skip:
            skip = False
            continue
        if arg in ("-o", "-MF", "-MT", "-MQ"):
            skip = True
            continue
        if arg in ("-MD", "-MMD"):
            continue
        command.append(str(source) if arg == entry["file"] else arg)
    command[1:1] = ["-I" + str(overlay), "-DRESEARCH_STABILIZED_SITL_ONLY=1"]
    command += [flag for flag in shlex.split(subprocess.check_output(["pkg-config", "--cflags", "gz-transport13", "gz-msgs10"], text=True)) if not flag.startswith("-std=")]
    command += ["-o", str(out / "consumer.o"), "-MD", "-MF", str(out / "consumer.d"), "-fstack-usage"]
    (out / "command.json").write_text(json.dumps(command, indent=2) + "\n")
    with (out / "compile.log").open("x") as log:
        result = subprocess.run(command, cwd=entry["directory"], env=dict(os.environ, CCACHE_DISABLE="1"), stdout=log, stderr=subprocess.STDOUT)
    if result.returncode:
        print((out / "compile.log").read_text()[-10000:])
        raise RuntimeError("Consumer compile failed")
    paths = shlex.split((out / "consumer.d").read_text().replace("\\\n", "").split(":", 1)[1])
    inputs = {}
    for name in paths:
        path = Path(name)
        path = path if path.is_absolute() else Path(entry["directory"]) / path
        inputs[str(path)] = digest(path)
        target = out / "inputs" / str(path).lstrip("/")
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, target)
    (out / "inputs.json").write_text(json.dumps(inputs, indent=2) + "\n")
    (out / "symbols.txt").write_text(subprocess.check_output(["nm", "-C", str(out / "consumer.o")], text=True))
    assert "Px4SessionConsumer::Run()" in (out / "symbols.txt").read_text()
    if args.source == "Px4SessionSITL.cpp":
        assert "Px4SessionSITL::Run()" in (out / "symbols.txt").read_text()
    shutil.copy2(__file__, out / Path(__file__).name)
    files = {str(p.relative_to(out)): digest(p) for p in out.rglob("*") if p.is_file()}
    result = dict(scope="actual_PX4_POSIX_session_translation_unit_only", source=args.source, passed=True, runtime_verified=False,
                  new_PX4_ELF_linked=False, hardware_operated=False, baseline_receipt=EXPECTED, files=files)
    receipt = out / "consumer-manifest.json"
    receipt.write_text(json.dumps(result, indent=2) + "\n")
    assert all(digest(out / n) == h for n, h in files.items())
    print("PASS", len(files), "files; receipt", digest(receipt), flush=True)
if __name__ == "__main__":
    main()
