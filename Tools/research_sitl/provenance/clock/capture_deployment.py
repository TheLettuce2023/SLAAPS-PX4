#!/usr/bin/env python3
"""Record a closed common-clock deployment; never issue or install a map."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import secrets
import shutil
import signal
import subprocess
import tempfile
import time

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("native_check", HERE.parent / "native/check_capture.py")
native = importlib.util.module_from_spec(spec)
spec.loader.exec_module(native)

def save(path, value):
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n")

def require(value, reason):
    if not value:
        raise ValueError(reason)

def sha(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()

def clock():
    value = time.clock_gettime_ns(time.CLOCK_MONOTONIC)
    require(type(value) is int and 0 < value < ((1 << 64) - 1) * 1000, "clock outside representation")
    return value

def preload():
    path = Path("/etc/ld.so.preload")
    require(not path.exists(), "system preload needs a separate provider review")
    return {"path": str(path), "absent": True}

def snapshot(pid, objects):
    """Bind mapped executable files to actual device/inode identities."""
    proc = Path("/proc") / str(pid)
    before = clock()
    maps = (proc / "maps").read_text()
    require(len(maps) < 1024 * 1024, "oversize process map")
    files = {}
    for line in maps.splitlines():
        fields = line.split(maxsplit=5)
        if len(fields) != 6 or "x" not in fields[1] or not fields[5].startswith("/"):
            continue
        address, perms, offset, device, inode, name = fields
        require(not name.endswith(" (deleted)"), "deleted executable mapping")
        path = proc / "root" / name.lstrip("/")
        major, minor = (int(x, 16) for x in device.split(":"))
        with path.open("rb") as stream:
            stat = os.fstat(stream.fileno())
            require(stat.st_ino == int(inode) and os.major(stat.st_dev) == major and os.minor(stat.st_dev) == minor,
                    "mapped executable descriptor identity changed")
            require(stat.st_size <= 256 * 1024 * 1024, "oversize mapped executable")
            digest = hashlib.file_digest(stream, "sha256").hexdigest()
            record = {"sha256": digest, "device": device, "inode": int(inode), "bytes": stat.st_size}
            retained = objects / digest
            if not retained.exists():
                stream.seek(0)
                with retained.open("xb") as target:
                    shutil.copyfileobj(stream, target)
            require(sha(retained) == digest and retained.stat().st_size == stat.st_size,
                    "mapped executable changed during retention")
            after = os.fstat(stream.fileno())
            current = path.stat()
            require((stat.st_dev, stat.st_ino, stat.st_size, stat.st_mtime_ns, stat.st_ctime_ns) ==
                    (after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns, after.st_ctime_ns) and
                    (current.st_dev, current.st_ino) == (stat.st_dev, stat.st_ino),
                    "mapped executable path/object changed during retention")
        require(name not in files or files[name] == record, "inconsistent mapped executable")
        files[name] = record
    require(files and any(Path(p).name.startswith("libc.so") for p in files), "libc mapping not ready")
    return {"pid": pid, "exe": os.readlink(proc / "exe"), "maps": maps, "executable_files": files,
            "boot_id": Path("/proc/sys/kernel/random/boot_id").read_text().strip(),
            "time_namespace": os.readlink(proc / "ns/time"),
            "mount_namespace": os.readlink(proc / "ns/mnt"),
            "monotonic_before_ns": before, "monotonic_after_ns": clock(),
            "system_preload": preload()}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("native_area", type=Path)
    args = parser.parse_args()
    origin = args.native_area.resolve()
    origin_result = native.check(origin)
    source_manifest = native.read(origin / "manifest.json")
    expected_manifest = sha(origin / "manifest.json")
    # This environment check covers this already-running Python observer too.
    require(not any(k.startswith("LD_") for k in os.environ), "observer loader environment needs review")
    preload()
    area = Path(tempfile.mkdtemp(prefix="p3a-clock-", dir="/var/tmp"))
    print(str(area), flush=True)
    runtime = area / "run"; runtime.mkdir()
    objects = area / "objects"; objects.mkdir()
    for name in ("startup.sh", "imu.txt"):
        shutil.copy2(origin / "run-1" / name, runtime / name)
    scripts = area / "source"; scripts.mkdir()
    for path in HERE.glob("*.py"):
        shutil.copy2(path, scripts / path.name)
    # Preserve the exact independent native checker used for reference validation.
    shutil.copy2(HERE.parent / "native/check_capture.py", scripts / "native_check.py")
    script_hashes = {p.name: sha(p) for p in scripts.iterdir()}
    identity = str(secrets.randbits(63) + 1)
    executable = origin / "build/bin/px4"
    environment = {k:v for k,v in os.environ.items()
                   if not k.startswith(("PX4_", "SIM_", "GZ_", "P2B_", "LD_")) and k not in ("replay", "replay_mode")}
    environment["P2B_RUN_ID"] = identity
    command = [str(executable), "-i", str(secrets.randbelow(50000) + 10000),
               "-w", str(runtime), "-s", str(runtime / "startup.sh")]
    manifest = {"schema": "closed-clock-deployment/v1", "status": "incomplete",
                "native_area": str(origin), "native_manifest_sha256": expected_manifest,
                "native_check": origin_result, "source_hashes": script_hashes,
                "executable_sha256": source_manifest["executable_sha256"],
                "run_id": identity, "command": command, "clock_map_qualified": False,
                "map_installed": False, "positive_admission": False,
                "provider_semantics_proven": False, "all_native_clock_returns_checked": False,
                "forced_shutdown": False}
    process = None
    try:
        manifest["parent_before"] = snapshot(os.getpid(), objects)
        manifest["before_launch_ns"] = clock()
        deadline = manifest["before_launch_ns"] + 25_000_000_000
        with (runtime / "stdout.log").open("w") as log:
            process = subprocess.Popen(command, cwd=runtime, env=environment, stdin=subprocess.PIPE,
                                       stdout=log, stderr=subprocess.STDOUT, text=True, start_new_session=True)
            # Wait only for loader completion; preserve the first complete mapping.
            last_error = None
            for _ in range(100):
                try:
                    manifest["child_before"] = snapshot(process.pid, objects)
                    break
                except (OSError, ValueError) as error:
                    last_error = error
                    require(process.poll() is None, "native process exited before provider snapshot")
                    time.sleep(0.01)
            else:
                raise ValueError("provider snapshot unavailable: " + str(last_error))
            while "P2B_CAPTURE_COMPLETE" not in (runtime / "stdout.log").read_text():
                require(process.poll() is None and clock() < deadline, "native capture incomplete or overdue")
                time.sleep(0.02)
            manifest["child_after"] = snapshot(process.pid, objects)
            process.communicate("shutdown\n", timeout=5)
            manifest["exit_code"] = process.returncode
        manifest["after_exit_ns"] = clock()
        manifest["parent_after"] = snapshot(os.getpid(), objects)
        require(process.returncode == 0, "unclean native exit")
        stdout = (runtime / "stdout.log").read_text()
        require("ERROR [" not in stdout, "native runtime error")
        trace = native.read(runtime / "observations.json")
        manifest["observations"] = native.observations(trace, identity)
        require(sha(origin / "manifest.json") == expected_manifest and
                sha(executable) == manifest["executable_sha256"], "reference changed")
        manifest["artifact_hashes"] = {name: sha(runtime / name) for name in
                                       ("startup.sh", "imu.txt", "stdout.log", "observations.json")}
        for side in ("parent", "child"):
            first, last = manifest[side + "_before"], manifest[side + "_after"]
            for field in ("exe", "boot_id", "time_namespace", "mount_namespace", "executable_files"):
                require(first[field] == last[field], side + " deployment identity changed: " + field)
        for phase in ("before", "after"):
            parent, child = manifest["parent_" + phase], manifest["child_" + phase]
            require(parent["boot_id"] == child["boot_id"] and parent["time_namespace"] == child["time_namespace"],
                    "producer/validator timeline identity differs")
        require(all(manifest["before_launch_ns"] // 1000 <= r["sample_us"] <= r["observed_us"] <=
                    (manifest["after_exit_ns"] + 999) // 1000 for r in trace["records"]), "clock bracket contradiction")
        manifest["status"] = "observed_unqualified"
    except BaseException as error:
        manifest["status"] = "failed"; manifest["error"] = str(error)
        raise
    finally:
        if process is not None and process.poll() is None:
            manifest["forced_shutdown"] = True
            os.killpg(process.pid, signal.SIGTERM)
            try:
                process.communicate(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL); process.communicate(timeout=5)
        save(area / "manifest.json", manifest)

if __name__ == "__main__":
    main()
