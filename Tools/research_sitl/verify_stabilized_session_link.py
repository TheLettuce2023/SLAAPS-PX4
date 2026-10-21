#!/usr/bin/env python3
"""Seal a fresh session-consumer link and module-registration smoke test."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import build_stabilized_manual_checkout as builder

def digest(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--evidence", type=Path, required=True)
    args = parser.parse_args()
    out = args.evidence.resolve()
    assert out.parent == Path("/var/tmp") and out.name.startswith("research-stabilized-sitl-")
    assert not (out / "session-link-manifest.json").exists()
    record = json.loads((out / "checkout-source-origin.json").read_text())
    result = json.loads((out / "session-link-result.json").read_text())
    builder.verify_sources(out, record)
    binary = out / "sitl-lockstep-build/bin/px4"
    assert digest(binary) == result["px4_sha256"]
    config = (out / "sitl-lockstep-build/px4_boardconfig.h").read_text()
    assert "#define CONFIG_MODULES_RESEARCH_STABILIZED_MANUAL 1" in config
    assert "#define CONFIG_MODULES_RESEARCH_BENCH_CONTROL 1" not in config
    symbols = subprocess.check_output(["nm", "-C", str(binary)], text=True)
    assert "research_session_consumer_main" in symbols and "Px4SessionConsumer::Run()" in symbols
    assert "research_session_sitl_main" in symbols and "Px4SessionSITL::Run()" in symbols
    assert "research_stabilized_sitl_main" not in symbols
    assert "research_bench_control_main" not in symbols
    work = Path(tempfile.mkdtemp(prefix="session-link-smoke-", dir=out))
    script = work / "rc.test"
    script.write_text("#!/bin/sh\nset -e\n. px4-alias.sh\nresearch_session_consumer || true\nresearch_session_sitl || true\nshutdown\n")
    with (work / "console.log").open("x") as log:
        completed = subprocess.run([str(binary), "-d", "-i", "29479", "-w", str(work), "-s", str(script)],
                                   cwd=work, stdout=log, stderr=subprocess.STDOUT, timeout=30)
    text = (work / "console.log").read_text()
    assert completed.returncode == 0 and "start <observer-profile> <unique-plant-namespace>" in text
    assert "Unknown command" not in text
    with (out / "session-plugin-tests.log").open("x") as log:
        subprocess.run(["ctest", "--test-dir", str(out / "session-plugin-build"), "-V", "--output-on-failure"],
                       stdout=log, stderr=subprocess.STDOUT, check=True)
    assert "100% tests passed" in (out / "session-plugin-tests.log").read_text()
    source = out / "px4-source-publisher-epoch"
    for folder in ("Tools/research_sitl/stabilized_session", "src/lib/research_stabilized_manual",
                   "src/modules/research_stabilized_manual", "platforms/common/uORB"):
        for path in (source / folder).rglob("*"):
            if path.is_file():
                target = out / "verified-session-source" / path.relative_to(source)
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(path, target)
    for path in (source / "msg").glob("Stabilized*.msg"):
        target = out / "verified-session-source/msg" / path.name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, target)
    result.update(module_registration_smoke_passed=True, consumer_started=False, producer_started=False,
                  full_runtime_verified=False, plugin_sha256=digest(out / "session-plugin-build/libresearch_rotational_session.so"),
                  lifecycle_implementation="production_observer_only", legacy_fixture_module_linked=False)
    (out / "session-link-verification.json").write_text(json.dumps(result, indent=2) + "\n")
    shutil.copy2(__file__, out / Path(__file__).name)
    paths = [p for p in out.iterdir() if p.is_file()]
    paths += list((out / "verified-session-source").rglob("*"))
    paths += list(work.rglob("*"))
    for name in ("sitl-lockstep-build/bin/px4", "sitl-lockstep-build/compile_commands.json",
                 "sitl-lockstep-build/px4_boardconfig.h", "sitl-lockstep-build/CMakeCache.txt",
                 "session-plugin-build/libresearch_rotational_session.so", "session-plugin-build/plugin_probe",
                 "session-plugin-build/RotationalSessionFixture.cpp", "session-plugin-build/CMakeCache.txt"):
        paths.append(out / name)
    files = {str(p.relative_to(out)): digest(p) for p in sorted(set(paths)) if p.is_file()}
    receipt = out / "session-link-manifest.json"
    receipt.write_text(json.dumps(dict(**result, files=files), indent=2) + "\n")
    assert all(digest(out / n) == h for n, h in files.items())
    print("PASS session link + command registration;", len(files), "files; receipt", digest(receipt), flush=True)

if __name__ == "__main__":
    main()
