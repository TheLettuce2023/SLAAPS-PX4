#!/usr/bin/env python3
"""Explicit opt-in C1H host experiment. Never connects to PX4/Gazebo/serial."""

import argparse
from contextlib import nullcontext
import ctypes as C
import gc
import hashlib
import json
import os
from pathlib import Path
import secrets
import subprocess
import sys
import tempfile
import time

HERE = Path(__file__).resolve().parent
WIRE = HERE.parent / "wire"
sys.path.insert(0, str(WIRE))
from codec import PROTOCOL_VERSION, decode_status, encode_request, encode_session
import test_generated_mavlink as generated
from host_clock import HostClock
from live_transport import Guidance, LoopbackReceiver, TransportError
from native_bridge import Config, NativeOwner, OperatorDriver, load_native
from campaign_window import campaign_window
from check_live_evidence import mutation_check, score


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def identifier():
    return secrets.randbits(63) + 1


class Experiment:
    """Trusted scenario runtime; its owner/console handles never enter Guidance."""

    def __init__(self, library, module, trace, label):
        self.trace, self.label = trace, label
        self.clock = HostClock()
        self.manifest = self.clock.qualify(library.c1h_now_ns)
        self.interval = self.clock.map_interval(self.clock.now_us(), 10_000_000)
        key = secrets.token_bytes(32)
        self.config = Config(abi_version=1, struct_size=C.sizeof(Config),
            key=(C.c_uint8 * 32).from_buffer_copy(key), run_id=identifier(),
            source_id=identifier(), session_id=identifier(), controller_id=identifier(),
            channel_id=identifier(), map_epoch=1, signing_seed=self.clock.now_ns() // 10000,
            map_start_us=self.interval.valid_from_us, map_end_us=self.interval.valid_until_us,
            maximum_receipt_age_us=60_000, maximum_confirmation_age_us=60_000)
        self.owner = NativeOwner(library, self.config, self.clock)
        self.guidance = Guidance(module, key, self.config.signing_seed)
        for index in range(32):
            self.config.key[index] = 0
        self.receiver = LoopbackReceiver()
        self.operator = None
        self.latest_status = None
        self.last = None
        self.operation_sequence = 0
        self.request_sequence = 0
        self.state = 30
        self.queue = []
        self.queue_mode = "normal"
        self.queue_drops = 0
        self.returned = []
        self.accepted = []
        self.last_accepted_us = 0
        self.ack_us = 0
        self.record("run", manifest=self.manifest, map=self.interval.evidence(),
                    identities={name: getattr(self.config, name) for name in (
                        "run_id", "source_id", "session_id", "controller_id", "channel_id")},
                    sender=self.guidance.socket.getsockname(), receiver=self.receiver.address,
                    state_provenance="harness-simulated", key_policy="fresh ephemeral in-memory")

    def record(self, event, **fields):
        require(len(self.trace) < 20000, "bounded host trace capacity exceeded")
        self.trace.append(dict(run=self.label, event=event, **fields))

    def close(self):
        try:
            if self.operator is not None and not self.owner.closed:
                self.operator.detach()
            self.clock.check_environment()
        finally:
            self.owner.close()
            self.guidance.close()
            self.receiver.close()

    def flush(self):
        while self.queue:
            frame, expected = self.queue.pop(0)
            sent_ns = self.clock.now_ns()
            self.receiver.status(frame, self.guidance.socket.getsockname())
            status, returned_ns = self.guidance.status(self.receiver.address, self.config.controller_id)
            require(status == expected, "returned status differs from actual native report")
            self.latest_status = status
            self.returned.append((status, returned_ns))
            self.record("status_return", sent_ns=sent_ns, returned_ns=returned_ns, status=status)

    def observe(self, operation, triple, snapshots=False):
        result, pre, post = triple
        self.last = result
        reports = []
        for encoded in result.reports[:result.report_count]:
            status = decode_status(bytes(encoded.payload))
            require((status["status_sequence"], status["transition_epoch"], status["validator_time_us"],
                     status["status_class"], status["decision_code"], status["status_flags"]) ==
                    (encoded.report_sequence, encoded.transition_epoch, encoded.validator_time_us,
                     encoded.status_class, encoded.decision_code, encoded.status_flags),
                    "native status metadata mismatch")
            require(0 < encoded.frame_length <= 280, "native status frame length")
            reports.append(status)
            if status["status_class"] == 1 and status["decision_code"] == 1 and status["status_flags"] & 1:
                self.last_accepted_us = status["validator_time_us"]
            # Ordinary poll/access snapshots are locally observed, not a >1Hz diagnostic flood.
            selected = status["status_class"] != 5 or snapshots
            disposition = "local_observation"
            if selected:
                if self.queue_mode == "loss":
                    disposition = "injected_status_loss"
                elif len(self.queue) == 2:
                    disposition = "injected_queue_drop_newest"
                    self.queue_drops += 1
                else:
                    self.queue.append((bytes(encoded.frame[:encoded.frame_length]), status))
                    disposition = "enqueued"
            self.record("status_disposition", status_sequence=status["status_sequence"],
                        disposition=disposition)
            if self.queue_mode == "normal":
                self.flush()
        self.record("native", operation=operation, pre_ns=pre, post_ns=post,
                    now_us=result.now_us, completion_us=result.completion_us,
                    received_us=result.received_us, parser_reason=result.parser_reason,
                    signature_verified=result.signature_verified, packet_kind=result.packet_kind,
                    receipt_offered=result.receipt_offered, native_code=result.native_code,
                    category=result.code_category, terminal=result.terminal,
                    closed=result.admission_closed, eligible=result.has_eligible_request,
                    request_sequence=result.request_sequence, request_created_us=result.request_created_us,
                    request_expiry_us=result.request_expiry_us,
                    acceleration=list(result.acceleration_frd_m_s2), reports=reports)
        self.trace[-1].update(last_admitted_us=self.last_accepted_us,
                             no_fresh=bool(self.last_accepted_us and result.now_us >= self.last_accepted_us + 40_000))
        return result

    def tick(self, state=None, snapshots=False):
        if state is not None:
            self.state = state
        return self.observe("tick", self.owner.call("tick", self.state), snapshots)

    def access(self):
        return self.observe("access", self.owner.call("access"))

    def exchange(self, frame, label="request", foreign=False):
        self.clock.check_environment()
        if foreign:
            # Actual unexpected UDP peer: the receiver socket sends to itself.
            # This opens no third endpoint and does not forge trusted metadata.
            sent_pre = self.clock.now_ns()
            self.receiver.status(frame, self.receiver.address)
            sent_post = self.clock.now_ns()
        else:
            sent_pre, sent_post = self.guidance.send(frame, self.receiver.address)
        data, received_ns, bound = self.receiver.receive(self.guidance.socket.getsockname())
        require(data == frame, "loopback bytes changed")
        triple = self.owner.receive(data, received_ns, self.config.channel_id if bound
                                    else self.config.channel_id + 1)
        self.record("datagram", label=label, send_pre_ns=sent_pre, send_post_ns=sent_post,
                    received_ns=received_ns, admission_pre_ns=triple[1], admission_post_ns=triple[2],
                    bound=bound, byte_count=len(data))
        return self.observe("receive:" + label, triple)

    def session(self, operation):
        require(self.latest_status is not None, "guidance has no returned controller status")
        self.operation_sequence += 1
        status = self.latest_status
        return dict(controller_instance_id=status["controller_instance_id"],
            transition_epoch=status["transition_epoch"], clock_mapping_epoch=status["clock_mapping_epoch"],
            source_id=self.config.source_id, session_id=self.config.session_id,
            operation_sequence=self.operation_sequence, producer_acknowledgement_monotonic_us=0,
            acknowledged_status_sequence=0, protocol_version=PROTOCOL_VERSION,
            target_system=1, target_component=1, operation=operation, flags=0, reserved=bytes(4))

    def send_session(self, fields, label):
        encode_session(fields)
        return self.exchange(self.guidance.frame("session", fields), label)

    def consent(self, intent):
        # Explicit scenario-driver action, never called by Guidance or packet dispatch.
        self.record("operator_intent", intent=intent, at_ns=self.clock.now_ns())
        if self.operator is None:
            self.operator = OperatorDriver(self.owner, identifier())
        source, session = (0, 0) if intent == 2 else (self.config.source_id, self.config.session_id)
        prepared = self.observe("operator_prepare", self.operator.prepare(intent, source, session))
        require(prepared.ticket != 0 and prepared.console_disposition == 0, "operator prepare failed")
        confirmed = self.observe("operator_confirm", self.operator.confirm(prepared.ticket))
        require(confirmed.console_disposition == 1 and confirmed.event_consumed == 1
                and confirmed.has_adapter_code == 1 and confirmed.adapter_code == 1,
                "independent operator action did not complete")
        return confirmed

    def acknowledge(self):
        fields = self.session(3)
        # The barrier comes from a real returned status, never a guessed counter.
        barriers = [status for status, _ in self.returned if status["status_class"] == 3
                    and status["transition_epoch"] == self.latest_status["transition_epoch"]]
        require(bool(barriers), "no returned barrier for ACK")
        barrier = barriers[-1]
        now = self.clock.now_us()
        while now <= barrier["validator_time_us"] + 1:
            now = self.clock.now_us()
        fields["producer_acknowledgement_monotonic_us"] = now
        fields["acknowledged_status_sequence"] = barrier["status_sequence"]
        result = self.send_session(fields, "ack")
        require(result.parser_reason == 0 and result.native_code == 1, "ACK rejected")
        self.ack_us = now

    def establish(self):
        self.tick()
        offered = self.send_session(self.session(1), "establish_offer")
        require(offered.receipt_offered == 1, "authenticated establish receipt not offered")
        require(not self.access().has_eligible_request, "wire establishment granted itself authority")
        self.consent(0)
        self.tick(31)
        require(not self.access().has_eligible_request, "armed transition bypassed ACK")
        _, probe = self.request()
        rejected = self.exchange(probe, "pre_ack_probe")
        require(rejected.parser_reason == 0 and rejected.code_category == 2 and rejected.native_code == 12
                and not self.access().has_eligible_request, "request bypassed missing ACK")
        self.acknowledge()

    def request(self, **changes):
        created_ns = self.clock.now_ns()
        while created_ns // 1000 <= self.ack_us + 2:
            created_ns = self.clock.now_ns()
        created = created_ns // 1000
        require(self.interval.covers(created, created + 80_000), "run map coverage exhausted")
        self.request_sequence += 1
        fields = dict(source_id=self.config.source_id, session_id=self.config.session_id,
            sequence=self.request_sequence, producer_monotonic_us=created,
            producer_expiry_monotonic_us=created + 80_000,
            controller_instance_id=self.latest_status["controller_instance_id"],
            transition_epoch=self.latest_status["transition_epoch"],
            clock_mapping_epoch=self.latest_status["clock_mapping_epoch"],
            acceleration_x_frd_m_s2=1.0, acceleration_y_frd_m_s2=-0.5, acceleration_z_frd_m_s2=0.25,
            protocol_version=PROTOCOL_VERSION, target_system=1, target_component=1,
            frame=1, quantity=1, reference_point=1, flags=0, reserved=bytes(4))
        fields.update(changes)
        encode_request(fields)
        self.record("creation", actual_ns=created_ns, fields={k: v for k, v in fields.items() if k != "reserved"},
                    injected_fields=sorted(changes))
        return fields, self.guidance.frame("request", fields)

    def accepted_request(self):
        fields, frame = self.request()
        result = self.exchange(frame)
        require(result.parser_reason == 0 and result.signature_verified == 1,
                "valid request not authenticated")
        require(result.code_category == 2 and result.native_code == 1
                and any(status.status_class == 1 and status.decision_code == 1
                        and decode_status(bytes(status.payload))["correlation_sequence"] == fields["sequence"]
                        for status in result.reports[:result.report_count]), "request not actually accepted")
        accessed = self.access()
        require(accessed.has_eligible_request == 1 and accessed.request_sequence == fields["sequence"]
                and accessed.request_created_us == fields["producer_monotonic_us"]
                and accessed.request_expiry_us == fields["producer_expiry_monotonic_us"]
                and list(accessed.acceleration_frd_m_s2) == [1.0, -0.5, 0.25],
                "complete request-to-native-access path failed")
        self.accepted.append(fields)
        self.last_accepted_us = result.now_us
        return fields, frame


def nominal(experiment):
    experiment.establish()
    start = experiment.clock.now_ns()
    release = start
    observer = start
    ticks = []
    releases = []
    # Fixed 20 requests / 380ms release campaign, followed by 100ms silence.
    for _ in range(20):
        while experiment.clock.now_ns() < release:
            now = experiment.clock.now_ns()
            if now >= observer:
                ticks.append(experiment.tick().now_us)
                observer += 1_000_000
            else:
                time.sleep(max(0, min((release - now) / 1e9, 0.0002)))
        releases.append(experiment.clock.now_ns())
        experiment.accepted_request()
        release += 20_000_000
    last = experiment.accepted[-1]
    silence_start = experiment.clock.now_ns()
    silence = []
    diagnostic = False
    while experiment.clock.now_ns() - silence_start < 100_000_000:
        result = experiment.access()
        silence.append((result.now_us, bool(result.has_eligible_request)))
        if result.now_us >= last["producer_monotonic_us"] + 60_000:
            require(not result.has_eligible_request, "command remained eligible beyond conservative hard age")
        if not diagnostic and result.now_us >= experiment.last_accepted_us + 40_000:
            experiment.tick(snapshots=True)
            experiment.record("no_fresh_diagnostic", since_admission_us=experiment.last_accepted_us,
                              observed_us=result.now_us)
            diagnostic = True
        time.sleep(0.0005)
    experiment.record("nominal_schedule", releases_ns=releases, observer_us=ticks,
                      silence=silence, diagnostic_sent=diagnostic, request_count=20, period_ns=20_000_000,
                      campaign_start_ns=start, campaign_end_ns=experiment.clock.now_ns())
    require(not experiment.access().has_eligible_request, "silence retained demand")


def faults(experiment):
    experiment.establish()
    fields, accepted_frame = experiment.accepted_request()
    replay = experiment.exchange(accepted_frame, "signing_replay")
    require(replay.parser_reason != 0, "replayed signed datagram admitted")
    mutated = bytearray(accepted_frame)
    mutated[-1] ^= 1
    require(experiment.exchange(bytes(mutated), "bad_signature").parser_reason != 0, "bad signature admitted")
    _, fresh = experiment.request()
    require(experiment.exchange(fresh, "foreign_channel", foreign=True).parser_reason != 0,
            "foreign channel admitted")
    require(experiment.exchange(fresh, "correct_channel_after_foreign").parser_reason == 0,
            "foreign channel poisoned valid frame")
    require(experiment.access().has_eligible_request, "valid post-foreign request inaccessible")
    for name, bad in (("truncated", fresh[:-1]), ("concatenated", fresh + fresh),
                      ("trailing", fresh + bytes(1)), ("split_a", fresh[:30]), ("split_b", fresh[30:])):
        require(experiment.exchange(bad, name).parser_reason != 0, name + " admitted")
    experiment.accepted_request()
    # Real signed duplicate command with newer signing time is not a new demand.
    before = experiment.access().request_sequence
    duplicate = experiment.exchange(experiment.guidance.frame("request", experiment.accepted[-1]), "command_duplicate")
    after_duplicate = experiment.access()
    require(duplicate.parser_reason == 0 and any(status.decision_code == 12
            for status in duplicate.reports[:duplicate.report_count])
            and after_duplicate.request_sequence == before
            and after_duplicate.request_created_us == experiment.accepted[-1]["producer_monotonic_us"]
            and after_duplicate.request_expiry_us == experiment.accepted[-1]["producer_expiry_monotonic_us"],
            "duplicate command refreshed or replaced demand")
    # The reverse channel is entirely absent, then held with finite capacity.
    experiment.queue_mode = "loss"
    experiment.accepted_request()
    experiment.queue_mode = "hold"
    for _ in range(5):
        experiment.accepted_request()
    require(experiment.queue_drops > 0 and len(experiment.queue) == 2,
            "bounded status backpressure was not exercised")
    experiment.queue_mode = "normal"
    experiment.flush()
    # Simulated required-state loss is a real trusted-context transition.
    experiment.tick(31 & ~8)
    require(not experiment.access().has_eligible_request, "required-state loss retained demand")
    experiment.tick(31)
    require(not experiment.access().has_eligible_request, "state recovery silently revalidated demand")
    experiment.acknowledge()
    experiment.accepted_request()
    experiment.tick(30)
    require(experiment.send_session(experiment.session(2), "revoke_offer").receipt_offered == 1,
            "revoke receipt missing")
    experiment.consent(1)
    require(not experiment.access().has_eligible_request, "revocation retained eligibility")
    return accepted_frame


def latch(experiment):
    experiment.establish()
    experiment.accepted_request()
    # Advance using actual readings; fault only the later wire producer field.
    time.sleep(0.001)
    previous, _ = experiment.accepted_request()
    regressed = previous["producer_monotonic_us"] - 1
    fields, frame = experiment.request(producer_monotonic_us=regressed,
                                      producer_expiry_monotonic_us=regressed + 80_000)
    result = experiment.exchange(frame, "injected_producer_regression")
    require(any(status.decision_code == 18 for status in result.reports[:result.report_count]),
            "producer regression did not latch")
    require(not experiment.access().has_eligible_request, "latched request remained eligible")
    experiment.tick(30)
    experiment.consent(2)
    require(not experiment.access().has_eligible_request, "clear restored old demand")


def stale(experiment):
    experiment.establish()
    fields, frame = experiment.request()
    release_after = fields["producer_monotonic_us"] + 65_000
    while experiment.clock.now_us() < release_after:
        experiment.tick()
        time.sleep(0.0005)
    result = experiment.exchange(frame, "injected_delivery_delay")
    require(result.parser_reason == 0 and any(status.decision_code == 20
            for status in result.reports[:result.report_count]), "stale command did not reject as too old")
    require(not experiment.access().has_eligible_request, "stale command became accessible")
    experiment.accepted_request()
    experiment.tick(31 & ~16)
    require(not experiment.access().has_eligible_request, "invalid clock map retained demand")
    experiment.tick(31)
    require(not experiment.access().has_eligible_request, "clock map recovery restored old demand")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run-live", action="store_true", help="explicitly allow isolated loopback/key experiment")
    parser.add_argument("--build-only", action="store_true")
    parser.add_argument("--build-type", choices=("Debug", "Release"), default="Debug")
    parser.add_argument("--diagnostic-gc", action="store_true",
                        help="observe host GC intervals under the recorded bounded-campaign policy")
    arguments = parser.parse_args()
    require(arguments.run_live != arguments.build_only, "select exactly one of --run-live or --build-only")
    require("MAV_IGNORE_CRC" not in os.environ, "MAV_IGNORE_CRC bypass forbidden")
    generated.git_clean(generated.MAVLINK, generated.PIN_MAVLINK)
    generated.git_clean(generated.PYMAVLINK, generated.PIN_PYMAVLINK)
    generated.validate_and_scan()
    output = Path(tempfile.mkdtemp(prefix="c1h_host_", dir="/var/tmp"))
    print("C1H owned evidence directory: " + str(output), flush=True)
    checks = []

    def run_check(arguments):
        completed = subprocess.run(arguments, text=True, capture_output=True, timeout=120)
        record = dict(arguments=list(map(str, arguments)), exit_code=completed.returncode,
                      stdout=completed.stdout, stderr=completed.stderr)
        checks.append(record)
        (output / "checks.json").write_text(json.dumps(checks, indent=2), encoding="utf-8")
        print(completed.stdout + completed.stderr, flush=True)
        require(completed.returncode == 0, "host prerequisite check failed; see checks.json")

    c_output, _, module_path = generated.generate(output)
    generated.check_header(c_output)
    build = output / "build"
    run_check(["cmake", "-S", HERE, "-B", build,
          "-DCMAKE_BUILD_TYPE=" + arguments.build_type,
          "-DC1H_GENERATED_C_DIRECTORY=" + str(c_output)])
    run_check(["cmake", "--build", build, "--parallel", "2"])
    run_check(["ctest", "--test-dir", build, "--output-on-failure"])
    run_check([sys.executable, "-B", HERE / "test_host_clock.py"])
    run_check([sys.executable, "-B", HERE / "test_live_transport.py", module_path])
    run_check([sys.executable, "-B", HERE / "test_native_bridge.py"])
    run_check([sys.executable, "-B", HERE / "test_live_evidence.py"])
    run_check([sys.executable, "-B", HERE / "test_campaign_window.py"])
    if arguments.build_only:
        return
    library = load_native(build / "libresearch_c1h.so")
    module = generated.load_generated(module_path)
    trace = []
    report = dict(result="FAIL", scope="host-only output-disabled C1H", output=str(output),
                  build_type=arguments.build_type,
                  nominal_gc_policy="defer cyclic GC for <=2s, cap trace at20000rows, restore prior setting",
                  generator_pins=[generated.PIN_MAVLINK, generated.PIN_PYMAVLINK],
                  generated_python_sha256=hashlib.sha256(module_path.read_bytes()).hexdigest())
    completed_scenarios = []
    gc_events = []

    def observe_gc(phase, info):
        if len(gc_events) < 10000:
            gc_events.append(dict(phase=phase, generation=info["generation"], at_ns=time.monotonic_ns()))

    if arguments.diagnostic_gc:
        gc.callbacks.append(observe_gc)
    try:
        for label, scenario in (("nominal", nominal), ("faults", faults), ("latch", latch), ("stale", stale)):
            experiment = Experiment(library, module, trace, label)
            try:
                with campaign_window() if label == "nominal" else nullcontext():
                    old_frame = scenario(experiment)
                if label == "faults":
                    saved_old_frame = old_frame
                completed_scenarios.append(label)
            finally:
                experiment.close()
        restarted = Experiment(library, module, trace, "restart")
        try:
            restarted.tick(snapshots=True)
            result = restarted.exchange(saved_old_frame, "prior_run_key_context")
            require(result.parser_reason != 0 and not restarted.access().has_eligible_request,
                    "old traffic survived fresh-key/context restart")
            completed_scenarios.append("restart")
        finally:
            restarted.close()
        require(completed_scenarios == ["nominal", "faults", "latch", "stale", "restart"],
                "required functional scenario incomplete")
        report.update(score(trace), functional_result="PASS", completed_scenarios=completed_scenarios)
        report["mutation_checks"] = mutation_check(trace)
        report["result"] = report["timing_result"]
    except Exception as error:
        report["failure"] = str(error)
        raise
    finally:
        if arguments.diagnostic_gc:
            gc.callbacks.remove(observe_gc)
            report["gc_diagnostic"] = dict(policy=report["nominal_gc_policy"], events=gc_events,
                                           complete=len(gc_events) < 10000)
        (output / "trace.json").write_text(json.dumps(trace, indent=2), encoding="utf-8")
        report["trace_sha256"] = hashlib.sha256((output / "trace.json").read_bytes()).hexdigest()
        sources = [path for path in HERE.iterdir() if path.is_file()]
        sources += list((HERE.parent / "supervisor").glob("*.hpp"))
        sources += list((HERE.parent / "supervisor").glob("*.cpp"))
        for extension in ("*.cpp", "*.hpp"):
            sources += list((generated.ROOT / "src/lib/research_acceleration").glob(extension))
        sources += [generated.XML, generated.SCHEMA, generated.MAVGEN,
                    WIRE / "codec.py", WIRE / "test_generated_mavlink.py"]
        report["source_sha256"] = {str(path.relative_to(generated.ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in sorted(sources)}
        (output / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
        print(json.dumps(report, sort_keys=True), flush=True)
    if report["result"] != "PASS":
        raise SystemExit(1)


if __name__ == "__main__":
    main()
