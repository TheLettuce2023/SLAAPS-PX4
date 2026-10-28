#!/usr/bin/env python3
"""Exercise SLAAPS supervisor and actual mixer handling with capture-only outputs."""

import argparse
from contextlib import ExitStack
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


def load_mavlink_dialect(out, build):
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
    return dialect


class SignedFlightClient:
    """Actual signed UDP ingress for the isolated capture-only flight fixture.

    Fixture instructions select a test operation and carry current owner
    identity, never authentication or a remote timestamp. This peer creates
    its own producer timestamps and sends through the normal MAVLink parser.
    """

    instruction_prefix = "SLAAPS_SIGNED_FLIGHT "

    def __init__(self, dialect, out):
        self.dialect, self.out = dialect, out
        self.key = bytes(range(1, 33))  # Public synthetic key; no deployment material.
        self.sender = dialect.MAVLink(None, srcSystem=42, srcComponent=191)
        self.sender.signing.secret_key = self.key
        self.sender.signing.link_id = 19
        self.sender.signing.timestamp = 100000000
        self.sender.signing.sign_outgoing = True
        self.receiver = dialect.MAVLink(None)
        # Configured pymavlink verification rejects bad signatures before a
        # typed status/challenge reaches the checks below; SIGNED alone is not proof.
        self.receiver.signing.secret_key = self.key
        self.receiver.robust_parsing = True
        self.peer = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.peer.bind(("127.0.0.1", 0))
        self.peer.setblocking(False)
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as reservation:
            reservation.bind(("127.0.0.1", 0))
            self.port = reservation.getsockname()[1]
        self.destination = ("127.0.0.1", self.port)
        self.operation_sequence = self.request_sequence = 0
        self.pending_ack = self.pending_request = self.clock = None
        self.last_ack = {}
        self.buffer = ""
        self.sent, self.received, self.exchanges, self.instructions = [], [], [], []
        self.discarded_requests = []
        self.received_types = {}
        self.setup_sent = False
        self.proven = False
        self.datagrams = self.maximum_batch = 0

    def startup(self):
        return ("dataman start -r\n"
                f"mavlink start -u {self.port} -o {self.peer.getsockname()[1]} -t 127.0.0.1 -m onboard -r 10000\n"
                "mavlink boot_complete\n"
                f"mavlink stream -u {self.port} -s TIMESYNC -r 50\n"
                "echo SLAAPS_SIGNED_LINK_READY\n")

    @staticmethod
    def producer_now():
        return time.monotonic_ns() // 1000 + 1234567

    def send(self, message):
        frame = message.pack(self.sender)
        self.peer.sendto(frame, self.destination)
        self.sent.append(dict(type=message.get_type(), signed=bool(frame[0] == 0xfd and frame[2] & 1),
                              producer_us=self.producer_now(), payload=message.to_dict()))

    def session(self, instruction, operation, acknowledged=0, acknowledgement_time=0):
        self.operation_sequence += 1
        self.send(self.dialect.MAVLink_research_acceleration_session_message(
            controller_instance_id=instruction["controller"], transition_epoch=instruction["epoch"],
            clock_mapping_epoch=max(1, instruction["map"]), source_id=1, session_id=instruction["session"],
            operation_sequence=self.operation_sequence, producer_acknowledgement_monotonic_us=acknowledgement_time,
            acknowledged_status_sequence=acknowledged, protocol_version=0x10000,
            target_system=1, target_component=1, operation=operation, flags=0, reserved=[0] * 4))

    def instruction(self, line):
        assert len(line) <= 1024, "Unbounded signed-flight instruction"
        parts = [item.split("=", 1) for item in line[len(self.instruction_prefix):].split()]
        fields = dict(parts)
        assert len(fields) == len(parts), "Repeated signed-flight instruction field"
        kind = fields.pop("kind")
        common = {"controller", "binding", "epoch", "map", "session", "offset", "uncertainty", "deadline"}
        extra = {"session": {"operation", "barrier", "barrier_time"}, "clock": set(),
                 "clock_done": set(), "request": {"ax", "ay", "az", "minimum_validity", "maximum_validity"}}
        assert kind in extra and set(fields) == common | extra[kind], line
        instruction = {key: float(value) if key in ("ax", "ay", "az") else int(value)
                       for key, value in fields.items()}
        assert instruction["controller"] > 0 and instruction["binding"] > 0 and instruction["session"] > 0, line
        self.instructions.append(dict(kind=kind, **instruction))
        if kind == "session":
            assert instruction["operation"] in (1, 3), line
            if instruction["operation"] == 1:
                self.session(instruction, 1)
            else:
                self.pending_ack = instruction
        elif kind == "clock":
            self.clock = instruction
        elif kind == "clock_done":
            self.clock = None
        else:
            if self.pending_request and any(self.pending_request[key] != instruction[key]
                                            for key in ("controller", "session", "epoch", "map", "ax", "ay", "az")):
                # A changed operation supersedes an unsent sample. Discard it
                # explicitly; never rewrite its objective/creation/expiry or
                # silently lose the new one-shot lateral operation behind it.
                self.discarded_requests.append(self.pending_request)
                self.pending_request = None
            if self.pending_request is None:
                # Queue the test operation until its actual wire ACK is old
                # enough. A one-shot instruction must not be lost here.
                self.pending_request = dict(instruction)

    def pump(self, text):
        self.buffer += text
        while "\n" in self.buffer:
            line, self.buffer = self.buffer.split("\n", 1)
            if line == "SLAAPS_SIGNED_LINK_READY":
                assert not self.setup_sent, "Repeated signing setup would replace the receiver lifetime"
                self.sender.signing.sign_outgoing = False
                self.send(self.dialect.MAVLink_setup_signing_message(1, 1, self.key, self.sender.signing.timestamp))
                self.sender.signing.sign_outgoing = True
                self.setup_sent = True
            elif line.startswith(self.instruction_prefix):
                assert self.setup_sent, "Signed fixture started before key setup"
                self.instruction(line)
        assert len(self.buffer) <= 4096, "Unterminated supervisor output line"
        until, batch = time.monotonic() + .005, 0
        for _ in range(64):
            try:
                data, address = self.peer.recvfrom(65535)
            except BlockingIOError:
                break
            assert address == self.destination, "Foreign signed-flight UDP source"
            batch += 1
            self.datagrams += 1
            for message in self.receiver.parse_buffer(data) or []:
                kind = message.get_type()
                self.received_types[kind] = self.received_types.get(kind, 0) + 1
                if kind not in ("TIMESYNC", "RESEARCH_ACCELERATION_STATUS"):
                    continue
                if kind == "TIMESYNC" and (message.tc1 != 0 or message.target_system != 42 or message.target_component != 191):
                    continue  # Ordinary TIMESYNC is not the bound private challenge.
                raw = message.get_msgbuf()
                assert len(raw) > 2 and raw[0] == 0xfd and raw[2] & 1, "Unsigned active protocol response"
                assert message.get_srcSystem() == 1 and message.get_srcComponent() == 1, message
                assert message.target_system == 42 and message.target_component == 191, message
                self.received.append(message.to_dict())
                if kind == "TIMESYNC" and self.clock:
                    stamp = self.producer_now()
                    self.send(self.dialect.MAVLink_timesync_message(
                        tc1=stamp * 1000, ts1=message.ts1, target_system=1, target_component=1))
                    self.exchanges.append(dict(challenge_ns=message.ts1, producer_reply_us=stamp, **self.clock))
            if time.monotonic() >= until:
                break
        self.maximum_batch = max(self.maximum_batch, batch)
        if self.pending_ack:
            pending = self.pending_ack
            stamp = self.producer_now()
            earliest = pending["barrier_time"] - pending["offset"] + pending["uncertainty"]
            barrier_received = any(message.get("mavpackettype") == "RESEARCH_ACCELERATION_STATUS"
                                   and message["status_class"] == 3 and message["status_sequence"] == pending["barrier"]
                                   and message["controller_instance_id"] == pending["controller"]
                                   and message["transition_epoch"] == pending["epoch"]
                                   and message["clock_mapping_epoch"] == pending["map"]
                                   and message["source_id"] == 1
                                   and message["session_id"] == pending["session"]
                                   for message in self.received)
            if barrier_received and stamp > earliest:
                self.session(pending, 3, pending["barrier"], stamp)
                self.last_ack[(pending["controller"], pending["session"], pending["epoch"], pending["map"])] = stamp
                self.pending_ack = None
        if self.pending_request and "created" not in self.pending_request:
            pending = self.pending_request
            created = self.producer_now()
            acknowledged = self.last_ack.get((pending["controller"], pending["session"], pending["epoch"], pending["map"]), 0)
            if acknowledged and created > acknowledged + 2 * pending["uncertainty"]:
                expiry = min(created + pending["maximum_validity"],
                             pending["deadline"] - pending["offset"] - pending["uncertainty"])
                if expiry - created < pending["minimum_validity"]:
                    self.pending_request = None
                else:
                    # Producer creation happens once; the exact sample then
                    # waits before send without refreshing any timestamp.
                    pending.update(created=created, expiry=expiry, send_after=created + 2 * pending["uncertainty"])
        if self.pending_request and "created" in self.pending_request and self.producer_now() > self.pending_request["send_after"]:
            pending, self.pending_request = self.pending_request, None
            if self.producer_now() < pending["expiry"]:
                self.request_sequence += 1
                self.send(self.dialect.MAVLink_research_acceleration_request_message(
                    source_id=1, session_id=pending["session"], sequence=self.request_sequence,
                    producer_monotonic_us=pending["created"], producer_expiry_monotonic_us=pending["expiry"],
                    controller_instance_id=pending["controller"], transition_epoch=pending["epoch"],
                    clock_mapping_epoch=pending["map"], acceleration_x_frd_m_s2=pending["ax"],
                    acceleration_y_frd_m_s2=pending["ay"], acceleration_z_frd_m_s2=pending["az"],
                    protocol_version=0x20000, target_system=1, target_component=1,
                    frame=1, quantity=1, reference_point=1, flags=1, reserved=[0] * 4,
                    desired_attitude_ned_from_body=[1., 0., 0., 0.]))

    def close(self):
        self.peer.close()
        (self.out / "signed-flight-evidence.json").write_text(json.dumps(dict(
            synthetic_authority=True, physical_outputs=False, producer_clock_offset_us=1234567,
            passed=self.proven, intended_ingress="actual_signed_mavlink_parser", setup_sent=self.setup_sent, instructions=self.instructions,
            sent=self.sent, received=self.received, exchanges=self.exchanges,
            discarded_unsent_requests=self.discarded_requests,
            received_types=self.received_types, datagrams=self.datagrams, maximum_batch=self.maximum_batch), indent=2) + "\n")


def verify_armed_mode_rejections(client, out, dialect, peer, destination, bench_id):
    """Reject unsigned GCS LAND requests in an existing capture-only EDF session."""
    sender = dialect.MAVLink(None, srcSystem=255, srcComponent=190)
    sender.signing.sign_outgoing = False
    receiver = dialect.MAVLink(None)
    receiver.robust_parsing = True
    # Wire values are fixed by px4_custom_mode.h, as in verify_mode_protocol.
    px4_auto_main_mode, px4_auto_land_sub_mode = 4, 6
    # PX4 VehicleCommand.msg calls wire command 23 NAV_PRECLAND; the pinned
    # common MAVLink dialect calls that value NAV_LAND_LOCAL.
    px4_nav_precland = 23
    evidence = dict(passed=False, loopback_only=True, unsigned_commands=True,
                    actual_commander=True, physical_outputs=False,
                    termination_wire_tested=False, cases=[], wire_acknowledgements=[])

    def capture():
        return dict(authority=client("listener", "vehicle_status", "1"),
                    status=client("listener", "slaaps_status", "1"),
                    command=client("listener", "slaaps_manual_command", "1"))

    def assert_active(snapshot):
        authority, status, command = (snapshot[key] for key in ("authority", "status", "command"))
        assert re.search(r"\barming_state:\s+2\b", authority), snapshot
        for field in ("nav_state", "nav_state_user_intention"):
            assert re.search(rf"\b{field}:\s+{bench_id}\b", authority), snapshot
        assert re.search(r"\bbench_entry_status:\s+16\b", status), snapshot
        assert re.search(r"\bbench_worker_active:\s+(?:True|true|1)\b", status), snapshot
        assert re.search(r"\bbench_disarm_pending:\s+(?:False|false|0)\b", status), snapshot
        assert re.search(r"\bmode:\s+4\b", command), snapshot
        session = int(re.search(r"\bsession:\s+(\d+)", command).group(1))
        deadline = int(re.search(r"\bbench_session_deadline:\s+(\d+)", status).group(1))
        assert session and deadline, snapshot
        return session, deadline

    try:
        evidence["before"] = capture()
        binding = assert_active(evidence["before"])
        cases = (
            ("do_set_mode_auto_land", dialect.MAV_CMD_DO_SET_MODE,
             (dialect.MAV_MODE_FLAG_CUSTOM_MODE_ENABLED, px4_auto_main_mode, px4_auto_land_sub_mode)),
            ("nav_land", dialect.MAV_CMD_NAV_LAND, (0., 0., 0.)),
            ("nav_precland", px4_nav_precland, (0., 0., 0.)),
        )
        for name, command_id, parameters in cases:
            case = dict(name=name, command=command_id, sends=0)
            evidence["cases"].append(case)
            before = capture()
            assert assert_active(before) == binding, before
            before_timestamp = int(re.search(r"\btimestamp:\s+(\d+)", before["authority"]).group(1))
            message = dialect.MAVLink_command_long_message(1, 1, command_id, 0,
                *parameters, float("nan"), float("nan"), float("nan"), float("nan"))
            frame = message.pack(sender)
            assert frame[0] == 0xFD and not frame[2] & dialect.MAVLINK_IFLAG_SIGNED, "Expected unsigned MAVLink2"
            start = len(evidence["wire_acknowledgements"])
            deadline = time.monotonic() + 8  # Existing verifier wait bound; no command retries.
            peer.sendto(frame, destination)
            case.update(sends=1, sent_python_monotonic_ns=time.monotonic_ns())
            acknowledgment = None
            while acknowledgment is None:
                receive_until = time.monotonic() + .005
                for _ in range(64):
                    try:
                        data, _ = peer.recvfrom(65535)
                    except BlockingIOError:
                        break
                    for received in receiver.parse_buffer(data) or []:
                        if received.get_type() != "COMMAND_ACK":
                            continue
                        assert received.get_srcSystem() == 1 and received.get_srcComponent() == 1, received
                        assert len(evidence["wire_acknowledgements"]) < 64, "ACK evidence exceeded bounded capture"
                        evidence["wire_acknowledgements"].append(dict(
                            received_python_monotonic_ns=time.monotonic_ns(), **received.to_dict()))
                    if time.monotonic() >= receive_until:
                        break
                matches = [value for value in evidence["wire_acknowledgements"][start:]
                           if value["command"] == command_id]
                if matches:
                    assert len(matches) == 1, matches
                    acknowledgment = matches[0]
                    break
                assert time.monotonic() < deadline, (name, evidence)
                time.sleep(.02)
            case["wire_ack"] = acknowledgment
            assert acknowledgment["target_system"] == 255 and acknowledgment["target_component"] == 190, acknowledgment
            assert acknowledgment["result"] == dialect.MAV_RESULT_TEMPORARILY_REJECTED, acknowledgment
            native_ack = client("listener", "vehicle_command_ack", "1")
            case["native_ack"] = native_ack
            for field, value in (("command", command_id), ("result", dialect.MAV_RESULT_TEMPORARILY_REJECTED),
                                 ("target_system", 255), ("target_component", 190)):
                assert re.search(rf"\b{field}:\s+{value}\b", native_ack), native_ack
            ack_timestamp = int(re.search(r"\btimestamp:\s+(\d+)", native_ack).group(1))
            assert ack_timestamp > before_timestamp, native_ack
            while True:
                after = capture()
                case["after"] = after
                # A retained pre-command status cannot prove the rejection kept
                # the actual/intended mode. Commander must publish after its ACK.
                after_timestamp = int(re.search(r"\btimestamp:\s+(\d+)", after["authority"]).group(1))
                assert assert_active(after) == binding, after
                if after_timestamp > ack_timestamp:
                    break
                assert time.monotonic() < deadline, (name, native_ack, after)
                time.sleep(.02)
            case.update(passed=True, post_ack_vehicle_status_timestamp=after_timestamp,
                        native_ack_timestamp=ack_timestamp)
        evidence.update(passed=True, unchanged_session=binding[0], unchanged_session_deadline=binding[1])
        return dict(passed=True, actual_unsigned_loopback=True, commands=[case[0] for case in cases],
                    acknowledgment="TEMPORARILY_REJECTED", actual_and_intended_bench_unchanged=True,
                    fresh_post_ack_vehicle_status=True, armed_worker_and_session_preserved=True,
                    termination_wire_tested=False, physical_outputs=False)
    finally:
        (out / "armed-mode-rejection-evidence.json").write_text(json.dumps(evidence, indent=2) + "\n")


def verify_companion_transport(client, wait_status, out, dialect):
    """Exercise the actual loopback parser and supervisor with a synthetic key.

    Exercises receipt and deliberate clock/session administration without a
    selected acceleration mode, acceleration controller or output permission.
    The caller owns an isolated native process and its private storage directory.
    """
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

        def verify_mode_protocol(registered):
            # Pin the existing PX4 wire encoding (px4_custom_mode.h): AUTO=4,
            # EXTERNAL1 submode=11, and VehicleStatus external nav states 23..30.
            # A disarmed intention is independent of readiness/output permission.
            names = ("SLAAPS Manual", "SLAAPS Acceleration", "SLAAPS Bench")
            px4_auto_main_mode, px4_manual_main_mode = 4, 1
            px4_external1_sub_mode, first_external_nav_state, last_external_nav_state = 11, 23, 30
            matched = re.search(r"manual=(\d+) acceleration=(\d+) bench=(\d+)", registered)
            assert matched, registered
            nav_ids = tuple(map(int, matched.groups()))
            assert len(set(nav_ids)) == 3 and all(first_external_nav_state <= value <= last_external_nav_state for value in nav_ids), registered
            custom_ids = {name: (px4_auto_main_mode << 16) | ((px4_external1_sub_mode + nav - first_external_nav_state) << 24)
                          for name, nav in zip(names, nav_ids)}
            manual_custom = px4_manual_main_mode << 16
            gcs = dialect.MAVLink(None, srcSystem=255, srcComponent=190)
            receiver = dialect.MAVLink(None)
            receiver.robust_parsing = True
            peer.setblocking(False)
            observed = []
            evidence = dict(passed=False, registrations_before_link=registered, custom_modes=custom_ids,
                            qgc_gui_verified=False, physical_outputs=False)

            def command(command_id, param1, param2=float("nan"), param3=float("nan")):
                message = dialect.MAVLink_command_long_message(1, 1, command_id, 0,
                    param1, param2, param3, float("nan"), float("nan"), float("nan"), float("nan"))
                peer.sendto(message.pack(gcs), destination)

            def wait_wire(predicate, description):
                deadline = time.monotonic() + 8
                while time.monotonic() < deadline:
                    # Bounded drain of the shared telemetry socket; send each
                    # tested command once, without retrying a failed transition.
                    receive_until = time.monotonic() + .005
                    for _ in range(64):
                        try:
                            data, _ = peer.recvfrom(65535)
                        except BlockingIOError:
                            break
                        for message in receiver.parse_buffer(data) or []:
                            kind = message.get_type()
                            if kind not in ("AVAILABLE_MODES", "AVAILABLE_MODES_MONITOR", "CURRENT_MODE", "COMMAND_ACK"):
                                continue
                            assert message.get_srcSystem() == 1 and message.get_srcComponent() == 1, message
                            value = message.to_dict()
                            if kind == "AVAILABLE_MODES":
                                name = message.mode_name
                                value["mode_name"] = (name.decode("ascii") if isinstance(name, bytes) else name).rstrip("\0")
                            assert len(observed) < 256, "Mode protocol evidence exceeded its bounded capture"
                            observed.append(dict(received_python_monotonic_ns=time.monotonic_ns(), **value))
                        if time.monotonic() >= receive_until:
                            break
                    if predicate():
                        return
                    time.sleep(.02)
                raise AssertionError(f"Missing wire mode evidence: {description}; observed={observed}")

            def request_modes():
                start = len(observed)
                command(dialect.MAV_CMD_REQUEST_MESSAGE, dialect.MAVLINK_MSG_ID_AVAILABLE_MODES, 0)
                rows = lambda: [value for value in observed[start:] if value["mavpackettype"] == "AVAILABLE_MODES"]
                wait_wire(lambda: bool(rows()) and len(rows()) >= rows()[0]["number_modes"], "complete AVAILABLE_MODES list")
                result = rows()
                total = result[0]["number_modes"]
                assert len(result) == total and {value["mode_index"] for value in result} == set(range(1, total + 1)), result
                assert len({value["seq"] for value in result}) == 1, result
                return result

            def assert_names(rows):
                for name, custom in custom_ids.items():
                    matches = [value for value in rows if value["custom_mode"] == custom]
                    assert len(matches) == 1 and matches[0]["mode_name"] == name, (name, custom, rows)
                    assert matches[0]["standard_mode"] == dialect.MAV_STANDARD_MODE_NON_STANDARD, matches

            def assert_no_permission():
                wire = client("listener", "slaaps_status", "1")
                assert re.search(r"can_arm_mask:\s+0\b", wire), wire
                can_run = int(re.search(r"can_run_mask:\s+(\d+)", wire).group(1))
                assert not can_run & ((1 << nav_ids[0]) | (1 << nav_ids[1])), wire
                for field in ("manual_worker_active", "bench_worker_active"):
                    assert re.search(rf"\b{field}:\s+(?:False|false|0)\b", wire), wire
                assert re.search(r"manual_preparation_session:\s+0\b", wire), wire
                authority = client("listener", "vehicle_status", "1")
                assert re.search(r"\barming_state:\s+1\b", authority), authority
                return wire

            try:
                initial = request_modes()
                assert_names(initial)
                evidence["initial_modes"] = initial
                start = len(observed)
                peer.sendto(dialect.MAVLink_set_mode_message(1, dialect.MAV_MODE_FLAG_CUSTOM_MODE_ENABLED,
                            custom_ids["SLAAPS Bench"]).pack(gcs), destination)
                wait_wire(lambda: any(value["mavpackettype"] == "CURRENT_MODE"
                    and value["custom_mode"] == custom_ids["SLAAPS Bench"]
                    and value["intended_custom_mode"] == custom_ids["SLAAPS Bench"] for value in observed[start:]),
                    "SET_MODE selects actual and intended Bench")
                evidence["selected_without_permission"] = assert_no_permission()
                start = len(observed)
                command(dialect.MAV_CMD_DO_SET_MODE, dialect.MAV_MODE_FLAG_CUSTOM_MODE_ENABLED, px4_manual_main_mode, 0)
                wait_wire(lambda: any(value["mavpackettype"] == "CURRENT_MODE"
                    and value["custom_mode"] == manual_custom and value["intended_custom_mode"] == manual_custom
                    for value in observed[start:]) and any(value["mavpackettype"] == "COMMAND_ACK"
                    and value["command"] == dialect.MAV_CMD_DO_SET_MODE and value["result"] == dialect.MAV_RESULT_ACCEPTED
                    for value in observed[start:]), "COMMAND_LONG exits Bench with accepted ACK/current mode")
                wait_status(r"bench entry=0 active=0\b")
                assert_no_permission()

                # Exercise real unregistration while this same link remains up.
                # Observe completion before querying, rather than race the three
                # separate unregister requests or change their timeout policy.
                client("slaaps", "stop")
                registry_deadline = time.monotonic() + 8
                while True:
                    registry = client("listener", "external_mode_registry", "1")
                    valid = int(re.search(r"\bvalid_mask:\s+(\d+)", registry).group(1))
                    if not valid & sum(1 << (nav - first_external_nav_state) for nav in nav_ids):
                        break
                    assert time.monotonic() < registry_deadline, registry
                    time.sleep(.02)
                evidence["removed_registry"] = registry
                wait_wire(lambda: any(value["mavpackettype"] == "AVAILABLE_MODES_MONITOR"
                    and value["seq"] != initial[0]["seq"] for value in observed), "removal changes mode monitor sequence")
                removed = request_modes()
                assert removed[0]["seq"] != initial[0]["seq"], removed
                for custom in custom_ids.values():
                    matches = [value for value in removed if value["custom_mode"] == custom]
                    assert len(matches) == 1 and matches[0]["mode_name"] == "(Mode not available)", matches
                    assert matches[0]["properties"] & dialect.MAV_MODE_PROPERTY_NOT_USER_SELECTABLE, matches
                evidence["removed_modes"] = removed

                client("slaaps", "start")
                restarted = wait_status(r"configured=1")
                assert re.search(r"manual=(\d+) acceleration=(\d+) bench=(\d+)", restarted).groups() == matched.groups(), restarted
                restored = request_modes()
                assert_names(restored)
                assert restored[0]["seq"] != removed[0]["seq"], restored
                evidence.update(passed=True, restored_modes=restored, real_commander=True,
                    link_started_after_all_registrations=True, set_mode_bench=True, command_long_exit=True,
                    current_mode_actual_and_intended=True, removal_monitor_and_stale_name_clear=True,
                    registration_restored_without_permission=assert_no_permission(),
                    selection_is_not_readiness=True, flight_admission=False)
                return {name: value for name, value in evidence.items()
                        if name not in ("registrations_before_link", "initial_modes", "removed_registry", "removed_modes", "restored_modes",
                                        "selected_without_permission", "registration_restored_without_permission")}
            finally:
                evidence["wire_messages"] = observed
                (out / "mode-protocol-evidence.json").write_text(json.dumps(evidence, indent=2) + "\n")

        def verify_protocol_owner():
            # Native estimator observations only. Commander still exclusively
            # owns actual arming, mode and the coherent transition witness.
            client("slaaps_control_test", "selected", "estimator-only")
            client("mavlink", "boot_complete")
            client("mavlink", "stream", "-u", str(port), "-s", "TIMESYNC", "-r", "5")
            parameters = {
                "SLA_CMP_ADMIT": 1, "SLA_CMP_DISARM": 2,
                "SLA_CMP_AX_MIN": -5, "SLA_CMP_AX_MAX": 5,
                "SLA_CMP_AY_MIN": -5, "SLA_CMP_AY_MAX": 5,
                "SLA_CMP_AZ_MIN": -5, "SLA_CMP_AZ_MAX": 5, "SLA_CMP_A_NORM": 8,
                "SLA_CMP_V_MIN": 10000, "SLA_CMP_V_MAX": 1000000,
                "SLA_CMP_CMD_AGE": 1000000, "SLA_CMP_FUTURE": 500000,
                "SLA_CMP_CLOCK_U": 300000, "SLA_CMP_DRIFT": 50,
                "SLA_CMP_STAMP_U": 1000, "SLA_CMP_RTT": 500000, "SLA_CMP_MAP_US": 8000000,
                "SLA_STATUS_AGE": 1000000, "SLA_ARM_AGE": 1000000,
                "SLA_STATE_AGE": 500, "SLA_STATE_SKEW": 100, "SLA_QUAT_TOL": .01,
                **{f"SLA_{axis}_MEAS_R": 20 for axis in "RPY"},
                **{f"SLA_{axis}_MEAS_A": 100 for axis in "RPY"},
            }
            for name, value in parameters.items():
                client("param", "set", name, str(value))
            reapply()
            receiver = dialect.MAVLink(None)
            receiver.signing.secret_key = key
            receiver.robust_parsing = True
            peer.setblocking(False)
            messages, states, exchanges = [], [], []
            feedback_parameters, feedback_transitions = {}, []
            received_types, clock_receipts = {}, []
            receive_summary = dict(datagrams=0, maximum_batch=0, batch_limit_hits=0, budget_limit_hits=0)
            maximum_datagrams_per_pump = 64
            receive_budget_seconds = .005
            operation_sequence = 0
            keepalive = None
            last_keepalive = 0.
            reply_clock = False

            def producer_now():
                # A labeled synthetic producer clock with an explicit offset;
                # no PX4 HRT value is substituted for the remote timestamp.
                return time.monotonic_ns() // 1000 + 1234567

            def owner_status():
                text = client("listener", "slaaps_companion_status", "1", check=False)
                return {name: (value.lower() == "true" if value.lower() in ("true", "false") else int(value))
                        for name, value in re.findall(r"^\s*(\w+):\s+(-?\d+|[Tt]rue|[Ff]alse)\b", text, re.M)}

            def pump():
                nonlocal last_keepalive
                if keepalive and time.monotonic() - last_keepalive >= .2:
                    stamp = producer_now()
                    send(request(**keepalive, producer_monotonic_us=stamp,
                                 producer_expiry_monotonic_us=stamp + 500000))
                    last_keepalive = time.monotonic()
                # Other onboard telemetry shares this socket. One datagram per
                # listener invocation can leave current challenges behind old
                # telemetry; drain a bounded batch without changing any deadline.
                receive_until = time.monotonic() + receive_budget_seconds
                received = 0
                for _ in range(maximum_datagrams_per_pump):
                    try:
                        data, _ = peer.recvfrom(65535)
                    except BlockingIOError:
                        break
                    received += 1
                    receive_summary["datagrams"] += 1
                    received_us = time.monotonic_ns() // 1000
                    for message in receiver.parse_buffer(data) or []:
                        kind = message.get_type()
                        received_types[kind] = received_types.get(kind, 0) + 1
                        if kind not in ("TIMESYNC", "RESEARCH_ACCELERATION_STATUS"):
                            continue
                        if kind == "TIMESYNC":
                            clock_receipts.append(dict(received_monotonic_us=received_us, tc1=message.tc1,
                                                       ts1=message.ts1, target_system=message.target_system,
                                                       target_component=message.target_component, reply_enabled=reply_clock))
                            if len(clock_receipts) > 128:
                                del clock_receipts[0]
                            if message.tc1 != 0 or message.target_system != 42 or message.target_component != 191:
                                continue  # Stock TIMESYNC is not trusted-map evidence.
                        raw = message.get_msgbuf()
                        assert len(raw) > 2 and raw[0] == 0xfd and raw[2] & 1, "Unsigned protocol response"
                        assert message.get_srcSystem() == 1 and message.get_srcComponent() == 1, message
                        assert message.target_system == 42 and message.target_component == 191, message
                        if kind == "TIMESYNC":
                            if reply_clock and message.tc1 == 0 and message.ts1 > 0:
                                remote_ns = producer_now() * 1000
                                send(dialect.MAVLink_timesync_message(
                                    tc1=remote_ns, ts1=message.ts1, target_system=1, target_component=1))
                                exchanges.append(dict(echoed_challenge_ns=message.ts1, authenticated=True,
                                                      received_monotonic_us=received_us, producer_reply_ns=remote_ns))
                        else:
                            assert message.protocol_version == 0x10000 and not message.status_flags & 1, message
                            messages.append(message)
                    if time.monotonic() >= receive_until:
                        receive_summary["budget_limit_hits"] += 1
                        break
                receive_summary["maximum_batch"] = max(receive_summary["maximum_batch"], received)
                if received == maximum_datagrams_per_pump:
                    receive_summary["batch_limit_hits"] += 1
                if not received:
                    time.sleep(.001)  # Bound wait_wire idle polling without extending its timeout.

            def wait_owner(predicate, purpose, timeout=8):
                deadline = time.monotonic() + timeout
                state = {}
                while time.monotonic() < deadline:
                    pump()
                    state = owner_status()
                    if predicate(state):
                        states.append(dict(purpose=purpose, **state))
                        return state
                    time.sleep(.025)
                states.append(dict(purpose=f"{purpose}: timed out", **state))
                raise AssertionError(f"Companion owner {purpose}: {state}")

            def wait_wire(predicate, purpose):
                deadline = time.monotonic() + 8
                while time.monotonic() < deadline:
                    for message in reversed(messages):
                        if predicate(message):
                            return message
                    pump()
                raise AssertionError(f"Missing signed companion status: {purpose}")

            def session(state, session_id, operation=1, acknowledged=0, acknowledgement_time=0):
                nonlocal operation_sequence
                operation_sequence += 1
                return dialect.MAVLink_research_acceleration_session_message(
                    controller_instance_id=state["controller_instance_id"], transition_epoch=state["transition_epoch"],
                    clock_mapping_epoch=max(1, state["map_epoch"]), source_id=1, session_id=session_id,
                    operation_sequence=operation_sequence, producer_acknowledgement_monotonic_us=acknowledgement_time,
                    acknowledged_status_sequence=acknowledged, protocol_version=0x10000,
                    target_system=1, target_component=1, operation=operation, flags=0, reserved=[0] * 4)

            def action(value, completed):
                client("param", "set", "SLA_CMP_ACT", str(value))
                deadline = time.monotonic() + 8
                while client("param", "compare", "SLA_CMP_ACT", "0", check=False, return_code=True):
                    assert time.monotonic() < deadline, "Companion action was not consumed"
                    pump()
                    time.sleep(.025)
                # Consumption precedes execution in the serialized owner. Wait
                # past a post-consumption publication so a repeated command
                # cannot match the previous command's identical retained result.
                consumed_timestamp = owner_status().get("timestamp", 0)
                state = wait_owner(lambda s: s.get("timestamp", 0) > consumed_timestamp
                                   and s.get("action") == value and s.get("action_result") == (1 if completed else 2),
                                   f"action {value} {'completed' if completed else 'rejected'}")
                client("param", "compare", "SLA_CMP_ACT", "0")
                return state

            def enter(session_id):
                nonlocal keepalive, reply_clock
                state = wait_owner(lambda s: s.get("required_state_valid") and s.get("controller_instance_id")
                                   and not s.get("continuity_fault"), "fresh actual witness and synthetic estimator")
                keepalive = dict(controller_instance_id=state["controller_instance_id"],
                                 transition_epoch=state["transition_epoch"], clock_mapping_epoch=max(1, state["map_epoch"]),
                                 session_id=session_id)
                state = wait_owner(lambda s: s.get("transport_connected") and s.get("required_state_valid"),
                                   "matching signed source connected")
                send(session(state, session_id))
                wait_owner(lambda s: s.get("proposal_session") == session_id and s.get("proposal_deadline", 0) > s.get("timestamp", 0),
                           "authenticated proposal staged without session authority")
                prepared = action(1, True)
                assert prepared["prepared"] and not prepared["session_established"], prepared
                reply_clock = True
                mapped = wait_owner(lambda s: s.get("map_valid") and s.get("map_session") == session_id,
                                    "matched signed challenge freezes bounded clock map")
                reply_clock = False
                assert mapped["map_uncertainty_us"] > 0 and not mapped["session_established"], mapped
                keepalive.update(transition_epoch=mapped["transition_epoch"], clock_mapping_epoch=mapped["map_epoch"])
                send(session(mapped, session_id))
                wait_owner(lambda s: s.get("proposal_session") == session_id and s.get("proposal_deadline", 0) > s.get("timestamp", 0),
                           "fresh exact post-map session proposal")
                established = action(2, True)
                assert established["session_established"] and not established["acknowledgement_current"], established
                keepalive.update(transition_epoch=established["transition_epoch"], clock_mapping_epoch=established["map_epoch"])
                barrier = wait_wire(lambda m: m.status_class == 3 and m.status_sequence == established["barrier_sequence"]
                                    and m.controller_instance_id == established["controller_instance_id"]
                                    and m.transition_epoch == established["transition_epoch"]
                                    and m.clock_mapping_epoch == mapped["map_epoch"] and m.session_id == session_id,
                                    "the exact current session-establishment barrier")
                assert barrier.session_state == 2 and barrier.clock_mapping_state == 1, barrier
                # Conservative ACK creation must be strictly after the actual
                # barrier even at the earliest end of the frozen uncertainty.
                earliest = barrier.validator_time_us - mapped["map_offset_us"] + mapped["map_uncertainty_us"] + 1000
                while producer_now() <= earliest:
                    pump()
                send(session(established, session_id, 3, barrier.status_sequence + 1, producer_now()))
                wrong_operation = operation_sequence
                wait_wire(lambda m: m.status_class == 2 and m.correlation_sequence == wrong_operation and m.decision_code == 80,
                          "wrong barrier sequence rejected")
                assert not owner_status()["acknowledgement_current"]
                send(session(established, session_id, 3, barrier.status_sequence, producer_now()))
                acknowledged = wait_owner(lambda s: s.get("acknowledgement_current") and s.get("session_established"),
                                          "exact signed barrier ACK accepted")
                stamp = producer_now()
                send(request(**keepalive, producer_monotonic_us=stamp, producer_expiry_monotonic_us=stamp + 500000))
                sequence = request_sequence
                rejection = wait_wire(lambda m: m.status_class == 1 and m.correlation_sequence == sequence
                                      and m.decision_code == 32 and m.session_id == session_id,
                                      "current authenticated request rejected ModeNotSelected")
                assert rejection.arm_state == 1 and rejection.research_mode_state == 1 and rejection.session_state == 2, rejection
                assert acknowledged["map_deadline"] == mapped["map_deadline"], "Handshake renewed original map expiry"
                return acknowledged

            try:
                first = enter(7001)
                revoked = action(3, True)
                assert not revoked["session_established"] and not revoked["map_valid"] and not revoked["prepared"], revoked
                rejected = action(2, False)
                assert not rejected["session_established"] and not rejected["acknowledgement_current"], rejected
                second = enter(7002)
                assert second["controller_instance_id"] == first["controller_instance_id"] and second["binding_id"] != first["binding_id"]
                expiry = second["map_deadline"]
                expired = wait_owner(lambda s: s.get("timestamp", 0) >= expiry and not s.get("map_valid")
                                     and not s.get("session_established") and not s.get("acknowledgement_current"),
                                     "original map expiry closes current session despite fresh signed traffic", timeout=12)
                assert not expired["prepared"] and expired["transport_connected"], expired
                rejected = action(2, False)
                assert not rejected["session_established"], "Grant without fresh protocol entry revived an expired map"
                third = enter(7003)
                assert third["map_epoch"] > second["map_epoch"] and third["binding_id"] != second["binding_id"]
                wire = client("listener", "slaaps_status", "1")
                mode = int(re.search(r"acceleration=(\d+)", client("slaaps", "status")).group(1))
                can_run = int(re.search(r"can_run_mask:\s+(\d+)", wire).group(1))
                can_arm = int(re.search(r"can_arm_mask:\s+(\d+)", wire).group(1))
                assert not can_run & (1 << mode) and not can_arm & (1 << mode), wire
                assert re.search(r"manual_worker_active:\s+(?:False|false|0)\b", wire), wire
                assert re.search(r"bench_worker_active:\s+(?:False|false|0)\b", wire), wire
                action(3, True)
                # Keep the original disabled-feedback sessions above. This
                # additional path supplies synthetic sensor observations only;
                # Commander remains the actual authority/mode witness.
                feedback_parameters = {
                    "SLA_FB_EN": 1, "SLA_FB_AGE": 500000, "SLA_FB_BIAS_AGE": 1000000,
                    "SLA_FB_LAT_US": 1000, "SLA_FB_SETTLE_US": 10000,
                    "SLA_FB_F_MAX": 50, "SLA_FB_A_MAX": 20, "SLA_FB_GRAV": 9.8,
                    "EKF2_IMU_POS_X": 0, "EKF2_IMU_POS_Y": 0, "EKF2_IMU_POS_Z": 0,
                    "IMU_ACCEL_CUTOFF": 0,
                    # Isolate original feedback age expiry from the separate
                    # state/feedback skew rejection tested by the host suite.
                    "SLA_STATE_SKEW": 500,
                }
                for name, value in feedback_parameters.items():
                    client("param", "set", name, str(value))
                client("slaaps_control_test", "selected", "feedback-valid")
                reapply()
                feedback_ready = wait_owner(lambda s: s.get("feedback_required") and s.get("feedback_valid")
                                            and s.get("required_state_valid")
                                            and s.get("feedback_deadline", 0) > s.get("timestamp", 0),
                                            "explicit synthetic CG acceleration feedback settles ready")
                assert not feedback_ready["session_established"] and not feedback_ready["acknowledgement_current"], feedback_ready

                for session_id, operation, reason in (
                        (7101, "feedback-hold", "original acceleration sample expiry"),
                        (7102, "feedback-bias-replace", "applied bias publisher replacement"),
                        (7103, "feedback-raw-replace", "raw accelerometer publisher replacement")):
                    established = enter(session_id)
                    before = owner_status()
                    assert before["feedback_required"] and before["feedback_valid"] and before["required_state_valid"], before
                    assert before["session_established"] and before["acknowledgement_current"], before
                    original_deadline = before["feedback_deadline"]
                    assert before["map_deadline"] > original_deadline, before
                    client("slaaps_control_test", "selected", operation)
                    closed = wait_owner(lambda s: s.get("feedback_required") and not s.get("feedback_valid")
                                        and not s.get("required_state_valid") and not s.get("session_established")
                                        and not s.get("acknowledgement_current") and not s.get("map_valid"),
                                        f"{reason} closes actual owner state/session/ACK")
                    assert closed["timestamp"] < before["map_deadline"], (closed, before)
                    producer = client("slaaps_control_test", "selected", "status")
                    match = re.search(r"SLAAPS_FEEDBACK_STATUS enabled=(\d+) holding=(\d+) generation=(\d+) last_sample=(\d+)", producer)
                    sources = re.search(r"SLAAPS_FEEDBACK_SOURCES bias_revision=(\d+) raw_revision=(\d+)", producer)
                    assert match and sources, producer
                    enabled, holding, generation, held_sample = map(int, match.groups())
                    bias_revision, raw_revision = map(int, sources.groups())
                    assert enabled == holding == 1 and held_sample and generation and bias_revision and raw_revision, producer
                    held = client("listener", "vehicle_acceleration", "1")
                    assert int(re.search(r"\btimestamp_sample:\s+(\d+)", held).group(1)) == held_sample, held
                    assert re.search(r"\bbias_valid:\s+(?:True|true|1)\b", held), held
                    if operation == "feedback-hold":
                        assert closed["feedback_result"] == 5, closed  # AccelerationFeedbackResult::Stale
                        assert closed["timestamp"] >= held_sample + feedback_parameters["SLA_FB_AGE"] - feedback_parameters["SLA_FB_LAT_US"], (closed, producer)
                        assert closed["timestamp"] >= original_deadline, (closed, before)
                    else:
                        # The old acceleration sample is still within its
                        # original age window: the independently observed
                        # publisher replacement must be the earlier barrier.
                        assert closed["timestamp"] < original_deadline, (closed, before, producer)
                        field = "bias_publisher_revision" if operation == "feedback-bias-replace" else "raw_publisher_revision"
                        applied_revision = int(re.search(rf"\b{field}:\s+(\d+)", held).group(1))
                        assert applied_revision != (bias_revision if operation == "feedback-bias-replace" else raw_revision), (held, producer)
                    assert closed["transport_connected"] and closed["state_generation"] > established["state_generation"], closed
                    rejected = action(2, False)
                    assert not rejected["session_established"] and not rejected["acknowledgement_current"], rejected
                    client("slaaps_control_test", "selected", "feedback-valid")
                    restored = wait_owner(lambda s: s.get("feedback_required") and s.get("feedback_valid")
                                          and s.get("required_state_valid") and not s.get("session_established")
                                          and not s.get("acknowledgement_current") and not s.get("map_valid"),
                                          f"fresh feedback after {reason} cannot automatically revive protocol authority")
                    rejected = action(2, False)
                    assert not rejected["session_established"], "Grant without fresh Prepare/session revived feedback loss"
                    feedback_transitions.append(dict(session=session_id, cause=reason, before=before, closed=closed,
                                                     restored_without_authority=restored, producer=producer, held_acceleration=held))
                final_feedback = enter(7104)
                assert final_feedback["feedback_valid"] and final_feedback["required_state_valid"], final_feedback
                # Exercise the actual signed parser and production quaternion
                # getter. Valid full commands retain ordinary mode rejection;
                # missing/invalid attitude must fail before control admission.
                for quaternion, decision in (([1., 0., 0., 0.], 32),
                                             ([-1., 0., 0., 0.], 32),
                                             ([0., 0., 0., 0.], 38)):
                    stamp = producer_now()
                    send(request(**keepalive, producer_monotonic_us=stamp,
                                 producer_expiry_monotonic_us=stamp + 500000,
                                 protocol_version=0x20000, flags=1,
                                 desired_attitude_ned_from_body=quaternion))
                    sequence = request_sequence
                    response = wait_wire(lambda m: m.status_class == 1 and m.correlation_sequence == sequence
                                         and m.session_id == 7104 and m.decision_code == decision,
                                         f"signed full command quaternion {quaternion} decision {decision}")
                    assert response.arm_state == 1 and response.research_mode_state == 1, response
                final_wire = client("listener", "slaaps_status", "1")
                for field in ("manual_worker_active", "bench_worker_active"):
                    assert re.search(rf"\b{field}:\s+(?:False|false|0)\b", final_wire), final_wire
                assert not int(re.search(r"can_arm_mask:\s+(\d+)", final_wire).group(1)) & (1 << mode), final_wire
                assert not int(re.search(r"can_run_mask:\s+(\d+)", final_wire).group(1)) & (1 << mode), final_wire
                action(3, True)
                return dict(passed=True, actual_commander_witness=True, synthetic_estimator_only=True,
                            signed_matched_timesync=True, deliberate_prepare_and_grant=True,
                            exact_barrier_acknowledgement=True, wrong_barrier_rejected=True,
                            request_decision="ModeNotSelected", revoke_requires_protocol_reentry=True,
                            original_map_expiry_requires_protocol_reentry=True, successful_sessions=[7001, 7002, 7003],
                            feedback_enabled_sessions=[7101, 7102, 7103, 7104], synthetic_feedback=True,
                            signed_full_command_quaternion_decode=True, invalid_full_command_attitude_rejected=True,
                            original_feedback_expiry_revokes=True, bias_publisher_replacement_revokes=True,
                            raw_publisher_replacement_revokes=True, feedback_requires_deliberate_protocol_reentry=True,
                            acceleration_mode_selected=False, eligible_control=False, physical_outputs=False)
            finally:
                observations = {}
                try:
                    for topic in ("slaaps_companion_control", "slaaps_companion_packet_0"):
                        observations[topic] = client("listener", topic, "1", check=False)
                    observations["mavlink_status"] = client("mavlink", "status", check=False)
                except Exception as error:
                    observations["capture_error"] = repr(error)
                (out / "companion-protocol-evidence.json").write_text(json.dumps(
                    dict(states=states, exchanges=exchanges, signed_status=[m.to_dict() for m in messages],
                         received_types=received_types, receive_summary=receive_summary, clock_receipts=clock_receipts,
                         final_observations=observations,
                         parameters=parameters, feedback_parameters=feedback_parameters, feedback_transitions=feedback_transitions,
                         synthetic_clock_offset_us=1234567), indent=2) + "\n")
                client("slaaps_control_test", "selected", "stop", check=False)

        # The receiver constructs a mission manager. Supply its normal dataman
        # dependency so startup does not time out waiting for a client ID.
        client("dataman", "start", "-r")
        try:
            registered_before_link = wait_status(r"configured=1")
            client("mavlink", "start", "-u", str(port), "-o", str(peer.getsockname()[1]),
                   "-t", "127.0.0.1", "-m", "onboard", "-r", "10000")
            client("mavlink", "boot_complete")
            mode_protocol = verify_mode_protocol(registered_before_link)
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
            protocol = verify_protocol_owner()
            return dict(passed=True, actual_mavlink_parser=True, actual_supervisor=True,
                        synthetic_key_and_identity=True, loopback_only=True, signature_replay_rejected=True,
                        unsigned_and_wrong_key_rejected=True, source_target_binding=True,
                        receipt_expiry=True, signing_reload_requires_apply=True,
                        protocol_owner=protocol, mode_protocol=mode_protocol, command_admission=False, acceleration_control=False, physical_outputs=False)
        finally:
            client("mavlink", "stop-all", check=False)
            client("dataman", "stop", check=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=Path)
    parser.add_argument("--supervisor-only", action="store_true",
                        help="Run only the isolated actual-source supervisor regression for focused diagnosis")
    parser.add_argument("--signed-flight", action="store_true",
                        help="Use actual signed UDP ingress in the isolated capture-only flight handoff")
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
    dialect = load_mavlink_dialect(out, build) if args.signed_flight else None
    signed_flight = SignedFlightClient(dialect, supervisor_dir) if args.signed_flight else None
    supervisor_script = supervisor_dir / "startup.sh"
    supervisor_script.write_text("#!/bin/sh\nset -e\n. px4-alias.sh\n"
                                 "param set SYS_AUTOSTART 22001\nparam set MAV_TYPE 1\n"
                                 + (signed_flight.startup() if signed_flight else "")
                                 + f"slaaps_control_test supervisor{' signed' if signed_flight else ''}\n"
                                 "echo SLAAPS_SUPERVISOR_FINISHED\n")
    supervisor_result = dict(passed=False, isolated_process=True, synthetic_authority=True,
                             physical_outputs=False, binary_sha256=receipt["binary_sha256"])
    with (supervisor_dir / "px4.log").open("w") as supervisor_log, ExitStack() as supervisor_cleanup:
        if signed_flight:
            supervisor_cleanup.callback(signed_flight.close)
        supervisor_reader = supervisor_cleanup.enter_context((supervisor_dir / "px4.log").open())
        supervisor_process = subprocess.Popen(
            [str(binary), "-d", "-i", str(instance + 1000000), "-w", str(supervisor_dir),
             "-s", str(supervisor_script)], cwd=supervisor_dir,
            stdout=supervisor_log, stderr=subprocess.STDOUT)
        try:
            # Outer runner budget for the sequential Manual, flight-handoff,
            # recovery-loss, servo and EDF lifecycles (about 40 s typical). Their
            # individual live deadlines remain enforced by the fixture and production code.
            deadline = time.monotonic() + 90
            supervisor_output = ""
            while True:
                newest = supervisor_reader.read(65536 if signed_flight else -1)
                supervisor_output += newest
                if signed_flight:
                    signed_flight.pump(newest)
                if "SLAAPS_SUPERVISOR_FINISHED" in supervisor_output:
                    break
                assert supervisor_process.poll() is None and time.monotonic() < deadline, "Isolated supervisor failed"
                time.sleep(.001 if signed_flight else .1)
            supervisor_match = re.search(r"SLAAPS_SUPERVISOR_TEST_PASS checks=(\d+)", supervisor_output)
            assert supervisor_match, "Actual-source supervisor activation regression missing"
            assert "SLAAPS_OPERATOR_DIAGNOSTIC_TEST_PASS actual_source=1 emitted_mavlink_log=1 actual_mixer_routing=1" in supervisor_output
            if signed_flight:
                ingress_proof = re.search(r"SLAAPS_SIGNED_FLIGHT_TEST_PASS actual_parser=1 packet_injection=0 both_banks=1 "
                                          r"receiver=(\d+) signing=(\d+) clocks=(\d+) requests=(\d+)", supervisor_output)
                assert ingress_proof and all(int(value) > 0 for value in ingress_proof.groups()), "Signed active receiver/bank proof missing"
                assert signed_flight.exchanges and signed_flight.request_sequence, "No actual signed clock/full-command exchange"
                assert all(message["signed"] for message in signed_flight.sent
                           if message["type"] != "SETUP_SIGNING"), "Unsigned active protocol input"
                allocation_proof = re.search(r"SLAAPS_ACCEL_DIAGNOSTIC_TEST_PASS source=1 session=7303 request=(\d+) "
                                             r"stage=1 solver=5 qgc_log=1 modeled_only=1 guarded_capture=1", supervisor_output)
                assert allocation_proof, "Actual-source allocation diagnostic/log proof missing"
                rejected_sequence = int(allocation_proof.group(1))
                lateral_requests = [message for message in signed_flight.sent if message["type"] == "RESEARCH_ACCELERATION_REQUEST"
                                    and message["payload"]["session_id"] == 7303
                                    and message["payload"]["acceleration_y_frd_m_s2"] > .5]
                assert len(lateral_requests) == 1 and lateral_requests[0]["payload"]["sequence"] == rejected_sequence
                guarded_capture = re.search(r"SLAAPS_GUARDED_CAPTURE source=(\d+) session=(\d+) request=(\d+) config=(\d+) "
                                            r"capture=(\d+) observed=(\d+) producer=(\d+) expiry=(\d+)", supervisor_output)
                assert guarded_capture, "Successful real caller capture observation missing"
                source, session_id, request_id, configuration, captured, observed, created, expiry = map(int, guarded_capture.groups())
                lateral_payload = lateral_requests[0]["payload"]
                assert (source, session_id, request_id) == (1, 7303, rejected_sequence) and configuration > 0
                assert captured >= observed > 0 and created == lateral_payload["producer_monotonic_us"]
                assert expiry == lateral_payload["producer_expiry_monotonic_us"] and expiry > created
                # Revocation can correctly prevent a queued ACCEPTED status
                # from transmitting. The test-only observer runs only after
                # the actual captureControlInput returns true, and correlates
                # that validated canonical request with the signed raw sample.
                wire_acceptance_observed = any(message.get("mavpackettype") == "RESEARCH_ACCELERATION_STATUS"
                                               and message["status_class"] == 1 and message["correlation_sequence"] == rejected_sequence
                                               and message["source_id"] == 1 and message["session_id"] == 7303
                                               and message["decision_code"] == 1 and message["status_flags"] & 1
                                               for message in signed_flight.received)
                signed_flight.proven = True
                supervisor_result["allocator_rejected_signed_request"] = rejected_sequence
                supervisor_result["allocator_request_guarded_capture"] = dict(
                    source=source, session=session_id, sequence=request_id, configuration=configuration,
                    captured_us=captured, observed_us=observed, producer_created_us=created, producer_expiry_us=expiry,
                    wire_acceptance_observed=bool(wire_acceptance_observed))
                supervisor_result["signed_ingress_proof"] = dict(zip(
                    ("receiver_revision", "signing_generation", "clock_samples", "full_requests"),
                    map(int, ingress_proof.groups())))
            supervisor_result.update(passed=True, checks=int(supervisor_match.group(1)),
                                     operator_rejection_diagnostics=True, actual_mavlink_log=True, qgc_display=False,
                                     signed_parser_active_handoff=bool(signed_flight),
                                     active_handoff_post_parser=not bool(signed_flight))
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
    commander_edf_failure_captured = False

    def client(module, *arguments, check=True, return_code=False):
        assert process.poll() is None, "PX4 exited"
        result = subprocess.run([str(binary.parent / ("px4-" + module)), "--instance", str(instance),
                                 *map(str, arguments)], cwd=out, capture_output=True, text=True, timeout=10)
        output = result.stdout + result.stderr
        commands.append(dict(command=[module, *arguments], code=result.returncode, output=output))
        if check:
            assert result.returncode == 0, output
        return result.returncode if return_code else output

    def wait_status(pattern):
        nonlocal commander_edf_failure_captured
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            output = client("slaaps", "status", check=False)
            if re.search(pattern, output):
                return output
            if (not commander_edf_failure_captured and pattern == r"bench entry=16 active=1 target=5\b"
                    and (re.search(r"bench entry=(?:0|11|12|17|18)\b", output)
                         or re.search(r"power required=1 ready=0 status=9\b", output))):
                commander_edf_failure_captured = True
                capture = dict(wait_pattern=pattern, first_observed_status=output,
                               observed_python_monotonic_ns=time.monotonic_ns(), sampled_evidence_only=True,
                               causal_order_established=False, commands=[])
                destination = out / "commander-edf-first-failure.json"
                destination.write_text(json.dumps(capture, indent=2) + "\n")
                # Capture existing payloads only. All these topics have already
                # published during prior real-Commander Bench sessions. Keep the
                # original wait deadline, predicate and failure behavior intact.
                for module, arguments in [
                        ("slaaps_control_test", ("selected", "status")),
                        ("listener", ("slaaps_status", "1")), ("listener", ("battery_status", "1")),
                        ("listener", ("vehicle_status", "1")), ("listener", ("actuator_armed", "1")),
                        ("listener", ("failsafe_flags", "1")), ("listener", ("slaaps_vehicle_state", "1")),
                        ("listener", ("slaaps_manual_permission", "1")), ("listener", ("slaaps_manual_command", "1")),
                        ("slaaps_control_test", ("mixing", "status"))]:
                    observed = time.monotonic_ns()
                    try:
                        value = client(module, *arguments, check=False)
                    except (AssertionError, subprocess.TimeoutExpired) as error:
                        value = repr(error)
                    capture["commands"].append(dict(command=[module, *arguments],
                                                    started_python_monotonic_ns=observed,
                                                    completed_python_monotonic_ns=time.monotonic_ns(), output=value))
                    destination.write_text(json.dumps(capture, indent=2) + "\n")
            time.sleep(.15)
        raise AssertionError(f"Missing status {pattern}: {output}")

    result = dict(passed=False, scope="native_supervisor_and_mixer_capture_only_no_hardware",
                  build=str(build), binary_sha256=receipt["binary_sha256"],
                  production_supervisor_test_seam=False,
                  harness_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
    mode_link = ExitStack()
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
        result["native_operator_rejection_diagnostics"] = dict(actual_source=True,
            actual_mavlink_log=True, actual_mixer_routing=True, qgc_display=False)
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
        # Public synthetic record reference, not a physical measurement or
        # qualification assertion. The accepted tuple must survive export/restore.
        calibrated_profile = r"profile accepted=1 schema=1 record=1771 revision=7 declared_state=1\b"
        unaccepted_profile = r"profile accepted=0 schema=0 record=0 revision=0 declared_state=0\b"
        values = {"SLA_DATA_VER": 1, "SLA_DATA_REC": 1771, "SLA_DATA_STATE": 1, "SLA_DATA_REV": 7,
                  "SLA_IN_SOURCE": 2, "SLA_IN_INST": 2, "SLA_IN_SYS": 255, "SLA_IN_COMP": 190,
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
        wait_status(calibrated_profile)
        client("param", "set", "SLA_C1_PWM_CTR", "1501")
        wait_status(r"configuration valid=0")
        wait_status(unaccepted_profile)
        client("param", "set", "SLA_C1_PWM_CTR", "1500")
        time.sleep(.3)
        assert "configuration valid=0" in client("slaaps", "status")
        client("param", "set", "SLA_CFG_APPLY", "2")
        wait_status(r"configuration valid=1 use=0 apply=2")
        client("param", "set", "SLA_CFG_KIND", "1")
        client("param", "set", "SLA_CFG_APPLY", "3")
        wait_status(r"configuration valid=0 use=1 apply=3")
        rejected_profile = wait_status(r"configuration rejected parameter=SLA_PWR_INST\b")
        assert re.search(r"configuration valid=0 use=1 apply=3 fault=1\b", rejected_profile), rejected_profile
        client("param", "set", "SLA_CFG_KIND", "0")
        client("param", "set", "SLA_CFG_APPLY", "4")
        accepted_profile = wait_status(r"configuration valid=1 use=0 apply=4")
        assert "configuration rejected parameter=" not in accepted_profile, accepted_profile
        accepted_parameters = out / "accepted-configuration.bson"
        client("param", "save", str(accepted_parameters))
        assert accepted_parameters.is_file() and accepted_parameters.stat().st_size > 0
        client("slaaps_routing_test", "valid")
        wait_status(r"routing snapshot=1 inventory_fault=0 routing_fault=0 index=-1 bank=255 channel=0\b")
        routing_details = []
        # Exact indexes follow the fixture's MAIN1..8 then AUX1..8 inventory;
        # physical channels are one based. Missing/stale reports identify none.
        for scenario, inventory_fault, routing_fault, index, bank, channel in (
                ("duplicate", 0, 5, 15, 1, 8),
                ("unavailable", 0, 4, 10, 1, 3),
                ("calibration", 0, 8, 10, 1, 3),
                ("stale", 3, 1, -1, 255, 0)):
            client("slaaps_routing_test", scenario)
            wait_status(rf"routing snapshot=0 inventory_fault={inventory_fault} routing_fault={routing_fault} "
                        rf"index={index} bank={bank} channel={channel}\b")
            routing_wire = client("listener", "slaaps_status", "1")
            for field, value in (("io_index", index), ("io_bank", bank), ("io_channel", channel)):
                assert re.search(rf"\b{field}:\s+{value}\b", routing_wire), routing_wire
            routing_details.append(dict(scenario=scenario, inventory_fault=inventory_fault,
                                        routing_fault=routing_fault, index=index, bank=bank, channel=channel))
            # Let the next stale capture supersede a known AUX3 calibration
            # error directly; its physical detail must clear, not linger.
            if scenario != "calibration":
                client("slaaps_routing_test", "valid")
                wait_status(r"routing snapshot=1 inventory_fault=0 routing_fault=0 index=-1 bank=255 channel=0\b")
        time.sleep(1.1)
        wait_status(r"routing snapshot=0 inventory_fault=3 routing_fault=1 index=-1 bank=255 channel=0\b")
        result["operator_rejection_details"] = dict(passed=True, actual_supervisor=True,
            invalid_apply_parameter="SLA_PWR_INST", corrected_apply_clears_parameter=True,
            routing=routing_details, successful_routing_clears_detail=True,
            inventory_failure_supersedes_channel_detail=True, qgc_gui_verified=False)
        for _ in range(3):
            client("slaaps", "stop")
            client("slaaps", "start")
            restarted = wait_status(r"configured=1")
            assert "configuration valid=1 use=0 apply=4" in restarted, "Exact accepted profile must restore disarmed"
            assert re.search(calibrated_profile, restarted), restarted
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
        wait_status(unaccepted_profile)
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
        wait_status(calibrated_profile)
        client("param", "set", "SLA_DATA_REC", "0")
        client("param", "set", "SLA_CFG_APPLY", "6")
        wait_status(r"configuration valid=0 use=0 apply=6\b")
        wait_status(r"configuration rejected parameter=SLA_DATA_REC\b")
        wait_status(unaccepted_profile)
        for name, value in {"SLA_DATA_REC": 1772, "SLA_DATA_REV": 8, "SLA_DATA_STATE": 2}.items():
            client("param", "set", name, str(value))
        client("param", "set", "SLA_CFG_APPLY", "7")
        wait_status(r"configuration valid=1 use=0 apply=7\b")
        wait_status(r"profile accepted=1 schema=1 record=1772 revision=8 declared_state=2\b")
        qualified_status = client("listener", "slaaps_status", "1")
        for field in ("can_arm_mask", "manual_preparation_session"):
            assert re.search(rf"\b{field}:\s+0\b", qualified_status), qualified_status
        qualified_can_run = int(re.search(r"\bcan_run_mask:\s+(\d+)\b", qualified_status).group(1))
        assert not qualified_can_run & sum(1 << int(mode) for mode in modes[:2]), qualified_status
        for field in ("manual_worker_active", "bench_worker_active"):
            assert re.search(rf"\b{field}:\s+(?:False|false|0)\b", qualified_status), qualified_status
        client("slaaps", "stop")
        client("param", "load", str(accepted_parameters))
        client("slaaps", "start")
        wait_status(r"configuration valid=1 use=0 apply=4\b")
        wait_status(calibrated_profile)
        result["profile_provenance"] = dict(passed=True, actual_native_apply=True,
            synthetic_operator_declared_record=True, schema_version=1, calibrated_record=1771,
            calibrated_revision=7, qualified_record=1772, qualified_revision=8,
            invalid_claim_parameter="SLA_DATA_REC", accepted_only_display=True,
            numerical_edit_revokes_claim=True, save_export_restart_restore=True,
            qualified_grants_no_flight_or_output_permission=True, physical_measurement_verified=False)
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
        # This wall-clock lifecycle test is not controller timing qualification.
        # Retained f068fduk evidence shows a 32.9 ms selected-state gap correctly
        # tripping the 20 ms portable fixture before its operator-window expiry.
        # Match the synthetic pilot/status budgets; production defaults and
        # the focused numerical freshness tests retain their original limits.
        client("param", "set", "SLA_STATE_AGE", "100")
        client("param", "set", "SLA_MAN_WAIT", "5")  # Synthetic, non-actuating operator window.
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
        first_preparation_deadline = int(re.search(r"manual preparation=2 .*deadline=(\d+)", prepared).group(1))
        assert first_preparation_deadline - first_session == 5_000_000, prepared
        for bank in ("main", "aux"):
            ready_wire = client("listener", "slaaps_manual_ready_" + bank, "1")
            assert re.search(r"session:\s+" + str(first_session) + r"\b", ready_wire), ready_wire
        # A completed non-actuating handshake must survive its one-second
        # response budget while the operator waits, without minting a new
        # session or extending the original configured readiness deadline.
        operator_wait_started = time.monotonic()
        while time.monotonic() - operator_wait_started < 1.2:
            waiting = client("slaaps", "status")
            assert re.search(rf"manual preparation=2 session={first_session} banks=3 deadline={first_preparation_deadline}\b", waiting), waiting
            time.sleep(.02)
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
        expiring = reenter_manual()
        operator_deadline = int(re.search(r"manual preparation=2 .*deadline=(\d+)", expiring).group(1))
        wait_status(r"manual preparation=3")  # Original five-second operator window is immutable.
        expired_wire = client("listener", "slaaps_status", "1")
        assert int(re.search(r"\btimestamp:\s+(\d+)", expired_wire).group(1)) >= operator_deadline, expired_wire
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
        # Cover the explicit 60ms ACK window plus 80ms first-command reserve
        # within the original report age; production reserve checks stay strict.
        edf_power_age_us = 200000
        client("slaaps_control_test", "selected", "neutral")
        client("slaaps_control_test", "selected", "power-valid")
        for name, value in {"SLA_CFG_KIND": 2, "SLA_BENCH_CH": 5, "SLA_BENCH_EN": 0,
                            "SLA_BENCH_SEC": 10, "SLA_EDF_BMAX": .25,
                            "SLA_EDF_RISE": 2, "SLA_EDF_FALL": 2, "SLA_PWR_AGE": edf_power_age_us}.items():
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
        # Prepare the unsigned command link while disarmed, so setup does not
        # consume the unchanged bounded armed commissioning duration.
        if dialect is None:
            dialect = load_mavlink_dialect(out, build)
        mode_peer = mode_link.enter_context(socket.socket(socket.AF_INET, socket.SOCK_DGRAM))
        mode_peer.bind(("127.0.0.1", 0))
        mode_peer.setblocking(False)
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as reservation:
            reservation.bind(("127.0.0.1", 0))
            mode_port = reservation.getsockname()[1]
        mode_destination = ("127.0.0.1", mode_port)
        client("dataman", "start", "-r")
        mode_link.callback(client, "dataman", "stop", check=False)
        client("mavlink", "start", "-u", str(mode_port), "-o", str(mode_peer.getsockname()[1]),
               "-t", "127.0.0.1", "-m", "onboard", "-r", "10000")
        mode_link.callback(client, "mavlink", "stop-all", check=False)
        client("mavlink", "boot_complete")
        # Labeled synthetic raw IMU/baro/mag and avionics rail feed Commander's
        # unmodified ordinary checks. They qualify no physical vehicle health.
        client("slaaps_control_test", "selected", "health-valid")
        health_deadline = time.monotonic() + 2
        while True:
            health = re.search(r"SLAAPS_HEALTH_STATUS running=(\d+) samples=(\d+)",
                               client("slaaps_control_test", "selected", "status"))
            if health and health.group(1) == "1" and int(health.group(2)) > 0:
                break
            assert time.monotonic() < health_deadline, "Synthetic health inputs did not publish"
            time.sleep(.02)
        # Arm only after Commander's own report accepts the new inputs; an
        # immediate arm was observed to precede that acceptance.
        health_started = time.monotonic()
        health_present = (1 << 1) | (1 << 27) | (1 << 28) | (1 << 29)  # baro, mag, accel, gyro
        while True:
            report = client("listener", "health_report", "1")
            present = int(re.search(r"health_is_present_flags:\s+(\d+)", report).group(1))
            errors = int(re.search(r"health_error_flags:\s+(\d+)", report).group(1))
            if present & health_present == health_present and not errors:
                break
            assert time.monotonic() < health_started + 6, report
            time.sleep(.1)
        health_settle_seconds = round(time.monotonic() - health_started, 3)
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
            # Commander learns Bench readiness through its 300 ms external
            # arming-check poll; arm only once its failsafe flags reflect it.
            prevent_deadline = time.monotonic() + 1.5
            while True:
                flags = client("listener", "failsafe_flags", "1")
                prevent = int(re.search(r"mode_req_prevent_arming:\s+(\d+)", flags).group(1))
                if not prevent & bench_mask:
                    break
                assert time.monotonic() < prevent_deadline, flags
                time.sleep(.02)
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
            # Ordinary arming only: no forced fallback. A rejection is a real
            # Commander/SLAAPS gate failure and must fail this test.
            assert re.search(r"result:\s+0\b", acknowledgment), (
                acknowledgment, client("listener", "health_report", "1", check=False),
                client("listener", "failsafe_flags", "1", check=False))
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
            if edf_session == 0:
                result["armed_unsupported_mode_rejection"] = verify_armed_mode_rejections(
                    client, out, dialect, mode_peer, mode_destination, bench_id)
                mode_link.close()
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
        # Later lifecycles keep their previous inputs, without these sensors.
        client("slaaps_control_test", "selected", "health-stop")
        result["production_edf_commissioning"] = dict(passed=True, actual_commander=True, actual_supervisor=True,
            actual_mixer=True, synthetic_configuration=True, synthetic_selected_inputs=True, synthetic_permission=False,
            hardware_interface="capture_only", ordinary_arming_accepted=True, forced_internal_test_arm=False,
            synthetic_health_inputs=True, synthetic_health_settle_seconds=health_settle_seconds,
            normal_vehicle_health_qualified=False,
            mode_id=bench_id, target=5, normalized_ceiling=.25, duration_seconds=10,
            disarmed_preparation_no_output=True, actual_armed_state=True, flight_admitted=False,
            measured_condition_valid=False, parked_canards=True, low_throttle_zero_command=True,
            explicit_stop=True, observed_ordinary_disarm=True, completed_entry_cannot_revive=True, aux_output=edf_aux,
            synthetic_power=True, synthetic_power_maximum_age_us=edf_power_age_us,
            selected_power_required=True, stale_power_blocks_readiness=True,
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
            bounded_operator_wait_seconds=5, prepared_beyond_handshake_without_renewal=True,
            ordinary_arm_denied=True)
        result["companion_transport"] = verify_companion_transport(client, wait_status, out, dialect)
        result.update(passed=True, supervisor_endpoint_lifecycles=9, modes=list(modes), physical_outputs=False,
                      configuration_restart_checks=9, parameter_export_import=True,
                      accepted_parameters_sha256=hashlib.sha256(accepted_parameters.read_bytes()).hexdigest(),
                      qgc_gui_verified=False)
    finally:
        try:
            mode_link.close()
        except (AssertionError, subprocess.TimeoutExpired):
            pass
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
