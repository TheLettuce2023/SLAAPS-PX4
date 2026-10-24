#!/usr/bin/env python3
"""Exercise SLAAPS supervisor and actual mixer handling with capture-only outputs."""

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import socket
import subprocess
import sys
import tempfile
import time


def verify_companion_transport(client, wait_status, out, build):
    """Exercise the actual loopback parser and supervisor with a synthetic key.

    Receipt evidence only: no clock/session grant, command admission or output.
    The caller owns an isolated native process and its private storage directory.
    """
    source = build / "source"
    mavlink = source / "src/modules/mavlink/mavlink"
    generated = out / "companion_dialect.py"
    with (out / "companion-mavgen.log").open("w") as log:
        subprocess.run([sys.executable, str(mavlink / "pymavlink/tools/mavgen.py"),
                        "--lang=Python3", "--wire-protocol=2.0", "--output", str(generated),
                        str(source / "src/modules/mavlink/slaaps.xml")],
                       env=dict(os.environ, PYTHONPATH=str(mavlink)), stdout=log, stderr=subprocess.STDOUT, check=True)
    spec = importlib.util.spec_from_file_location("slaaps_native_companion_dialect", generated)
    dialect = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(dialect)
    key = bytes(range(1, 33))  # Public synthetic key, never deployment material.
    sender = dialect.MAVLink(None, srcSystem=42, srcComponent=191)
    sender.signing.secret_key = key
    sender.signing.link_id = 19
    sender.signing.timestamp = 100000000
    sender.signing.sign_outgoing = True
    request_sequence = 0
    apply = 105  # Previous commissioning lifecycle's completed apply counter.

    def request(**changes):
        nonlocal request_sequence
        request_sequence += 1
        fields = dict(source_id=1, session_id=1, sequence=request_sequence,
                      producer_monotonic_us=1, producer_expiry_monotonic_us=2,
                      controller_instance_id=1, transition_epoch=1, clock_mapping_epoch=1,
                      acceleration_x_frd_m_s2=0., acceleration_y_frd_m_s2=0., acceleration_z_frd_m_s2=0.,
                      protocol_version=0x10000, target_system=1, target_component=1,
                      frame=1, quantity=1, reference_point=1, flags=0, reserved=[0] * 4)
        fields.update(changes)
        return dialect.MAVLink_research_acceleration_request_message(**fields)

    def reapply():
        nonlocal apply
        apply += 1
        client("param", "set", "SLA_CFG_APPLY", str(apply))
        wait_status(rf"configuration valid=1 use=2 apply={apply}\b")
        wait_status(r"companion transport=2\b")

    def event_sequence():
        status = client("slaaps", "status")
        return int(re.search(r"companion transport=\d+ receiver=\d+ event=(\d+)", status).group(1))

    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as peer:
        peer.bind(("127.0.0.1", 0))
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as reservation:
            reservation.bind(("127.0.0.1", 0))
            port = reservation.getsockname()[1]
        destination = ("127.0.0.1", port)

        def send(message):
            frame = message.pack(sender)
            peer.sendto(frame, destination)
            return frame

        # The receiver constructs a mission manager. Supply its normal dataman
        # dependency so startup does not time out waiting for a client ID.
        client("dataman", "start", "-r")
        try:
            client("mavlink", "start", "-u", str(port), "-o", str(peer.getsockname()[1]),
                   "-t", "127.0.0.1", "-m", "onboard", "-r", "10000")
            for name, value in {"SLA_CMP_EN": 1, "SLA_CMP_INST": 0, "SLA_CMP_SYS": 42,
                                "SLA_CMP_COMP": 191, "SLA_CMP_SOURCE": 1, "SLA_CMP_AGE": 1000000}.items():
                client("param", "set", name, str(value))
            reapply()
            send(request())
            wait_status(r"companion transport=3\b")  # Signed bit without a verifier is insufficient.
            sender.signing.sign_outgoing = False
            send(dialect.MAVLink_setup_signing_message(1, 1, key, sender.signing.timestamp))
            sender.signing.sign_outgoing = True
            wait_status(r"companion transport=9\b")
            reapply()
            valid_frame = send(request())
            wait_status(r"companion transport=7\b")
            accepted_event = event_sequence()
            peer.sendto(valid_frame, destination)
            wait_status(r"companion transport=8\b")
            assert event_sequence() == accepted_event, "Signature replay reached the receipt queue"
            sender.signing.sign_outgoing = False
            send(request())
            time.sleep(.15)
            assert event_sequence() == accepted_event, "Unsigned private command passed signed-link parsing"
            sender.signing.sign_outgoing = True
            sender.signing.secret_key = bytes(reversed(key))
            send(request())
            time.sleep(.15)
            assert event_sequence() == accepted_event, "Wrong-key private command reached the receipt queue"
            sender.signing.secret_key = key
            send(request(source_id=2))
            wait_status(r"companion transport=4\b")
            send(request(target_system=2))
            wait_status(r"companion transport=5\b")
            sender.srcSystem = 43
            send(request())
            wait_status(r"companion transport=4\b")
            sender.srcSystem = 42
            send(request())
            wait_status(r"companion transport=7\b")
            send(dialect.MAVLink_setup_signing_message(1, 1, key, sender.signing.timestamp))
            wait_status(r"companion transport=9\b")
            send(request())
            time.sleep(.15)
            assert "companion transport=9 " in client("slaaps", "status"), "Key reload automatically recovered"
            reapply()
            send(request())
            wait_status(r"companion transport=7\b")
            wire = client("listener", "slaaps_status", "1")
            assert re.search(r"can_arm_mask:\s+0\b", wire), wire
            assert re.search(r"manual_worker_active:\s+(?:False|false|0)\b", wire), wire
            assert re.search(r"bench_worker_active:\s+(?:False|false|0)\b", wire), wire
            return dict(passed=True, actual_mavlink_parser=True, actual_supervisor=True,
                        synthetic_key_and_identity=True, loopback_only=True, signature_replay_rejected=True,
                        unsigned_and_wrong_key_rejected=True, source_target_binding=True,
                        receipt_expiry=True, signing_reload_requires_apply=True,
                        command_admission=False, acceleration_control=False, physical_outputs=False)
        finally:
            client("mavlink", "stop-all", check=False)
            client("dataman", "stop", check=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=Path)
    parser.add_argument("--supervisor-only", action="store_true",
                        help="Run only the isolated actual-source supervisor regression for focused diagnosis")
    args = parser.parse_args()
    build = args.build.resolve()
    receipt = json.loads((build / "result.json").read_text())
    manifest = json.loads((build / "source-manifest.json").read_text())
    assert manifest.get("config") == "px4_sitl_slaaps-test", "No-plant runtime tests require the wall-clock variant"
    assert hashlib.sha256((build / "source-manifest.json").read_bytes()).hexdigest() == receipt["source_manifest_sha256"]
    binary = build / "build/bin/px4"
    assert hashlib.sha256(binary.read_bytes()).hexdigest() == receipt["binary_sha256"]
    compilation = json.loads((build / "build/compile_commands.json").read_text())
    supervisor_commands = [item["command"] for item in compilation
                           if item["file"].endswith("/src/modules/slaaps/Slaaps.cpp")]
    fixture_commands = [item["command"] for item in compilation
                        if item["file"].endswith("/Tools/slaaps/configuration/VehicleConfigurationTest.cpp")]
    assert len(supervisor_commands) == len(fixture_commands) == 1
    assert "SLAAPS_SUPERVISOR_TEST_ACCESS" not in supervisor_commands[0], "Production supervisor has test admission seam"
    assert "SLAAPS_SUPERVISOR_TEST_ACCESS" in fixture_commands[0], "Isolated actual-source fixture missing"
    out = Path(tempfile.mkdtemp(prefix="slaaps-native-runtime-", dir="/var/tmp"))
    print(f"Evidence: {out}", flush=True)
    instance = os.getpid()
    # Synthetic registration/authority topics retain their last payload after
    # publisher destruction. A separate process prevents those payloads from
    # becoming inputs to the subsequent real Commander lifecycle checks.
    supervisor_dir = out / "supervisor"
    supervisor_dir.mkdir()
    supervisor_script = supervisor_dir / "startup.sh"
    supervisor_script.write_text("#!/bin/sh\nset -e\n. px4-alias.sh\n"
                                 "param set SYS_AUTOSTART 22001\nparam set MAV_TYPE 1\n"
                                 "slaaps_control_test supervisor\necho SLAAPS_SUPERVISOR_FINISHED\n")
    supervisor_result = dict(passed=False, isolated_process=True, synthetic_authority=True,
                             physical_outputs=False, binary_sha256=receipt["binary_sha256"])
    with (supervisor_dir / "px4.log").open("w") as supervisor_log:
        supervisor_process = subprocess.Popen(
            [str(binary), "-d", "-i", str(instance + 1000000), "-w", str(supervisor_dir),
             "-s", str(supervisor_script)], cwd=supervisor_dir,
            stdout=supervisor_log, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 30
            while True:
                supervisor_output = (supervisor_dir / "px4.log").read_text()
                if "SLAAPS_SUPERVISOR_FINISHED" in supervisor_output:
                    break
                assert supervisor_process.poll() is None and time.monotonic() < deadline, "Isolated supervisor failed"
                time.sleep(.1)
            supervisor_match = re.search(r"SLAAPS_SUPERVISOR_TEST_PASS checks=(\d+)", supervisor_output)
            assert supervisor_match, "Actual-source supervisor activation regression missing"
            supervisor_result.update(passed=True, checks=int(supervisor_match.group(1)))
        finally:
            if supervisor_process.poll() is None:
                supervisor_process.terminate()
                try:
                    supervisor_process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    supervisor_process.kill()
                    supervisor_process.wait(timeout=5)
            (supervisor_dir / "result.json").write_text(json.dumps(supervisor_result, indent=2) + "\n")
    if args.supervisor_only:
        print(json.dumps(supervisor_result), flush=True)
        return
    script = out / "startup.sh"
    script.write_text("#!/bin/sh\nset -e\n. px4-alias.sh\nparam set SYS_AUTOSTART 22001\nparam set MAV_TYPE 1\n"
                      "slaaps_control_test inputs\nslaaps_control_test\nslaaps_control_test output\n"
                      "param reset_all\nparam set SYS_AUTOSTART 22001\nparam set MAV_TYPE 1\n"
                      "commander start\nslaaps start\necho SLAAPS_TEST_STARTED\n")
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

    result = dict(passed=False, scope="native_supervisor_and_mixer_capture_only_no_hardware",
                  build=str(build), binary_sha256=receipt["binary_sha256"],
                  production_supervisor_test_seam=False,
                  harness_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
    try:
        deadline = time.monotonic() + 30
        while "SLAAPS_TEST_STARTED" not in (out / "px4.log").read_text():
            assert process.poll() is None and time.monotonic() < deadline, "Startup failed"
            time.sleep(.1)
        input_match = re.search(r"SLAAPS_INPUT_TEST_PASS checks=(\d+)", (out / "px4.log").read_text())
        assert input_match and int(input_match.group(1)) >= 30, "Selected input regression suite missing"
        result["native_input_checks"] = int(input_match.group(1))
        control_output = (out / "px4.log").read_text()
        control_match = re.search(r"SLAAPS_CONTROL_TEST_PASS checks=(\d+)",
                                  control_output + (out / "px4.log").read_text())
        assert control_match and int(control_match.group(1)) >= 2751, control_output
        result["native_control_checks"] = int(control_match.group(1))
        output_match = re.search(r"SLAAPS_MIXING_OUTPUT_TEST_PASS checks=(\d+)", control_output)
        assert output_match, "Real mixer output path regression missing"
        result["native_mixer_output_checks"] = int(output_match.group(1))
        result["native_supervisor_checks"] = supervisor_result["checks"]
        result["native_supervisor_isolated_process"] = True
        result["native_supervisor_scope"] = "actual_source_synthetic_authority_capture_only_no_hardware"
        result["native_mixer_output_scope"] = "actual_worker_and_mixer_synthetic_authority_capture_only_no_hardware"
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
        values = {"SLA_DATA_REV": 1, "SLA_IN_SOURCE": 2, "SLA_IN_INST": 2, "SLA_IN_SYS": 255, "SLA_IN_COMP": 190,
                  "SLA_IN_AGE": 100, "SLA_BENCH_GAP": 100, "SLA_OUT_LEASE": 100, "SLA_STK_ENTER": .04,
                  "SLA_BENCH_DZ": .03}
        for index, axis in enumerate("RPY"):
            values[f"SLA_{axis}_INPUT"] = index
            values[f"SLA_{axis}_SIGN"] = 1
        signs = ((1, -1, 1, -1), (1, 1, -1, -1), (1, -1, -1, 1))
        for index in range(4):
            for suffix, value in {"PWM_NEG": 1100, "PWM_CTR": 1500, "PWM_POS": 1900,
                                  "DEF_MAX": 13, "RISE": 5, "FALL": 4, "MIN": -1, "MAX": 1}.items():
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
        accepted_parameters = out / "accepted-configuration.bson"
        client("param", "save", str(accepted_parameters))
        assert accepted_parameters.is_file() and accepted_parameters.stat().st_size > 0
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
            assert "configuration valid=1 use=0 apply=4" in restarted, "Exact accepted profile must restore disarmed"
            endpoint = client("slaaps_control_test", "endpoint")
            endpoint_match = re.search(r"SLAAPS_ENDPOINT_TEST_PASS revision=(\d+) checks=5", endpoint)
            assert endpoint_match and int(endpoint_match.group(1)) != endpoint_revision, endpoint
            endpoint_revision = int(endpoint_match.group(1))
            new_modes = re.search(r"manual=(\d+) acceleration=(\d+) bench=(\d+)", restarted).groups()
            assert new_modes == modes, "Repeated lifecycle must release and reuse the registered slots"
        # PX4 persists individual edits, so the counter alone must never admit
        # an edited profile. A torn acceptance-marker write must also fail closed.
        client("param", "set", "SLA_C1_PWM_CTR", "1501")
        wait_status(r"configuration valid=0")
        client("slaaps", "stop")
        client("slaaps", "start")
        assert "configuration valid=0" in wait_status(r"configured=1"), "Unapplied persisted edit restored"
        client("param", "set", "SLA_C1_PWM_CTR", "1500")
        client("param", "set", "SLA_CFG_APPLY", "5")
        wait_status(r"configuration valid=1 use=0 apply=5")
        client("param", "set", "SLA_CFG_SAVED", "0")
        client("slaaps", "stop")
        client("slaaps", "start")
        assert "configuration valid=0" in wait_status(r"configured=1"), "Partial acceptance marker restored"
        client("slaaps", "stop")
        client("param", "set", "SLA_C1_PWM_CTR", "1502")
        client("param", "load", str(accepted_parameters))
        client("slaaps", "start")
        assert "configuration valid=1 use=0 apply=4" in wait_status(r"configured=1"), "Exported accepted profile did not restore"
        # Real Commander owns arming and mode intent. Synthetic selected inputs
        # supply estimator/manual and labeled battery topics; capture-only drivers never actuate.
        client("slaaps_routing_test", "stop")
        client("slaaps_control_test", "mixing", "start")
        wait_status(r"configuration valid=1 use=1 apply=100")
        # Real Commander republishes unchanged arming state at 2 Hz. The
        # isolated authority fixture publishes at 1 kHz; its 100 ms age bound
        # is intentionally unsuitable here. Configure this disarmed scenario
        # through the same explicit parameter/apply path, not a source default.
        client("param", "set", "SLA_ARM_AGE", "1000000")
        for name, value in {"SLA_PWR_INST": 0, "SLA_PWR_ID": 1, "SLA_PWR_SRC": 0,
                            "SLA_PWR_AGE": 100000, "SLA_PWR_VMIN": 20, "SLA_PWR_VMAX": 26}.items():
            client("param", "set", name, str(value))
        client("param", "set", "SLA_CFG_APPLY", "101")
        wait_status(r"configuration valid=1 use=1 apply=101")
        assert int(modes[0]) == 23, "Isolated fixture must register Manual in external slot 1"
        client("commander", "mode", "manual")
        inactive = wait_status(r"manual entry=0\b")
        assert "manual preparation=0" in inactive, "Apply alone began preparation"
        client("commander", "mode", "ext1")
        wait_status(r"manual entry=(?:3|6|14)\b")  # No selected input or propulsion battery yet.
        client("commander", "mode", "manual")
        wait_status(r"manual entry=0\b")
        client("slaaps_control_test", "selected", "start")
        client("slaaps_control_test", "selected", "offset")
        client("slaaps_control_test", "selected", "power-fault")
        client("commander", "mode", "ext1")
        wait_status(r"manual entry=14\b")
        wait_status(r"power required=1 ready=0 status=7\b")
        client("slaaps_control_test", "selected", "power-valid")
        wait_status(r"manual entry=4\b")
        client("slaaps_control_test", "selected", "throttle-high")
        wait_status(r"manual entry=5\b")
        client("slaaps_control_test", "selected", "invalid-state")
        wait_status(r"manual entry=6\b")
        client("slaaps_control_test", "selected", "wrong-source")
        wait_status(r"manual entry=3\b")
        client("slaaps_control_test", "selected", "neutral")
        prepared = wait_status(r"manual preparation=2 session=\d+ banks=3")
        first_session = int(re.search(r"manual preparation=2 session=(\d+)", prepared).group(1))
        for bank in ("main", "aux"):
            ready_wire = client("listener", "slaaps_manual_ready_" + bank, "1")
            assert re.search(r"session:\s+" + str(first_session) + r"\b", ready_wire), ready_wire
        prepared_wire = client("listener", "slaaps_status", "1")
        assert re.search(r"can_run_mask:\s+0\b", prepared_wire), prepared_wire
        assert re.search(r"can_arm_mask:\s+0\b", prepared_wire), prepared_wire
        authority = client("listener", "vehicle_status", "1")
        assert re.search(r"arming_state:\s+1\b", authority), authority
        assert re.search(r"nav_state_user_intention:\s+23\b", authority), authority
        # An accepted synthetic profile and prepared outputs must not bypass
        # the normal supervisor's missing measured/recovery admission.
        before_arm = int(re.search(r"timestamp:\s+(\d+)", authority).group(1))
        client("commander", "arm")
        arm_deadline = time.monotonic() + 2
        while True:
            acknowledgment = client("listener", "vehicle_command_ack", "1")
            ack_time = re.search(r"timestamp:\s+(\d+)", acknowledgment)
            if (re.search(r"command:\s+400\b", acknowledgment) and ack_time
                    and int(ack_time.group(1)) > before_arm):
                assert re.search(r"result:\s+[12]\b", acknowledgment), acknowledgment
                break
            assert time.monotonic() < arm_deadline, "Commander did not acknowledge ordinary arming"
            time.sleep(.02)
        authority = client("listener", "vehicle_status", "1")
        assert re.search(r"arming_state:\s+1\b", authority), authority
        denied = client("listener", "slaaps_manual_permission", "1")
        for field in ("flight_admitted", "healthy", "measured_condition_valid"):
            assert re.search(rf"{field}:\s+(?:False|false|0)\b", denied), denied
        denied_status = client("listener", "slaaps_status", "1")
        for field in ("can_run_mask", "can_arm_mask"):
            assert re.search(rf"{field}:\s+0\b", denied_status), denied_status
        assert re.search(r"manual_worker_active:\s+(?:False|false|0)\b", denied_status), denied_status
        output_status = client("slaaps_control_test", "mixing", "status")
        assert re.search(r"nonzero=0\b", output_status), output_status
        client("slaaps_control_test", "selected", "stop")
        wait_status(r"manual entry=9\b")
        wait_status(r"manual preparation=3")
        client("slaaps_control_test", "selected", "resume")
        time.sleep(.15)
        assert "manual entry=9" in client("slaaps", "status"), "Fresh inputs silently revived rejected entry"

        def reenter_manual():
            client("commander", "mode", "manual")
            wait_status(r"manual entry=0\b")
            client("commander", "mode", "ext1")
            return wait_status(r"manual preparation=2 session=\d+ banks=3")

        prepared = reenter_manual()
        second_session = int(re.search(r"manual preparation=2 session=(\d+)", prepared).group(1))
        assert second_session > first_session, "Mode reentry reused old session"
        client("slaaps_control_test", "selected", "replace")
        wait_status(r"manual entry=9\b")
        time.sleep(.15)
        assert "manual entry=9" in client("slaaps", "status"), "Publisher replacement silently rebound"
        reenter_manual()
        client("param", "set", "SLA_C1_PWM_CTR", "1501")
        wait_status(r"configuration valid=0")
        wait_status(r"manual preparation=3")
        client("param", "set", "SLA_C1_PWM_CTR", "1500")
        client("param", "set", "SLA_CFG_APPLY", "102")
        wait_status(r"configuration valid=1 use=1 apply=102")
        assert "manual entry=9" in client("slaaps", "status"), "Apply silently retried rejected entry"
        for bank in ("main", "aux"):
            cancelled_wire = client("listener", "slaaps_manual_ready_" + bank, "1")
            assert re.search(r"session:\s+0\b", cancelled_wire), cancelled_wire
        reenter_manual()
        client("param", "set", "PWM_AUX_CENT8", "1501")
        wait_status(r"manual preparation=3")
        client("param", "set", "PWM_AUX_CENT8", "1500")
        time.sleep(.15)
        assert "manual entry=9" in client("slaaps", "status"), "Routing repair silently reprepared"
        reenter_manual()
        wait_status(r"manual preparation=3")  # Original proposal expiry is immutable.
        time.sleep(.15)
        assert "manual entry=9" in client("slaaps", "status"), "Expired preparation automatically retried"
        reenter_manual()
        output_status = client("slaaps_control_test", "mixing", "status")
        assert re.search(r"nonzero=0\b", output_status), output_status
        client("commander", "mode", "manual")
        wait_status(r"manual entry=0\b")

        # Exercise the normal production module with real Commander authority.
        # These are explicit synthetic calibration/timing values, selected raw
        # input and capture-only output devices; no test permission is supplied.
        client("slaaps_control_test", "selected", "neutral")
        client("slaaps_control_test", "selected", "power-drop")
        bench_values = {"SLA_CFG_KIND": 0, "SLA_BENCH_EN": 0, "SLA_BENCH_CH": 1,
                        "SLA_BENCH_SEC": 5, "SLA_IN_AGE": 500, "SLA_STATUS_AGE": 1000000,
                        "SLA_ARM_AGE": 1000000, "SLA_BENCH_GAP": 200, "SLA_OUT_LEASE": 200,
                        "SLA_ACT_ACK_US": 60000, "SLA_FIRST_CMD_US": 80000,
                        "COM_PREARM_MODE": 2}  # Commander enum: Always prearmed; still disarmed.
        for name, value in bench_values.items():
            client("param", "set", name, str(value))
        client("param", "set", "SLA_CFG_APPLY", "103")
        wait_status(r"configuration valid=1 use=0 apply=103")
        wait_status(r"flight unavailable=4\b")
        bench_id = int(modes[2])
        assert 23 <= bench_id <= 30, modes
        bench_mask = 1 << bench_id
        available = client("listener", "slaaps_status", "1")
        assert re.search(rf"can_run_mask:\s+{bench_mask}\b", available), available
        assert re.search(r"can_arm_mask:\s+0\b", available), available
        before_bench = client("slaaps_control_test", "mixing", "status")
        before_counts = tuple(map(int, re.search(r"main_writes=(\d+) aux_writes=(\d+) nonzero=(\d+)",
                                                before_bench).groups()))
        for invalid_mode in ("ext0", "ext9", "ext3x"):
            rejected_mode = client("commander", "mode", invalid_mode, check=False)
            assert commands[-1]["code"] != 0 and "unsupported" in rejected_mode, rejected_mode
        client("commander", "mode", f"ext{bench_id - 22}")
        wait_status(r"bench entry=6 active=0\b")
        authority = client("listener", "vehicle_status", "1")
        for field, value in (("arming_state", 1), ("nav_state", bench_id), ("nav_state_user_intention", bench_id)):
            assert re.search(rf"\b{field}:\s+{value}\b", authority), authority
        prearm = client("listener", "actuator_armed", "1")
        assert re.search(r"\bprearmed:\s+(?:True|true|1)\b", prearm), prearm
        assert re.search(r"\barmed:\s+(?:False|false|0)\b", prearm), prearm
        assert tuple(map(int, re.search(r"main_writes=(\d+) aux_writes=(\d+) nonzero=(\d+)",
                                       client("slaaps_control_test", "mixing", "status")).groups())) == before_counts
        client("param", "set", "SLA_BENCH_EN", "1")
        running = wait_status(r"bench entry=10 active=1 target=1\b")
        bench_deadline = int(re.search(r"bench entry=10 active=1 target=1 deadline=(\d+)", running).group(1))
        assert bench_deadline > 0, running
        client("slaaps_control_test", "selected", "offset")
        command_deadline = time.monotonic() + 1
        while True:
            bench_command = client("listener", "slaaps_manual_command", "1")
            canards_match = re.search(r"\bcanards:\s*\[([^\]]+)\]", bench_command)
            canards = [float(value) for value in canards_match.group(1).split(",")] if canards_match else []
            if len(canards) == 4 and canards[0] > 0 and all(value == 0 for value in canards[1:]):
                break
            assert time.monotonic() < command_deadline, bench_command
            time.sleep(.02)
        assert re.search(r"\bmode:\s+3\b", bench_command), bench_command
        assert re.search(r"\bedf_disposition:\s+0\b", bench_command), bench_command
        assert re.search(r"\bedf:\s+0(?:\.0+)?\s", bench_command), bench_command
        sample_time = int(re.search(r"\btimestamp_sample:\s+(\d+)", bench_command).group(1))
        bench_permission = client("listener", "slaaps_manual_permission", "1")
        assert re.search(r"\bmode:\s+3\b", bench_permission), bench_permission
        for field in ("prearmed", "healthy"):
            assert re.search(rf"\b{field}:\s+(?:True|true|1)\b", bench_permission), bench_permission
        for field in ("armed", "flight_admitted", "measured_condition_valid", "kill", "lockdown", "termination"):
            assert re.search(rf"\b{field}:\s+(?:False|false|0)\b", bench_permission), bench_permission
        # The established fixture places C1/C3/EDF on AUX8/AUX6/AUX4 and C2/C4
        # on MAIN7/MAIN5. Inspect every actual output-topic instance, not a fake ACK.
        capture_deadline = time.monotonic() + 1
        while True:
            captures = client("listener", "actuator_outputs", "1")
            vectors = [(int(stamp), [float(value) for value in values.split(",")])
                       for stamp, values in re.findall(r"\btimestamp:\s+(\d+).*?\boutput:\s*\[([^\]]+)\]",
                                                       captures, re.S)]
            fresh_vectors = [values for stamp, values in vectors if stamp >= sample_time and len(values) >= 8]
            aux_capture = next((values for values in fresh_vectors if values[7] > 1500 and values[5] == 1500
                                and all(values[index] == 0 for index in (0, 1, 2, 3, 4, 6))), None)
            main_capture = next((values for values in fresh_vectors if values[4] == values[6] == 1500
                                 and all(values[index] == 0 for index in (0, 1, 2, 3, 5, 7))), None)
            if aux_capture and main_capture:
                break
            assert time.monotonic() < capture_deadline, captures
            time.sleep(.02)
        active_counts = tuple(map(int, re.search(r"main_writes=(\d+) aux_writes=(\d+) nonzero=(\d+)",
                                                client("slaaps_control_test", "mixing", "status")).groups()))
        assert all(after > before for before, after in zip(before_counts, active_counts)), active_counts
        capture_floor = max(stamp for stamp, values in vectors if len(values) >= 8)
        client("param", "set", "SLA_BENCH_EN", "0")
        wait_status(r"bench entry=11 active=0\b")
        # Observe both original output leases plus one second for host CLI
        # scheduling. This allowance never changes a production timestamp/lease.
        stop_observation_seconds = bench_values["SLA_OUT_LEASE"] / 1000 + 1
        stop_observation_deadline = time.monotonic() + stop_observation_seconds
        while True:
            stopped_captures = client("listener", "actuator_outputs", "1")
            stopped_vectors = [(int(stamp), [float(value) for value in values.split(",")])
                               for stamp, values in re.findall(r"\btimestamp:\s+(\d+).*?\boutput:\s*\[([^\]]+)\]",
                                                               stopped_captures, re.S)]
            if (len(stopped_vectors) >= 2 and all(stamp > capture_floor and all(value == 0 for value in values)
                                                 for stamp, values in stopped_vectors)):
                break
            assert time.monotonic() < stop_observation_deadline, stopped_captures
            time.sleep(.02)
        stopped_counts = tuple(map(int, re.search(r"main_writes=(\d+) aux_writes=(\d+) nonzero=(\d+)",
                                                 client("slaaps_control_test", "mixing", "status")).groups()))
        client("param", "set", "SLA_BENCH_EN", "1")
        time.sleep(.2)
        assert re.search(r"bench entry=11 active=0\b", client("slaaps", "status")), "Enable revived completed Bench entry"
        resumed_counts = tuple(map(int, re.search(r"main_writes=(\d+) aux_writes=(\d+) nonzero=(\d+)",
                                                 client("slaaps_control_test", "mixing", "status")).groups()))
        assert resumed_counts == stopped_counts, "Completed Bench wrote again"
        stopped_status = client("listener", "slaaps_status", "1")
        assert re.search(r"bench_worker_active:\s+(?:False|false|0)\b", stopped_status), stopped_status
        assert re.search(r"can_arm_mask:\s+0\b", stopped_status), stopped_status
        authority = client("listener", "vehicle_status", "1")
        before_arm = int(re.search(r"\btimestamp:\s+(\d+)", authority).group(1))
        client("commander", "arm")
        arm_deadline = time.monotonic() + 2
        while True:
            acknowledgment = client("listener", "vehicle_command_ack", "1")
            ack_time = re.search(r"timestamp:\s+(\d+)", acknowledgment)
            if (re.search(r"command:\s+400\b", acknowledgment) and ack_time
                    and int(ack_time.group(1)) > before_arm):
                assert re.search(r"result:\s+[12]\b", acknowledgment), acknowledgment
                break
            assert time.monotonic() < arm_deadline, "Commander did not deny ordinary arming in Bench"
            time.sleep(.02)
        authority = client("listener", "vehicle_status", "1")
        assert re.search(r"\barming_state:\s+1\b", authority), authority
        result["production_servo_bench"] = dict(passed=True, actual_commander=True, actual_supervisor=True,
            actual_mixer=True, synthetic_configuration=True, synthetic_selected_inputs=True,
            synthetic_permission=False, hardware_interface="capture_only", mode_id=bench_id,
            actual_nav_state=True, prearmed_disarmed=True, target=1, duration_seconds=5,
            session_deadline_us=bench_deadline, can_run_mask=bench_mask, can_arm_mask=0,
            flight_admitted=False, measured_condition_valid=False, edf_disposition="inhibited", edf_pwm=0,
            explicit_enable_edge=True, explicit_stop=True, completed_entry_cannot_revive=True,
            ordinary_arm_denied=True, callback_counts_before=before_counts,
            callback_counts_active=active_counts, main_output=main_capture, aux_output=aux_capture)
        client("commander", "mode", "manual")
        wait_status(r"bench entry=0 active=0\b")
        # Deliberately armed EDF commissioning uses the SAME production mode,
        # owner and physical callback path. These limits are synthetic fixtures,
        # not an approved vehicle commissioning profile.
        client("slaaps_control_test", "selected", "neutral")
        client("slaaps_control_test", "selected", "power-valid")
        for name, value in {"SLA_CFG_KIND": 2, "SLA_BENCH_CH": 5, "SLA_BENCH_EN": 0,
                            "SLA_BENCH_SEC": 10, "SLA_EDF_BMAX": .25,
                            "SLA_EDF_RISE": 2, "SLA_EDF_FALL": 2}.items():
            client("param", "set", name, str(value))
        client("param", "set", "SLA_CFG_APPLY", "104")
        wait_status(r"configuration valid=1 use=2 apply=104")
        client("slaaps_control_test", "selected", "power-drop")
        wait_status(r"power required=1 ready=0 status=9\b")
        unavailable_power = client("listener", "slaaps_status", "1")
        assert re.search(r"can_arm_mask:\s+0\b", unavailable_power), unavailable_power
        assert re.search(r"bench_worker_active:\s+(?:False|false|0)\b", unavailable_power), unavailable_power
        client("slaaps_control_test", "selected", "power-low")
        wait_status(r"power required=1 ready=0 status=11\b")
        client("slaaps_control_test", "selected", "power-valid")
        wait_status(r"power required=1 ready=1 status=1\b")
        client("commander", "mode", f"ext{bench_id - 22}")
        wait_status(r"bench entry=6 active=0\b")
        ordinary_arming_results = []
        for edf_session in range(2):
            edf_before = client("slaaps_control_test", "mixing", "status")
            edf_before_counts = tuple(map(int, re.search(r"main_writes=(\d+) aux_writes=(\d+) nonzero=(\d+)",
                                                        edf_before).groups()))
            client("param", "set", "SLA_BENCH_EN", "1")
            wait_status(r"bench entry=15 active=0\b")
            edf_ready = client("listener", "slaaps_status", "1")
            assert re.search(r"bench_commissioning_ready:\s+(?:True|true|1)\b", edf_ready), edf_ready
            assert re.search(rf"can_arm_mask:\s+{bench_mask}\b", edf_ready), edf_ready
            for field in ("power_required", "power_ready", "power_source_bound"):
                assert re.search(rf"\b{field}:\s+(?:True|true|1)\b", edf_ready), edf_ready
            readiness_expiry = int(re.search(r"bench_commissioning_ready_deadline:\s+(\d+)", edf_ready).group(1))
            power_expiry = int(re.search(r"power_deadline:\s+(\d+)", edf_ready).group(1))
            assert 0 < readiness_expiry <= power_expiry, edf_ready
            assert tuple(map(int, re.search(r"main_writes=(\d+) aux_writes=(\d+) nonzero=(\d+)",
                                            client("slaaps_control_test", "mixing", "status")).groups())) == edf_before_counts
            authority = client("listener", "vehicle_status", "1")
            assert re.search(r"\barming_state:\s+1\b", authority), authority
            before_arm = int(re.search(r"\btimestamp:\s+(\d+)", authority).group(1))
            client("commander", "arm")
            arm_deadline = time.monotonic() + 2
            while True:
                acknowledgment = client("listener", "vehicle_command_ack", "1")
                ack_time = re.search(r"timestamp:\s+(\d+)", acknowledgment)
                if re.search(r"command:\s+400\b", acknowledgment) and ack_time and int(ack_time.group(1)) > before_arm:
                    break
                assert time.monotonic() < arm_deadline, acknowledgment
                time.sleep(.02)
            ordinary_arming_accepted = bool(re.search(r"result:\s+0\b", acknowledgment))
            ordinary_arming_results.append(ordinary_arming_accepted)
            if not ordinary_arming_accepted:
                assert re.search(r"result:\s+[12]\b", acknowledgment), acknowledgment
                # No-plant builds lack normal sensor/power health. Use stock internal
                # forced arming ONLY in this capture-only test, with the limitation
                # recorded below. Production gates/health checks are not modified.
                client("commander", "arm", "-f")
            wait_status(r"bench entry=16 active=1 target=5\b")
            authority = client("listener", "vehicle_status", "1")
            assert re.search(r"\barming_state:\s+2\b", authority), authority
            assert re.search(rf"\bnav_state:\s+{bench_id}\b", authority), authority
            edf_permission = client("listener", "slaaps_manual_permission", "1")
            assert re.search(r"\bmode:\s+4\b", edf_permission), edf_permission
            for field in ("armed", "healthy", "commissioning_admitted"):
                assert re.search(rf"\b{field}:\s+(?:True|true|1)\b", edf_permission), edf_permission
            for field in ("flight_admitted", "measured_condition_valid", "kill", "lockdown", "termination"):
                assert re.search(rf"\b{field}:\s+(?:False|false|0)\b", edf_permission), edf_permission
            zero_command = client("listener", "slaaps_manual_command", "1")
            assert re.search(r"\bmode:\s+4\b", zero_command), zero_command
            assert re.search(r"\bedf:\s+0(?:\.0+)?\s", zero_command), zero_command
            client("slaaps_control_test", "selected", "throttle-high")
            command_deadline = time.monotonic() + 1
            while True:
                edf_command = client("listener", "slaaps_manual_command", "1")
                value = re.search(r"\bedf:\s+([\d.eE+-]+)", edf_command)
                if value and 0 < float(value.group(1)) <= .25:
                    break
                assert time.monotonic() < command_deadline, edf_command
                time.sleep(.02)
            assert re.search(r"\bedf_disposition:\s+1\b", edf_command), edf_command
            edf_canards = re.search(r"\bcanards:\s*\[([^\]]+)\]", edf_command)
            assert edf_canards and all(float(v) == 0 for v in edf_canards.group(1).split(",")), edf_command
            edf_sample_time = int(re.search(r"\btimestamp_sample:\s+(\d+)", edf_command).group(1))
            capture_deadline = time.monotonic() + 1
            while True:
                captures = client("listener", "actuator_outputs", "1")
                vectors = [(int(stamp), [float(v) for v in values.split(",")])
                           for stamp, values in re.findall(r"\btimestamp:\s+(\d+).*?\boutput:\s*\[([^\]]+)\]", captures, re.S)]
                edf_aux = next((values for stamp, values in vectors if stamp >= edf_sample_time and len(values) >= 8
                                and 1000 < values[3] <= 1250 and values[5] == values[7] == 1500
                                and all(values[i] == 0 for i in (0, 1, 2, 4, 6))), None)
                if edf_aux:
                    break
                assert time.monotonic() < capture_deadline, captures
                time.sleep(.02)
            edf_capture_floor = max(stamp for stamp, values in vectors if len(values) >= 8)
            if edf_session == 0:
                client("slaaps_control_test", "selected", "power-fault")
            else:
                client("param", "set", "SLA_BENCH_EN", "0")
            stop_deadline = time.monotonic() + 2
            while True:
                captures = client("listener", "actuator_outputs", "1")
                stopped = [(int(stamp), [float(v) for v in values.split(",")])
                           for stamp, values in re.findall(r"\btimestamp:\s+(\d+).*?\boutput:\s*\[([^\]]+)\]", captures, re.S)]
                if len(stopped) >= 2 and all(stamp > edf_capture_floor and all(v == 0 for v in values) for stamp, values in stopped):
                    break
                assert time.monotonic() < stop_deadline, captures
                time.sleep(.02)
            terminal_entry = 12 if edf_session == 0 else 11
            wait_status(rf"bench entry=(?:0|{terminal_entry}) active=0\b")
            authority = client("listener", "vehicle_status", "1")
            assert re.search(r"\barming_state:\s+1\b", authority), authority
            stopped_nonzero = int(re.search(r"nonzero=(\d+)",
                                            client("slaaps_control_test", "mixing", "status")).group(1))
            if edf_session == 0:
                client("slaaps_control_test", "selected", "power-valid")
            client("param", "set", "SLA_BENCH_EN", "1")
            time.sleep(.15)
            stopped_status = client("slaaps", "status")
            stopped_authority = client("listener", "vehicle_status", "1")
            assert re.search(r"\barming_state:\s+1\b", stopped_authority), stopped_authority
            # Commander may leave the external mode on ordinary disarm. The
            # retained terminal state applies while selected; mode exit is INACTIVE.
            still_selected = bool(re.search(rf"\bnav_state_user_intention:\s+{bench_id}\b", stopped_authority))
            stopped_entry = terminal_entry if still_selected else 0
            assert re.search(rf"bench entry={stopped_entry} active=0\b", stopped_status), stopped_status
            assert int(re.search(r"nonzero=(\d+)",
                                 client("slaaps_control_test", "mixing", "status")).group(1)) == stopped_nonzero
            edf_stopped = client("listener", "slaaps_status", "1")
            assert re.search(r"can_arm_mask:\s+0\b", edf_stopped), edf_stopped
            if edf_session == 0:
                # Restored power and reapply cannot restart a rejected session.
                client("slaaps_control_test", "selected", "neutral")
                client("param", "set", "SLA_CFG_APPLY", "105")
                wait_status(r"configuration valid=1 use=2 apply=105")
                reapplied_power = client("listener", "slaaps_status", "1")
                assert re.search(r"bench_worker_active:\s+(?:False|false|0)\b", reapplied_power), reapplied_power
                assert re.search(r"can_arm_mask:\s+0\b", reapplied_power), reapplied_power
                # The preceding no-restart probe deliberately left EN=1. A new
                # entry must observe disabled readiness before its fresh EN edge.
                client("param", "set", "SLA_BENCH_EN", "0")
                client("commander", "mode", "manual")
                wait_status(r"bench entry=0 active=0\b")
                client("commander", "mode", f"ext{bench_id - 22}")
                wait_status(r"bench entry=6 active=0\b")
        result["production_edf_commissioning"] = dict(passed=True, actual_commander=True, actual_supervisor=True,
            actual_mixer=True, synthetic_configuration=True, synthetic_selected_inputs=True, synthetic_permission=False,
            hardware_interface="capture_only", ordinary_arming_accepted=all(ordinary_arming_results),
            forced_internal_test_arm=not all(ordinary_arming_results), normal_vehicle_health_qualified=False,
            mode_id=bench_id, target=5, normalized_ceiling=.25, duration_seconds=10,
            disarmed_preparation_no_output=True, actual_armed_state=True, flight_admitted=False,
            measured_condition_valid=False, parked_canards=True, low_throttle_zero_command=True,
            explicit_stop=True, observed_ordinary_disarm=True, completed_entry_cannot_revive=True, aux_output=edf_aux,
            synthetic_power=True, selected_power_required=True, stale_power_blocks_readiness=True,
            out_of_range_power_blocks_readiness=True, active_power_fault_stops_and_disarms=True,
            restored_power_does_not_revive=True, explicit_power_fault_reentry=True)
        client("commander", "mode", "manual")
        wait_status(r"bench entry=0 active=0\b")
        # Persist the same accepted kind2 profile, including a saved EN=1 intent.
        # Restart must restore calibration but reset intent, never re-arm/re-enable.
        client("slaaps", "stop")
        client("param", "set", "SLA_BENCH_EN", "1")
        edf_parameters = out / "accepted-edf-commissioning.bson"
        client("param", "save", str(edf_parameters))
        assert edf_parameters.is_file() and edf_parameters.stat().st_size > 0
        client("param", "set", "SLA_EDF_BMAX", ".1")
        client("param", "set", "SLA_BENCH_EN", "0")
        client("param", "load", str(edf_parameters))
        client("slaaps", "start")
        restored = wait_status(r"configured=1")
        assert "configuration valid=1 use=2 apply=105" in restored, restored
        client("param", "compare", "SLA_BENCH_EN", "0")
        client("param", "compare", "SLA_EDF_BMAX", ".25")
        restored_wire = client("listener", "slaaps_status", "1")
        assert re.search(r"can_arm_mask:\s+0\b", restored_wire), restored_wire
        assert re.search(r"bench_worker_active:\s+(?:False|false|0)\b", restored_wire), restored_wire
        assert not re.search(r"\barmed:\s+(?:True|true|1)\b", client("listener", "actuator_armed", "1"))
        result["production_edf_commissioning"].update(profile_export_restore=True, restored_without_permission=True,
            accepted_parameters_sha256=hashlib.sha256(edf_parameters.read_bytes()).hexdigest())
        client("param", "set", "SLA_BENCH_EN", "0")
        client("param", "set", "COM_PREARM_MODE", "0")
        client("slaaps_control_test", "selected", "stop")
        client("slaaps_control_test", "mixing", "stop")
        result["production_mixer_preparation"] = dict(passed=True, actual_mixer=True, actual_supervisor=True,
            actual_commander=True, synthetic_configuration=True, synthetic_selected_inputs=True,
            hardware_interface="capture_only", activation=False, disarmed_intent_entry=True,
            centered_and_low_throttle_entry=True, numerical_state_gate=True, input_loss_cancel=True,
            publisher_replacement_cancel=True, configuration_cancel=True, routing_cancel=True,
            expiry_cancel=True, explicit_mode_reentry=True, apply_does_not_retry=True,
            ordinary_arm_denied=True)
        result["companion_transport"] = verify_companion_transport(client, wait_status, out, build)
        result.update(passed=True, supervisor_endpoint_lifecycles=7, modes=list(modes), physical_outputs=False,
                      configuration_restart_checks=7, parameter_export_import=True,
                      accepted_parameters_sha256=hashlib.sha256(accepted_parameters.read_bytes()).hexdigest(),
                      qgc_gui_verified=False)
    finally:
        if process.poll() is None:
            # Cleanup also runs after a failed positive scenario. Stop the
            # operator command and existing helpers before terminating PX4.
            for module, arguments in (("param", ("set", "SLA_BENCH_EN", "0")),
                                      ("commander", ("disarm",)),
                                      ("param", ("set", "COM_PREARM_MODE", "0")),
                                      ("slaaps_control_test", ("selected", "stop")),
                                      ("slaaps_control_test", ("mixing", "stop"))):
                try:
                    client(module, *arguments, check=False)
                except (AssertionError, subprocess.TimeoutExpired):
                    pass
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
