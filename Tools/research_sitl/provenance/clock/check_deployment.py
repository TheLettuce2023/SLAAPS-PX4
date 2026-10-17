#!/usr/bin/env python3
"""Independently check closed provider identity evidence, not clock qualification."""
import argparse
import importlib.util
from pathlib import Path
import re

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("native_check", HERE.parent / "native/check_capture.py")
native = importlib.util.module_from_spec(spec)
spec.loader.exec_module(native)
require, read, sha = native.require, native.read, native.sha

def snapshot(value, objects):
    expected = {}
    for line in value["maps"].splitlines():
        fields = line.split(maxsplit=5)
        if len(fields) != 6 or "x" not in fields[1] or not fields[5].startswith("/"):
            continue
        _, _, _, device, inode, name = fields
        require(not name.endswith(" (deleted)"), "deleted mapping")
        identity = (device, int(inode))
        require(name not in expected or expected[name] == identity, "inconsistent duplicate mapping")
        expected[name] = identity
    files = value["executable_files"]
    require(set(files) == set(expected) and files, "mapping inventory differs")
    require(value["exe"] in files, "process executable absent from retained mappings")
    for name, record in files.items():
        require((record["device"], record["inode"]) == expected[name], "mapping file identity mismatch")
        digest = record["sha256"]
        require(re.fullmatch("[0-9a-f]{64}", digest) is not None, "invalid object identity")
        path = objects / digest
        require(path.is_file() and not path.is_symlink() and path.stat().st_size == record["bytes"] and sha(path) == digest,
                "retained provider object drift")
    require(value["system_preload"] == {"path": "/etc/ld.so.preload", "absent": True}, "unreviewed preload")
    for name in ("monotonic_before_ns", "monotonic_after_ns", "pid"):
        native.uint(value[name])
    require(0 < value["monotonic_before_ns"] <= value["monotonic_after_ns"], "snapshot clock regression")
    require(any(Path(name).name.startswith("libc.so") for name in files), "missing libc")
    return set(r["sha256"] for r in files.values())

def check(area):
    area = Path(area).resolve()
    m = read(area / "manifest.json")
    require(m["schema"] == "closed-clock-deployment/v1" and m["status"] == "observed_unqualified", "incomplete deployment")
    require(m["forced_shutdown"] is False and m["exit_code"] == 0, "unclean closure")
    for field in ("clock_map_qualified", "map_installed", "positive_admission",
                  "provider_semantics_proven", "all_native_clock_returns_checked"):
        require(m[field] is False, "qualification invented")
    origin = Path(m["native_area"]).resolve()
    require(sha(origin / "manifest.json") == m["native_manifest_sha256"], "reference manifest drift")
    require(native.check(origin) == m["native_check"], "native reference check differs")
    expected_scripts = {"capture_deployment.py", "check_deployment.py", "native_check.py"}
    require(set(m["source_hashes"]) == expected_scripts and
            {p.name for p in (area / "source").iterdir()} == expected_scripts, "script inventory differs")
    for name, digest in m["source_hashes"].items():
        require(sha(area / "source" / name) == digest, "script drift")
    require(set(m["artifact_hashes"]) == {"startup.sh", "imu.txt", "stdout.log", "observations.json"}, "runtime inventory differs")
    runtime = area / "run"
    for name, digest in m["artifact_hashes"].items():
        require(sha(runtime / name) == digest, "runtime artifact drift")
    for name in ("startup.sh", "imu.txt"):
        require(sha(runtime / name) == sha(origin / "run-1" / name), "reference stimulus/startup differs")
    origin_manifest = read(origin / "manifest.json")
    require(m["executable_sha256"] == origin_manifest["executable_sha256"], "executable binding differs")
    trace = read(runtime / "observations.json")
    summary = native.observations(trace, m["run_id"])
    require(summary == m["observations"], "observation reconstruction differs")
    log = (runtime / "stdout.log").read_text()
    require("P2B_CAPTURE_COMPLETE" in log and "ERROR [" not in log, "runtime incomplete/error")
    objects = set()
    for side in ("parent", "child"):
        first, last = m[side + "_before"], m[side + "_after"]
        objects |= snapshot(first, area / "objects") | snapshot(last, area / "objects")
        for field in ("pid", "exe", "boot_id", "time_namespace", "mount_namespace", "executable_files"):
            require(first[field] == last[field], "deployment identity changed")
        require(first["monotonic_after_ns"] <= last["monotonic_before_ns"], "snapshot ordering")
    require({p.name for p in (area / "objects").iterdir()} == objects, "retained object inventory differs")
    child_exe = str(origin / "build/bin/px4")
    require(m["child_before"]["exe"] == child_exe and
            m["child_before"]["executable_files"][child_exe]["sha256"] == m["executable_sha256"], "wrong child executable")
    for phase in ("before", "after"):
        parent, child = m["parent_" + phase], m["child_" + phase]
        for field in ("boot_id", "time_namespace"):
            require(parent[field] == child[field], "producer/validator timeline differs")
    start, end = m["before_launch_ns"], m["after_exit_ns"]
    native.uint(start); native.uint(end)
    require(m["parent_before"]["monotonic_after_ns"] <= start <= m["child_before"]["monotonic_before_ns"] <=
            m["child_after"]["monotonic_after_ns"] <= end <= m["parent_after"]["monotonic_before_ns"], "process interval contradiction")
    require(all(start // 1000 <= r["sample_us"] <= r["observed_us"] <= (end + 999) // 1000
                for r in trace["records"]), "native clock bracket contradiction")
    return {"verified": True, "scope": "retained_deployment_observation_only",
            "manifest_sha256": sha(area / "manifest.json"), "retained_executable_objects": len(objects),
            "record_count": summary["record_count"], "clock_map_qualified": False, "positive_admission": False}

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("area", type=Path)
    import json
    print(json.dumps(check(parser.parse_args().area), indent=2, sort_keys=True))
