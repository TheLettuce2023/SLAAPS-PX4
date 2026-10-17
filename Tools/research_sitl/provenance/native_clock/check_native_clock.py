#!/usr/bin/env python3
"""Check native receipt-clock reads and exact merged timestamp correspondence."""
import argparse
import importlib.util
from pathlib import Path
import json
import re
HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("p2d_check", HERE.parent / "native_merge/check_native_merge.py")
p2d = importlib.util.module_from_spec(spec); spec.loader.exec_module(p2d)
require, read, sha, uint = p2d.require, p2d.read, p2d.sha, p2d.uint
PREFIX = "Tools/research_sitl/provenance/"
OVERLAYS = p2d.OVERLAYS | {PREFIX + "native_merge/NativeMerge.cpp", PREFIX + "provider/CheckedProvider.hpp", PREFIX + "provider/CheckedProvider.cpp"}


def clock_trace(clock, merged, run_id, case, raw):
    injected = case == "read_error"
    require(type(merged["bridge_fault"]) is int and merged["bridge_fault"] == (6 if injected else 0), "bridge did not propagate checked read failure")
    # Reuse P2D structural checks only; the actual fault disposition is checked above.
    structural = dict(merged, bridge_fault=0)
    result = p2d.trace(structural, run_id, "normal", raw)
    require(len(merged["records"]) == (25 if injected else 1200), "unexpected source prefix")
    require(set(clock) == {"schema", "run_id", "injection_configured", "fault", "fault_lane", "map_qualified", "positive_admission", "fault_record", "lanes"}, "clock fields")
    require(clock["schema"] == "p3c-native-clock/v1" and clock["run_id"] == run_id and
            clock["map_qualified"] is False and clock["positive_admission"] is False and
            clock["injection_configured"] is injected, "clock authority/injection binding")
    require(type(clock["fault"]) is int and clock["fault"] == (4 if injected else 0) and
            type(clock["fault_lane"]) is int and clock["fault_lane"] == (1 if injected else 0), "sticky clock fault")
    lanes = clock["lanes"]
    require(type(lanes) is list and len(lanes) == 3, "lane inventory")
    fields = {"sequence", "purpose", "code", "error", "seconds", "nanoseconds", "microseconds", "injected"}
    terminal = clock["fault_record"]
    require(type(terminal) is dict and set(terminal) == fields and
            all(type(terminal[k]) is int for k in fields - {"injected"}) and type(terminal["injected"]) is bool,
            "terminal read fields/types")
    for lane_index, lane in enumerate(lanes):
        require(type(lane) is list and 0 < len(lane) <= 4096, "lane bound")
        last = None
        for index, r in enumerate(lane, 1):
            require(set(r) == fields and all(type(r[k]) is int for k in fields - {"injected"}) and type(r["injected"]) is bool, "clock read fields/types")
            require(r["sequence"] == index and r["purpose"] in ({3,4,5,6}, {2}, {0,1})[lane_index], "lane identity/sequence/purpose")
            expected_injection = injected and lane_index == 1 and index == 26
            require(r["injected"] is expected_injection, "injection masquerading as actual read")
            if expected_injection:
                require(r["code"] == 1 and r["error"] == 5 and r["seconds"] == r["nanoseconds"] == r["microseconds"] == 0, "synthetic EIO contents")
            else:
                require(r["code"] == r["error"] == 0 and 0 <= r["seconds"] < 1 << 63 and 0 <= r["nanoseconds"] < 10**9 and
                        r["microseconds"] == r["seconds"] * 1000000 + r["nanoseconds"] // 1000 and
                        0 < r["microseconds"] < 1 << 64, "checked conversion")
                now = (r["seconds"], r["nanoseconds"])
                require(last is None or now >= last, "raw lane clock regression"); last = now
    source = lanes[1]
    require(len(source) == (26 if injected else 1200), "source checked-read coverage")
    require([r["microseconds"] for r in source if not r["injected"]] == [r["source_us"] for r in merged["records"]], "source timestamp substitution")
    life = lanes[2]
    require(len(life) == 2 and [r["purpose"] for r in life] == [0,1] and
            [r["microseconds"] for r in life] == [merged["start_us"], merged["end_us"]], "lifecycle timestamp substitution")
    owner = lanes[0]; pairs = []
    for index, r in enumerate(owner):
        if r["purpose"] == 4:
            require(index > 0 and owner[index-1]["purpose"] == 3, "owner completion without entry")
            pairs.append([owner[index-1]["microseconds"], r["microseconds"]])
    require(pairs == [[r["owner_begin_us"],r["owner_end_us"]] for r in merged["records"]], "owner bracket substitution")
    require(owner[-1]["purpose"] == 6 and [r["microseconds"] for r in owner if r["purpose"] == 5] == [merged["stop_intent_us"]] and
            [r["microseconds"] for r in owner if r["purpose"] == 6] == [merged["close_us"]], "owner lifecycle substitution")
    if injected: require(clock["fault_record"] == source[-1], "terminal read evidence differs")
    else: require(clock["fault_record"] == dict(sequence=0,purpose=0,code=1,error=0,seconds=0,nanoseconds=0,microseconds=0,injected=False), "invented terminal read")
    result.update(case=case, injected_read_error_rejected=injected, lane_read_counts=[len(x) for x in lanes])
    result.pop("restart_rejected", None)
    return result


def check(area):
    area = Path(area).resolve()
    m = read(area / "manifest.json")
    require(m["schema"] == "p3c-native-build/v1" and m["status"] == "captured_unqualified", "incomplete build/run")
    require(sha(area / "source_hashes.json") == m["source_hashes_sha256"], "source registry drift")
    registry = read(area / "source_hashes.json", 16 * 1024 * 1024)
    require(set(m["overlay"]) == OVERLAYS, "overlay inventory")
    for rel, hashes in m["overlay"].items():
        require(registry[rel] == hashes["original"] and sha(area / "source" / rel) == hashes["derived"], "overlay drift")
    require(m["overlay"][PREFIX + "merge/ReceiptMerge.cpp"]["original"] ==
            m["overlay"][PREFIX + "merge/ReceiptMerge.cpp"]["derived"], "merge algorithm changed")
    require(m["overlay"][PREFIX + "provider/CheckedProvider.cpp"]["original"] == m["overlay"][PREFIX + "provider/CheckedProvider.cpp"]["derived"], "checked read conversion changed")
    prefixes = tuple(PREFIX + x + "/" for x in ("native", "merge", "native_merge", "provider", "native_clock"))
    expected = {rel for rel in registry if rel.startswith(prefixes)}
    actual = {str(p.relative_to(area / "inputs")) for p in (area / "inputs").rglob("*") if p.is_file()}
    require(actual == expected and PREFIX + "native_clock/NativeClock.cpp" in expected, "source archive inventory")
    for rel in expected: require(sha(area / "inputs" / rel) == registry[rel], "archived input drift")
    build = area / "build"
    require(sha(build / "bin/px4") == m["executable_sha256"], "executable drift")
    source = area / "source"
    expected_commands = {
        "configure": ["cmake", "-S", str(source), "-B", str(build), "-G", "Ninja", "-DCONFIG=px4_sitl_p3c-default",
                      "-DVENDOR=px4", "-DMODEL=sitl", "-DLABEL=p3c-default", f"-DPX4_BOARD_DIR={source}/boards/px4/sitl",
                      f"-DPX4_CONFIG_FILE={source}/{PREFIX}native/p2b-default.px4board",
                      f"-DEXTERNAL_MODULES_LOCATION={source}/{PREFIX}native_clock/px4_external",
                      "-DCMAKE_BUILD_TYPE=Debug", "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"],
        "build": ["cmake", "--build", str(build), "--target", "px4", "-j", "4"],
    }
    for name in ("configure", "build"):
        require(m[name]["exit_code"] == 0 and m[name]["command"] == expected_commands[name], "build command binding")
        require(sha(area / (name + ".log")) == m[name]["log_sha256"], "build log drift")
    audit = m["configuration_audit"]
    require(sha(build / "boardconfig") == audit["boardconfig_sha256"] and
            sha(build / "compile_commands.json") == audit["compile_commands_sha256"], "configuration drift")
    config = {line.split("=", 1)[0] for line in (build / "boardconfig").read_text().splitlines() if line.endswith("=y")}
    require({c for c in config if c.startswith("CONFIG_MODULES_")} == {"CONFIG_MODULES_EKF2"} and
            not any(c.startswith("CONFIG_DRIVERS_") for c in config), "nonclosed profile")
    require("CONFIG_BOARD_NOLOCKSTEP" in config and "CONFIG_EKF2_MULTI_INSTANCE" not in config, "clock/instance profile")
    wanted = {"ekf2", "research_provenance", "param", "shutdown", "uorb", "ver", "list_tasks", "list_files", "sleep"}
    require(set(re.findall(r'apps\["([a-zA-Z0-9_]+)"\]', (build / "platforms/posix/apps.cpp").read_text())) == wanted, "builtin inventory")
    commands = read(build / "compile_commands.json", 16 * 1024 * 1024)
    for c in commands:
        require(not any(x in c["command"] for x in ("-DENABLE_LOCKSTEP_SCHEDULER", "-DCONFIG_MUORB_APPS_SYNC_TIMESTAMP",
                                                   "RESEARCH_C1E_NEGATIVE", "RESEARCH_SITL_DIAGNOSTIC", "RESEARCH_CLOSED_MERGE_HOST")), "mixed profile")
    for ending in ("/NativeClock.cpp", "/NativeMerge.cpp", "/ReceiptMerge.cpp", "/CheckedProvider.cpp", "/EKF2.cpp", "/research_provenance.cpp"):
        selected = [c for c in commands if c["file"].endswith(ending)]
        require(len(selected) == 1 and "-DRESEARCH_P3C_NATIVE=1" in selected[0]["command"], "missing native typed profile")
    require(len(m["runs"]) == 2 and [r["case"] for r in m["runs"]] == ["normal", "read_error"], "case inventory")
    require(m["runs"][0]["run_id"] != m["runs"][1]["run_id"], "reused process lifetime")
    results = []
    for run in m["runs"]:
        case = run["case"]; runtime = area / ("run-" + case)
        command = run["command"]
        require(len(command) == 7 and command[:2] == [str(build / "bin/px4"), "-i"] and
                command[2].isdigit() and 10000 <= int(command[2]) < 60000 and
                command[3:] == ["-w", str(runtime), "-s", str(runtime / "startup.sh")], "native invocation binding")
        require(run["exit_code"] == 0 and run["forced_shutdown"] is False, "unclean process")
        artifacts = {"startup.sh", "imu.txt", "stdout.log", "merged.json", "clock.json"} | ({"observations.json"} if case == "normal" else set())
        require(set(run["artifacts"]) == artifacts, "case artifact inventory")
        for name, digest in run["artifacts"].items(): require(sha(runtime / name) == digest, "case artifact drift")
        require(sha(runtime / "startup.sh") == registry[PREFIX + "native_clock/" + case + ".sh"], "startup binding")
        require(sha(runtime / "imu.txt") == run["fixture"]["fixture_sha256"] and run["fixture"]["source_sha256"] ==
                "62665d68f92ceb47068a5b5f16a1407998e0f2cf08049e98fc55cf61e90c8143", "fixture binding")
        log = (runtime / "stdout.log").read_text()
        marker = "P3C_CAPTURE_COMPLETE" if case == "normal" else "P3C_EXPECTED_READ_REJECTION"
        require(marker in log and "ERROR [" not in log and "P3C_UNEXPECTED_READ_ACCEPTANCE" not in log, "case completion")
        if case == "read_error": require(not (runtime / "observations.json").exists(), "restart presented as raw success")
        results.append(clock_trace(read(runtime / "clock.json"), read(runtime / "merged.json"), run["run_id"], case,
                             read(runtime / "observations.json") if case == "normal" else None))
    return dict(verified=True, scope="native_checked_receipt_clock_only", cases=results,
                manifest_sha256=sha(area / "manifest.json"), positive_admission=False, clock_map_qualified=False)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("area", type=Path)
    print(json.dumps(check(parser.parse_args().area), indent=2, sort_keys=True))
