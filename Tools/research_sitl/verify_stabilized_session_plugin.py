#!/usr/bin/env python3
"""Run the private session-aware physical Gazebo plant; no PX4 or hardware."""
import csv
import hashlib
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import tempfile
import uuid

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "Tools/research_sitl/stabilized_session"
FIXTURE = ROOT / "Tools/research_sitl/stabilized_manual"


def digest(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def run(args, log, **kwargs):
    with log.open("x") as stream:
        result = subprocess.run(args, stdout=stream, stderr=subprocess.STDOUT, **kwargs)
    if result.returncode:
        print(log.read_text()[-8000:], flush=True)
        raise RuntimeError(f"Failed: {log}")


def main():
    out = Path(tempfile.mkdtemp(prefix="research-stabilized-session-plugin-", dir="/var/tmp"))
    print(out, flush=True)
    inputs = {str(p.relative_to(ROOT)): digest(p) for p in SOURCE.iterdir() if p.is_file()}
    for name in ("RotationalFixture.cpp", "FixtureConstants.hpp", "world.sdf.in"):
        inputs[str((FIXTURE / name).relative_to(ROOT))] = digest(FIXTURE / name)
    for name in ("StabilizedOutput.cpp", "StabilizedOutputDriver.cpp"):
        path = ROOT / "src/lib/research_stabilized_manual" / name
        inputs[str(path.relative_to(ROOT))] = digest(path)
    build = out / "build"
    run(["cmake", "-S", str(SOURCE), "-B", str(build), "-G", "Ninja", "-DBUILD_SESSION_PLUGIN=ON",
         "-DCMAKE_BUILD_TYPE=Debug", "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"], out / "configure.log")
    run(["cmake", "--build", str(build), "-j", "4"], out / "build.log")
    run(["ctest", "--test-dir", str(build), "-V", "--output-on-failure"], out / "test.log")
    prefix = "/slaaps_session/" + uuid.uuid4().hex
    world = (FIXTURE / "world.sdf.in").read_text()
    substitutions = {"@PITCH@": "0", "@TRUTH_CSV@": str(out / "truth.csv"), "@MAGNETIC_DELAY_US@": "0",
                     "@IMU_DELAY_US@": "0", "@IMU_TOPIC@": "/model/slaaps_fixture/imu_raw",
                     "libresearch_rotational_fixture.so": str(build / "libresearch_rotational_session.so"),
                     "research_stabilized_fixture::RotationalFixture": "research_stabilized_fixture::RotationalSessionFixture"}
    for old, new in substitutions.items():
        assert old in world
        world = world.replace(old, new)
    world = world.replace("<truth_csv>", "<session_namespace>" + prefix + "</session_namespace><truth_csv>")
    assert "@" not in world
    (out / "world.sdf").write_text(world)
    env = dict(os.environ, GZ_PARTITION="session_" + uuid.uuid4().hex)
    with (out / "gazebo.log").open("x") as log:
        process = subprocess.Popen(["gz", "sim", "-s", "-r", str(out / "world.sdf")],
                                   stdout=log, stderr=subprocess.STDOUT, env=env, start_new_session=True)
        try:
            run([str(build / "plugin_probe"), prefix, str(out / "acks.csv")], out / "probe.log", env=env, timeout=45)
            assert process.poll() is None, "Gazebo exited during probe"
        finally:
            if process.poll() is None:
                os.killpg(process.pid, signal.SIGINT)
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid, signal.SIGKILL)
                    process.wait(timeout=5)
    assert "PLUGIN_RESULT acknowledgements=40 sessions=2 expiry=2 disarm=2 PASS" in (out / "probe.log").read_text()
    rows = list(csv.DictReader((out / "truth.csv").open()))
    acks = list(csv.DictReader((out / "acks.csv").open()))
    by_time = {int(row["time"]): row for row in rows}
    assert len(acks) == 40 and len({a["session"] for a in acks}) == 2
    for ack in acks:
        row = by_time[int(ack["accepted_time"])]
        assert row["enabled"] == "1" and row["session"] == ack["session"]
        assert row["command_time"] == ack["command_time"] and float(row["thrust"]) == 10
        assert all(abs(float(row["t" + str(i)]) - value) < 1e-6 for i, value in enumerate((.25, -.25, .25, -.25)))
    for session in {a["session"] for a in acks}:
        final = max(int(a["command_time"]) for a in acks if a["session"] == session)
        expired = next(row for row in rows if int(row["time"]) >= final + 100000)
        assert expired["enabled"] == "0" and float(expired["thrust"]) == 0
        assert all(float(expired["t" + str(i)]) == 0 for i in range(4))
    maximum_rate = max(abs(float(row["wx"])) for row in rows)
    assert maximum_rate > 0.01, "Physical rotational response missing"
    for name, identity in inputs.items():
        assert digest(ROOT / name) == identity
        target = out / "source" / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / name, target)
    dependencies = {}
    raw = subprocess.check_output(["ninja", "-C", str(build), "-t", "deps"], text=True)
    (out / "compiled-dependencies.log").write_text(raw)
    for line in raw.splitlines():
        if line.startswith("    "):
            path = Path(line.strip())
            path = (path if path.is_absolute() else build / path).resolve()
            if path.is_file():
                dependencies[str(path)] = digest(path)
                if path.is_relative_to(ROOT):
                    target = out / "compiled-inputs" / path.relative_to(ROOT)
                    target.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(path, target)
    (out / "compiled-inputs.json").write_text(json.dumps(dependencies, indent=2) + "\n")
    assert all(digest(Path(n)) == h for n, h in dependencies.items())
    shutil.copy2(__file__, out / Path(__file__).name)
    result = dict(passed=True, scope="real_Gazebo_session_plant_open_loop_only", acknowledgements=40,
                  sessions=2, expiry=2, disarm=2, truth_rows=len(rows), maximum_roll_rate=maximum_rate,
                  px4_integrated=False, closed_loop_qualified=False, hardware_operated=False)
    (out / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    files = {str(p.relative_to(out)): digest(p) for p in out.rglob("*") if p.is_file()}
    receipt = out / "plugin-manifest.json"
    receipt.write_text(json.dumps(dict(**result, files=files), indent=2) + "\n")
    assert all(digest(out / n) == h for n, h in files.items())
    print("PASS", json.dumps(result), "files", len(files), "receipt", digest(receipt), flush=True)


if __name__ == "__main__":
    main()
