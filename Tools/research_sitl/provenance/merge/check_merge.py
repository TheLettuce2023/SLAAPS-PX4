#!/usr/bin/env python3
"""Reconstruct closed merged rows from the original verified native observations."""
import argparse
import importlib.util
import json
from pathlib import Path
import re

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("native_check", HERE.parent / "native/check_capture.py")
native = importlib.util.module_from_spec(spec)
spec.loader.exec_module(native)
require, read, sha = native.require, native.read, native.sha


def check(area):
    area = Path(area).resolve()
    m = read(area / "manifest.json")
    require(m["schema"] == "closed-receipt-merge/v1" and m["status"] == "replayed_unqualified", "incomplete merge replay")
    require(m["execution_origin"] == "offline_native_observation_replay", "wrong provenance scope")
    for field in ("live_publisher_qualified", "clock_map_qualified", "positive_admission", "adapter_linked"):
        require(m[field] is False, "invented qualification")
    expected = {"ReceiptMerge.hpp", "ReceiptMerge.cpp", "MergeHarness.cpp", "CMakeLists.txt",
                "run_merge.py", "check_merge.py", "native_check.py"}
    require(set(m["source_hashes"]) == expected and {p.name for p in (area / "source").iterdir()} == expected,
            "source inventory differs")
    for name, digest in m["source_hashes"].items():
        require(sha(area / "source" / name) == digest, "source archive drift")
    expected_commands = {"default-configure", "default-build", "reject-unmarked", "reject-px4", "reject-lockstep",
                         "debug-configure", "debug-build", "debug-test", "sanitized-configure", "sanitized-build",
                         "sanitized-test", "replay"}
    require(len(m["commands"]) == len(expected_commands) and {c["name"] for c in m["commands"]} == expected_commands,
            "verification command inventory differs")
    source = area / "source"
    expected_argv = {
        "default-configure": ["cmake", "-S", str(source), "-B", str(area / "default")],
        "default-build": ["cmake", "--build", str(area / "default")],
        "replay": [str(area / "debug/receipt_merge"), "--replay", str(area / "input.tsv"), str(area / "merged.tsv")],
    }
    for name, definitions in (("unmarked", []), ("px4", ["-DRESEARCH_CLOSED_MERGE_HOST=1", "-D__PX4_POSIX=1"]),
                              ("lockstep", ["-DRESEARCH_CLOSED_MERGE_HOST=1", "-DENABLE_LOCKSTEP_SCHEDULER=1"])):
        expected_argv["reject-" + name] = ["c++", "-std=c++17", "-fsyntax-only", "-I", str(source), *definitions, str(area / "probe.cpp")]
    for name in ("debug", "sanitized"):
        build = area / name
        expected_argv[name + "-configure"] = ["cmake", "-S", str(source), "-B", str(build),
                                              "-DRESEARCH_BUILD_CLOSED_MERGE=ON", "-DCMAKE_BUILD_TYPE=Debug",
                                              "-DRESEARCH_MERGE_SANITIZE=" + ("ON" if name == "sanitized" else "OFF")]
        expected_argv[name + "-build"] = ["cmake", "--build", str(build), "-j", "2"]
        expected_argv[name + "-test"] = ["ctest", "--test-dir", str(build), "--output-on-failure"]
    require((area / "probe.cpp").read_text() == '#include "ReceiptMerge.hpp"\n', "compile probe drift")
    for c in m["commands"]:
        expected_success = not c["name"].startswith("reject-")
        require(c["expected_success"] is expected_success and (c["exit_code"] == 0) == expected_success,
                "verification command failed")
        require(c["argv"] == expected_argv[c["name"]], "verification command binding differs")
        require(sha(area / (c["name"] + ".log")) == c["log_sha256"], "verification log drift")
        log = (area / (c["name"] + ".log")).read_text()
        if c["name"].endswith("-test"):
            require("100% tests passed, 0 tests failed out of 1" in log, "test execution absent")
        if c["name"].startswith("reject-"):
            require("Closed receipt merge" in log, "probe failed for an unrelated reason")
    configuration = {"default/CMakeCache.txt"}
    for name in ("debug", "sanitized"):
        configuration |= {name + "/CMakeCache.txt", name + "/CMakeFiles/receipt_merge.dir/flags.make",
                          name + "/CMakeFiles/receipt_merge.dir/link.txt"}
    require(set(m["configuration_hashes"]) == configuration, "configuration inventory differs")
    for name, digest in m["configuration_hashes"].items():
        require(sha(area / name) == digest, "build configuration drift")
    for name in ("default", "debug", "sanitized"):
        cache = (area / name / "CMakeCache.txt").read_text().splitlines()
        require("CMAKE_HOME_DIRECTORY:INTERNAL=" + str(source) in cache, "wrong configured source")
        require("RESEARCH_BUILD_CLOSED_MERGE:BOOL=" + ("OFF" if name == "default" else "ON") in cache,
                "wrong default/build switch")
        require("RESEARCH_MERGE_SANITIZE:BOOL=" + ("ON" if name == "sanitized" else "OFF") in cache,
                "wrong sanitizer configuration")
        if name != "default":
            require("CMAKE_BUILD_TYPE:STRING=Debug" in cache, "wrong build type")
            flags = (area / name / "CMakeFiles/receipt_merge.dir/flags.make").read_text()
            link = (area / name / "CMakeFiles/receipt_merge.dir/link.txt").read_text()
            require("-DRESEARCH_CLOSED_MERGE_HOST=1" in flags, "missing closed-profile marker")
            require(("-fsanitize=address,undefined" in flags and "-fsanitize=address,undefined" in link)
                    == (name == "sanitized"), "sanitizer compile/link binding differs")
    require(set(m["builds"]) == {"debug", "sanitized"}, "build inventory differs")
    for name, value in m["builds"].items():
        require(sha(area / name / "receipt_merge") == value["executable_sha256"], "executable drift")
    require(not (area / "default/receipt_merge").exists(), "default target enabled")
    require(set(m["artifact_hashes"]) == {"input.tsv", "merged.tsv"}, "replay inventory differs")
    for name, digest in m["artifact_hashes"].items():
        require((area / name).stat().st_size <= 1024 * 1024 and sha(area / name) == digest, "replay artifact drift")
    origin = Path(m["native_area"]).resolve()
    require(native.check(origin) == m["native_check"], "native reference drift")
    records = read(origin / "run-1/observations.json")["records"]
    wanted = [[r["sequence"], r["observed_us"], r["sample_us"], r["control_flags"], r["fault_flags"],
               int(r["attitude_valid"]), int(r["horizontal_valid"]), int(r["vertical_position_valid"]),
               int(r["vertical_velocity_valid"])] for r in records]
    require((area / "input.tsv").read_text() == "".join(" ".join(map(str, r)) + "\n" for r in wanted),
            "fixture does not reconstruct native rows")
    lines = (area / "merged.tsv").read_text().splitlines()
    require(len(lines) == len(records) + 1, "merge row count")
    previous_time = 0
    for index, (line, row) in enumerate(zip(lines[:-1], wanted)):
        require(re.fullmatch(r"[0-9]+(?: [0-9]+){11}", line) is not None, "malformed merged row")
        actual = list(map(int, line.split()))
        require(all(x < 1 << 64 for x in actual) and actual[0] == index + 1 and actual[2] == 0, "owner order/slot")
        require(actual[3:] == row, "native observation changed or reordered")
        require(0 < actual[1] and actual[1] >= previous_time, "owner clock regression")
        previous_time = actual[1]
    end = lines[-1].split()
    require(len(end) == 4 and end[:2] == ["closed", "owner_stop"] and end[2].isdigit() and end[3].isdigit(), "missing closure")
    require(int(end[2]) >= previous_time and int(end[2]) < 1 << 64 and int(end[3]) == len(records), "closure mismatch")
    return dict(verified=True, scope="closed_merge_offline_native_replay", record_count=len(records),
                manifest_sha256=sha(area / "manifest.json"), live_publisher_qualified=False, positive_admission=False)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("area", type=Path)
    print(json.dumps(check(parser.parse_args().area), indent=2, sort_keys=True))
