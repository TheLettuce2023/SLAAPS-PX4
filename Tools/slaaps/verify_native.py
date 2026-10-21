#!/usr/bin/env python3
"""Exercise native SLAAPS registration and explicit configuration application."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=Path)
    args = parser.parse_args()
    build = args.build.resolve()
    receipt = json.loads((build / "result.json").read_text())
    manifest = json.loads((build / "source-manifest.json").read_text())
    assert manifest.get("config") == "px4_sitl_slaaps-test", "No-plant runtime tests require the wall-clock variant"
    assert hashlib.sha256((build / "source-manifest.json").read_bytes()).hexdigest() == receipt["source_manifest_sha256"]
    binary = build / "build/bin/px4"
    assert hashlib.sha256(binary.read_bytes()).hexdigest() == receipt["binary_sha256"]
    out = Path(tempfile.mkdtemp(prefix="slaaps-native-runtime-", dir="/var/tmp"))
    print(f"Evidence: {out}", flush=True)
    instance = os.getpid()
    script = out / "startup.sh"
    script.write_text("#!/bin/sh\nset -e\n. px4-alias.sh\nparam set SYS_AUTOSTART 22001\nparam set MAV_TYPE 1\n"
                      "slaaps_control_test inputs\nslaaps_control_test\ncommander start\nslaaps start\necho SLAAPS_TEST_STARTED\n")
    log = (out / "px4.log").open("w")
    process = subprocess.Popen([str(binary), "-d", "-i", str(instance), "-w", str(out),
                                "-s", str(script)], cwd=out, stdout=log, stderr=subprocess.STDOUT)
    commands = []

    def client(module, *arguments, check=True):
        assert process.poll() is None, "PX4 exited"
        result = subprocess.run([str(binary.parent / ("px4-" + module)), "--instance", str(instance),
                                 *map(str, arguments)], cwd=out, capture_output=True, text=True, timeout=10)
        output = result.stdout + result.stderr
        commands.append(dict(command=[module, *arguments], code=result.returncode, output=output))
        if check:
            assert result.returncode == 0, output
        return output

    def wait_status(pattern):
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            output = client("slaaps", "status", check=False)
            if re.search(pattern, output):
                return output
            time.sleep(.15)
        raise AssertionError(f"Missing status {pattern}: {output}")

    result = dict(passed=False, scope="native_registration_and_parameter_apply_no_outputs",
                  build=str(build), binary_sha256=receipt["binary_sha256"],
                  harness_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
    try:
        deadline = time.monotonic() + 15
        while "SLAAPS_TEST_STARTED" not in (out / "px4.log").read_text():
            assert process.poll() is None and time.monotonic() < deadline, "Startup failed"
            time.sleep(.1)
        input_match = re.search(r"SLAAPS_INPUT_TEST_PASS checks=(\d+)", (out / "px4.log").read_text())
        assert input_match and int(input_match.group(1)) >= 30, "Selected input regression suite missing"
        result["native_input_checks"] = int(input_match.group(1))
        control_output = (out / "px4.log").read_text()
        control_match = re.search(r"SLAAPS_CONTROL_TEST_PASS checks=(\d+)",
                                  control_output + (out / "px4.log").read_text())
        assert control_match and int(control_match.group(1)) >= 2721, control_output
        result["native_control_checks"] = int(control_match.group(1))
        result["native_control_scope"] = "native_selected_worker_and_bank_transport_synthetic_no_physical_driver"
        initial = wait_status(r"configured=1")
        assert "configuration valid=0" in initial
        endpoint = client("slaaps_control_test", "endpoint")
        endpoint_match = re.search(r"SLAAPS_ENDPOINT_TEST_PASS revision=(\d+) checks=5", endpoint)
        assert endpoint_match, endpoint
        endpoint_revision = int(endpoint_match.group(1))
        modes = re.search(r"manual=(\d+) acceleration=(\d+) bench=(\d+)", initial).groups()
        assert len(set(modes)) == 3
        status = client("listener", "slaaps_status", "1")
        assert re.search(r"can_run_mask:\s+0\b", status), status
        assert re.search(r"input_metadata_valid:\s+(?:False|false|0)\b", status), status
        assert re.search(r"input_source_epoch:\s+0\b", status), status
        assert re.search(r"can_arm_mask:\s+0\b", status), status
        values = {"SLA_DATA_REV": 1, "SLA_IN_INST": 2, "SLA_IN_SYS": 255, "SLA_IN_COMP": 190,
                  "SLA_BENCH_GAP": 100, "SLA_OUT_LEASE": 100, "SLA_STK_ENTER": .04,
                  "SLA_BENCH_DZ": .03}
        signs = ((1, -1, 1, -1), (1, 1, -1, -1), (1, -1, -1, 1))
        for index in range(4):
            for suffix, value in {"PWM_NEG": 1100, "PWM_CTR": 1500, "PWM_POS": 1900,
                                  "DEF_MAX": 13, "RISE": 5, "FALL": 4}.items():
                values[f"SLA_C{index + 1}_{suffix}"] = value
            for axis, sign in zip("RPY", signs):
                values[f"SLA_C{index + 1}_BM{axis}"] = sign[index]
        for name, value in values.items():
            client("param", "set", name, str(value))
        client("param", "set", "SLA_CFG_APPLY", "1")
        wait_status(r"configuration valid=1 use=0 apply=1")
        client("param", "set", "SLA_C1_PWM_CTR", "1501")
        wait_status(r"configuration valid=0")
        client("param", "set", "SLA_C1_PWM_CTR", "1500")
        time.sleep(.3)
        assert "configuration valid=0" in client("slaaps", "status")
        client("param", "set", "SLA_CFG_APPLY", "2")
        wait_status(r"configuration valid=1 use=0 apply=2")
        client("param", "set", "SLA_CFG_KIND", "1")
        client("param", "set", "SLA_CFG_APPLY", "3")
        wait_status(r"configuration valid=0 use=1 apply=3")
        client("param", "set", "SLA_CFG_KIND", "0")
        client("param", "set", "SLA_CFG_APPLY", "4")
        wait_status(r"configuration valid=1 use=0 apply=4")
        client("slaaps_routing_test", "valid")
        wait_status(r"routing snapshot=1 inventory_fault=0 routing_fault=0")
        for scenario in ("duplicate", "unavailable", "calibration", "stale"):
            client("slaaps_routing_test", scenario)
            wait_status(r"routing snapshot=0")
            client("slaaps_routing_test", "valid")
            wait_status(r"routing snapshot=1 inventory_fault=0 routing_fault=0")
        time.sleep(1.1)
        wait_status(r"routing snapshot=0 inventory_fault=3")
        for _ in range(3):
            client("slaaps", "stop")
            client("slaaps", "start")
            restarted = wait_status(r"configured=1")
            assert "configuration valid=0" in restarted, "Saved apply counter must not auto-apply"
            endpoint = client("slaaps_control_test", "endpoint")
            endpoint_match = re.search(r"SLAAPS_ENDPOINT_TEST_PASS revision=(\d+) checks=5", endpoint)
            assert endpoint_match and int(endpoint_match.group(1)) != endpoint_revision, endpoint
            endpoint_revision = int(endpoint_match.group(1))
            new_modes = re.search(r"manual=(\d+) acceleration=(\d+) bench=(\d+)", restarted).groups()
            assert new_modes == modes, "Repeated lifecycle must release and reuse the registered slots"
        result.update(passed=True, supervisor_endpoint_lifecycles=4, modes=list(modes), physical_outputs=False, qgc_gui_verified=False)
    finally:
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)
        log.close()
        (out / "commands.json").write_text(json.dumps(commands, indent=2) + "\n")
        (out / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result), flush=True)


if __name__ == "__main__":
    main()
