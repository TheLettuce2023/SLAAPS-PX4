#!/usr/bin/env python3
"""Private native scheduled output integration. No physical drivers or hardware."""
import argparse
import hashlib
import json
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

import verify_stabilized_observer as observer

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "Tools/research_sitl/stabilized_output"
SCENARIOS = ("revoke", "producer_stall", "consumer_stall", "lost_ack",
             "driver_reject", "slow_driver", "permission_loss", "duplicate",
             "revoke_inflight", "permission_inflight",
             "mapping_change", "routing_loss", "actuator_override",
             "spare_routing", "failsafe_change", "reversal_change",
             "permission_before_write", "revoke_after_completion", "stale_arming", "delayed_prepared",
             "competing_servos", "generic_remap", "wrong_research_route", "spare_research_route",
             "source_restart", "source_withdrawal", "source_before_write", "source_inflight",
             "source_after_completion", "competing_grant", "frame_revision_mismatch",
             "ack_restart", "ack_revision_mismatch", "health_while_waiting",
             "supervisor_withdrawal", "supervisor_restart", "grant_response_restart",
             "selected_health", "selected_manual_loss", "selected_attitude_nan", "selected_source_restart",
             "selected_device_change", "selected_producer_stall", "selected_profile_change", "selected_estimator_regression")


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def record(path, content):
    Path(path).write_text(json.dumps(content, indent=2) + "\n")


def sync(out):
    if (out / "runtime-manifest.json").exists():
        raise RuntimeError("Sealed evidence is immutable")
    source = out / "px4-source"
    paths = {}
    subscription = "platforms/common/uORB/ResearchSubscription.hpp"
    paths[subscription] = subscription
    for relative in ("msg/StabilizedGrantRequest.msg", "msg/StabilizedGrantResponse.msg"):
        paths[relative] = relative
    for name in ("mixer_module.cpp", "mixer_module.hpp", "ResearchOutputTransaction.hpp",
                 "ResearchStabilizedOutput.hpp", "ResearchStabilizedOutput.cpp", "CMakeLists.txt",
                 "output_functions.yaml", "functions/FunctionProviderBase.hpp", "functions/FunctionResearchStabilized.hpp"):
        relative = "src/lib/mixer_module/" + name
        paths[relative] = relative
    for folder in ("src/lib/research_stabilized_manual", "src/modules/research_stabilized_manual"):
        for path in (ROOT / folder).iterdir():
            if path.is_file():
                paths[path.relative_to(ROOT).as_posix()] = path.relative_to(ROOT).as_posix()
    paths["Tools/research_sitl/stabilized_observer/ObserverIntegrationTest.cpp"] = "src/examples/stabilized_observer_test/ObserverIntegrationTest.cpp"
    for path in FIXTURE.iterdir():
        if path.is_file():
            relative = ("msg/" if path.suffix == ".msg" else "src/examples/stabilized_output_test/") + path.name
            paths[path.relative_to(ROOT).as_posix()] = relative
    revision = Path(tempfile.mkdtemp(prefix="source-revision-", dir=out))
    manifest = {}
    for donor, destination in paths.items():
        target = source / destination
        if target.exists():
            backup = revision / destination
            backup.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(target, backup)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / donor, target)
        manifest[donor] = {"destination": destination, "sha256": digest(target)}
    record(revision / "inputs.json", manifest)
    record(out / "runtime-source-inputs.json", manifest)


def prepare(out):
    observer.prepare(out)
    sync(out)
    source = out / "px4-source"
    messages = source / "msg/CMakeLists.txt"
    text = messages.read_text()
    anchor = "\tStabilizedObserverStatus.msg\n"
    assert text.count(anchor) == 1
    messages.write_text(text.replace(anchor, anchor + "\tStabilizedOutputCommand.msg\n\tStabilizedOutputAck.msg\n\tStabilizedGrantRequest.msg\n\tStabilizedGrantResponse.msg\n"))
    board = source / "boards/px4/sitl/default.px4board"
    board.write_text(board.read_text() + "\nCONFIG_EXAMPLES_STABILIZED_OUTPUT_TEST=y\nCONFIG_RESEARCH_STABILIZED_OUTPUT_HOOK=y\n")
    record(out / "runtime-private-patches.json", {
        name: digest(source / name) for name in ("msg/CMakeLists.txt", "boards/px4/sitl/default.px4board")})


def test(out):
    results = []
    binary = out / "native/bin/px4"
    for repeat in range(2):
        for scenario in SCENARIOS:
            work = Path(tempfile.mkdtemp(prefix=f"runtime-{scenario}-{repeat}-", dir=out))
            profile = work / "synthetic.profile"
            subprocess.run([str(out / "debug/stabilized_observer_profile_test"), str(profile)], check=True)
            script = work / "rc.test"
            script.write_text("#!/bin/sh\nset -e\n. px4-alias.sh\nparam select parameters.bson\n"
                              f"stabilized_output_test {profile} {scenario} || true\nshutdown\n")
            log = work / "console.log"
            observer.run([str(binary), "-d", "-s", str(script)], log, cwd=work, timeout=60)
            text = log.read_text()
            match = re.search(r"OUTPUT_RESULT scenario=(\w+) checks=(\d+) failures=(\d+) acks=(\d+)", text)
            capture = re.search(r"OUTPUT_CAPTURE commands=(\d+) fallbacks=(\d+) disarms=(\d+)", text)
            timing = re.search(r"OUTPUT_TIMING last=(\d+) fallback=(\d+)", text)
            passed = bool(match and capture and timing and match[1] == scenario and int(match[3]) == 0 and int(match[4]) >= 20)
            result = {"scenario": scenario, "repeat": repeat, "passed": passed,
                      "binary_sha256": digest(binary), "console_sha256": digest(log),
                      "profile_sha256": digest(profile), "checks": int(match[2]) if match else 0}
            if match and capture and timing:
                result.update(acks=int(match[4]), commands=int(capture[1]), fallbacks=int(capture[2]), disarms=int(capture[3]),
                              last_command_us=int(timing[1]), first_fallback_us=int(timing[2]))
            record(work / "result.json", result)
            results.append(str(work.relative_to(out)))
            print(f"{scenario} repeat={repeat}: {'PASS' if passed else 'FAIL'}", flush=True)
            if not passed:
                print(log.read_text()[-7000:], flush=True)
                raise RuntimeError(f"Failed native fixture: {work}")
    record(out / "accepted-runtime-results.json", results)
    observer.test(out)  # Existing output-disabled observer on this same binary.


def seal(out):
    assert not (out / "runtime-manifest.json").exists()
    source = out / "px4-source"
    bindings = json.loads((out / "runtime-source-inputs.json").read_text())
    for donor, entry in bindings.items():
        assert digest(ROOT / donor) == digest(source / entry["destination"]) == entry["sha256"], donor
    patches = json.loads((out / "runtime-private-patches.json").read_text())
    for name, expected in patches.items():
        assert digest(source / name) == expected, name
    inherited = json.loads((out / "arm-overlay-inputs.json").read_text())
    # Bind the actual inherited mixer/manual/observer implementation too, not
    # only the newly added fixture. Explicit private patches supersede originals.
    actual_inputs = dict(inherited)
    actual_inputs.update(patches)
    actual_inputs.update({entry["destination"]: entry["sha256"] for entry in bindings.values()})
    for name, expected in actual_inputs.items():
        assert digest(source / name) == expected, name
    binary_hash = digest(out / "native/bin/px4")
    cases = json.loads((out / "accepted-runtime-results.json").read_text())
    assert len(cases) == 2 * len(SCENARIOS)
    covered = set()
    for case in cases:
        result = json.loads((out / case / "result.json").read_text())
        covered.add((result["scenario"], result["repeat"]))
        assert result["passed"] and result["binary_sha256"] == binary_hash
        assert digest(out / case / "console.log") == result["console_sha256"]
        assert digest(out / case / "synthetic.profile") == result["profile_sha256"]
    assert covered == {(scenario, repeat) for scenario in SCENARIOS for repeat in range(2)}
    host_results = {}
    for label in ("debug", "release", "sanitize"):
        log = max(out.glob(label + "-test.log*"), key=lambda p: p.stat().st_mtime_ns)
        assert "100% tests passed" in log.read_text(), label
        output_result = re.search(r"Stabilized output: (\d+) checks, 0 failures", log.read_text())
        assert output_result and int(output_result[1]) >= 2847
        session_result = re.search(r"Stabilized session: (\d+) checks, 0 failures", log.read_text())
        assert session_result and int(session_result[1]) >= 164
        host_results[label] = {"log": log.name, "output_checks": int(output_result[1]), "session_checks": int(session_result[1])}
    observation = list(out.glob("observation-*/result.json"))
    assert any(json.loads(p.read_text())["px4_sha256"] == binary_hash for p in observation)
    symbols = subprocess.check_output(["nm", "-C", str(out / "native/bin/px4")], text=True)
    assert "StabilizedOutputDriver::dispatch" in symbols and "stabilized_output_test_main" in symbols
    assert "ResearchStabilizedOutput::validate" in symbols and "ResearchStabilizedOutput::complete" in symbols
    assert "FunctionResearchStabilized" in symbols
    assert "StabilizedManualSession::update" in symbols
    assert "research_bench_control_main" not in symbols
    assert "#define CONFIG_RESEARCH_STABILIZED_OUTPUT_HOOK 1" in (out / "native/px4_boardconfig.h").read_text()
    snapshot = out / "verified-runtime-source"
    for name in actual_inputs:
        target = snapshot / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source / name, target)
    record(out / "verified-runtime-inputs.json", actual_inputs)
    shutil.copy2(__file__, out / Path(__file__).name)
    shutil.copy2(observer.__file__, out / Path(observer.__file__).name)
    files = list(out.glob("*.json")) + list(out.glob("*.log*")) + [out / Path(__file__).name]
    files += [out / Path(observer.__file__).name]
    files += list(snapshot.rglob("*"))
    files += [out / "native/bin/px4", out / "native/compile_commands.json", out / "native/px4_boardconfig.h"]
    for folder in out.glob("runtime-*"):
        if folder.is_dir():
            files += list(folder.iterdir())
    for revision in out.glob("source-revision-*"):
        files += list(revision.rglob("*"))
    for folder in out.glob("observation-*"):
        files += list(folder.iterdir())
    for label in ("debug", "release", "sanitize"):
        files += [out / label / name for name in
                  ("stabilized_manual_test", "stabilized_observer_profile_test", "stabilized_output_test",
                   "stabilized_manual_session_test")]
    manifest = {"scope": "native_work_queues_uORB_actual_mixer_memory_sink_only", "native_sha256": binary_hash,
                "cases": cases, "host_results": host_results, "hardware_operated": False, "production_driver_integrated": False,
                "production_mixer_hook_exercised": True,
                "reusable_mixer_adapter_exercised": True, "main_bank_channels": 8,
                "dedicated_prepared_candidate_provider": True, "generic_servos_used_as_research_source": False,
                "command_source_lifecycle_bound": True, "publisher_authenticated": False,
                "controller_session_and_ack_lifecycle_bound": True,
                "coherent_grant_handshake_and_supervisor_lifecycle": True,
                "selected_input_bound_at_both_endpoints": True,
                "physical_feedback": False, "files": {
                    p.relative_to(out).as_posix(): digest(p) for p in sorted(set(files)) if p.is_file()}}
    record(out / "runtime-manifest.json", manifest)
    print("Sealed", digest(out / "runtime-manifest.json"), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("prepare", "sync", "host", "native", "test", "seal"))
    parser.add_argument("--evidence", type=Path, required=True)
    args = parser.parse_args()
    out = args.evidence.resolve()
    if out.parent != Path("/var/tmp") or not out.name.startswith("research-stabilized-output-native-"):
        raise RuntimeError("Fresh private runtime evidence directory required")
    if args.action in ("host", "native"):
        getattr(observer, args.action)(out)
    else:
        globals()[args.action](out)


if __name__ == "__main__":
    main()
