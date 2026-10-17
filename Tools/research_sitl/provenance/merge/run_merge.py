#!/usr/bin/env python3
"""Verify closed host merge mechanics and replay retained native observations."""
import argparse
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("native_check", HERE.parent / "native/check_capture.py")
native = importlib.util.module_from_spec(spec)
spec.loader.exec_module(native)


def save(path, value):
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("native_area", type=Path)
    args = parser.parse_args()
    origin = args.native_area.resolve()
    checked = native.check(origin)
    area = Path(tempfile.mkdtemp(prefix="p2c-merge-", dir="/var/tmp"))
    print(area, flush=True)
    source = area / "source"; source.mkdir()
    files = {p.name: native.sha(p) for p in HERE.iterdir() if p.is_file()}
    for name in files:
        shutil.copy2(HERE / name, source / name)
    shutil.copy2(HERE.parent / "native/check_capture.py", source / "native_check.py")
    archived = {p.name: native.sha(p) for p in source.iterdir()}
    manifest = dict(schema="closed-receipt-merge/v1", status="incomplete", native_area=str(origin),
                    native_check=checked, source_hashes=archived, builds={}, commands=[],
                    execution_origin="offline_native_observation_replay", live_publisher_qualified=False,
                    clock_map_qualified=False, positive_admission=False, adapter_linked=False)

    def run(name, command, expect_success=True):
        result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=120)
        log = area / (name + ".log"); log.write_text(result.stdout)
        manifest["commands"].append(dict(name=name, argv=command, exit_code=result.returncode,
                                         log_sha256=native.sha(log), expected_success=expect_success))
        native.require((result.returncode == 0) == expect_success, name + " unexpected result; see " + str(log))

    try:
        run("default-configure", ["cmake", "-S", str(source), "-B", str(area / "default")])
        run("default-build", ["cmake", "--build", str(area / "default")])
        native.require(not (area / "default/receipt_merge").exists(), "default profile enabled")
        probe = area / "probe.cpp"; probe.write_text('#include "ReceiptMerge.hpp"\n')
        for name, flags in (("unmarked", []), ("px4", ["-DRESEARCH_CLOSED_MERGE_HOST=1", "-D__PX4_POSIX=1"]),
                            ("lockstep", ["-DRESEARCH_CLOSED_MERGE_HOST=1", "-DENABLE_LOCKSTEP_SCHEDULER=1"])):
            run("reject-" + name, ["c++", "-std=c++17", "-fsyntax-only", "-I", str(source), *flags, str(probe)], False)
        for name, sanitized in (("debug", False), ("sanitized", True)):
            build = area / name
            run(name + "-configure", ["cmake", "-S", str(source), "-B", str(build),
                                      "-DRESEARCH_BUILD_CLOSED_MERGE=ON", "-DCMAKE_BUILD_TYPE=Debug",
                                      "-DRESEARCH_MERGE_SANITIZE=" + ("ON" if sanitized else "OFF")])
            run(name + "-build", ["cmake", "--build", str(build), "-j", "2"])
            run(name + "-test", ["ctest", "--test-dir", str(build), "--output-on-failure"])
            manifest["builds"][name] = dict(executable_sha256=native.sha(build / "receipt_merge"))
        raw = native.read(origin / "run-1/observations.json")
        rows = []
        for r in raw["records"]:
            values = [r["sequence"], r["observed_us"], r["sample_us"], r["control_flags"], r["fault_flags"],
                      int(r["attitude_valid"]), int(r["horizontal_valid"]), int(r["vertical_position_valid"]),
                      int(r["vertical_velocity_valid"])]
            rows.append(" ".join(str(x) for x in values))
        fixture = area / "input.tsv"; fixture.write_text("\n".join(rows) + "\n")
        manifest["before_replay_ns"] = time.monotonic_ns()
        run("replay", [str(area / "debug/receipt_merge"), "--replay", str(fixture), str(area / "merged.tsv")])
        manifest["after_replay_ns"] = time.monotonic_ns()
        manifest["artifact_hashes"] = {name: native.sha(area / name) for name in ("input.tsv", "merged.tsv")}
        configuration_files = ["default/CMakeCache.txt"]
        for name in ("debug", "sanitized"):
            configuration_files += [name + "/CMakeCache.txt", name + "/CMakeFiles/receipt_merge.dir/flags.make",
                                    name + "/CMakeFiles/receipt_merge.dir/link.txt"]
        manifest["configuration_hashes"] = {name: native.sha(area / name) for name in configuration_files}
        native.require(files == {p.name: native.sha(p) for p in HERE.iterdir() if p.is_file()}, "merge source drift")
        native.require(native.check(origin) == checked, "native reference drift")
        manifest["status"] = "replayed_unqualified"
    except BaseException as error:
        manifest["status"] = "failed"; manifest["error"] = str(error)
        raise
    finally:
        save(area / "manifest.json", manifest)


if __name__ == "__main__":
    main()
