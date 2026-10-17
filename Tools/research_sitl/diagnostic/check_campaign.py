#!/usr/bin/env python3
"""Strict independent oracle for the finite synthetic C1B owner reference profile.

This is not a general flight/evidence validator. The accepted inputs and API
sequence are specified here, independently of the exporter; changing a footer
cannot turn a shortened campaign into this profile. No production predicate,
exporter output, or supplied success flag is used to derive expected behavior.
"""

import argparse
import copy
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import sys

SCHEMA = "research-c1b-offline/v1"
PROFILE = "c1b-owner-reference/v1"
MANIFEST_SCHEMA = "research-c1b-artifacts/v1"
DOMAIN = "SitlDisarmedDiagnostic"
DEFINES = ["RESEARCH_SITL_DIAGNOSTIC_OFFLINE=1", "RESEARCH_SITL_DIAGNOSTIC_HOST_TEST=1"]
UINT64_MAX = (1 << 64) - 1
DECIMAL = re.compile(r"(?:0|[1-9][0-9]*)\Z")
SIGNED_DECIMAL = re.compile(r"(?:0|-?[1-9][0-9]*)\Z")
SHA256 = re.compile(r"[0-9a-f]{64}\Z")
SOURCE_GLOBS = (
    "src/lib/research_acceleration/*.hpp", "src/lib/research_acceleration/*.cpp",
    "Tools/research_sitl/supervisor/*.hpp", "Tools/research_sitl/supervisor/*.cpp",
    "src/lib/matrix/matrix/*.hpp",
)
REQUIRED_SOURCES = (
    "src/lib/research_acceleration/CMakeLists.txt",
    "platforms/ros2/include/px4_platform_common/defines.h",
    "docs/C1B_OFFLINE_API_CONTRACT.md", "docs/C1B_OFFLINE_API_TEST_MATRIX.md",
    *("Tools/research_sitl/diagnostic/" + name for name in (
        "DiagnosticJson.hpp", "DiagnosticJson.cpp", "DiagnosticCampaign.cpp",
        "check_campaign.py", "test_check_campaign.py", "run_campaign.py")),
    *("src/lib/research_acceleration/" + name for name in (
        "FrameMath.hpp", "FrameMath.cpp", "RequestGuard.hpp", "RequestGuard.cpp",
        "TrustedTimeMap.hpp", "TrustedTimeMap.cpp", "WireAdapter.hpp", "WireAdapter.cpp",
        "WireAdapterChecks.hpp", "DiagnosticTypes.hpp", "DiagnosticGuard.cpp", "DiagnosticAdapter.cpp",
        "WireStatus.hpp", "WireStatus.cpp")),
    *("Tools/research_sitl/supervisor/" + name for name in (
        "HarnessSupervisorOwner.hpp", "HarnessSupervisorOwner.cpp", "SupervisorBroker.hpp",
        "SupervisorBroker.cpp", "DiagnosticHarnessSupervisorOwner.cpp", "DiagnosticSupervisorBroker.cpp")),
    "src/lib/matrix/matrix/Matrix.hpp", "src/lib/matrix/matrix/Vector3.hpp",
)


class CampaignError(ValueError):
    """The evidence cannot establish the specified finite profile."""


def fail(message):
    raise CampaignError(message)


def _unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            fail("duplicate JSON key: " + key)
        result[key] = value
    return result


def load_json(data):
    """Parse strict JSON, retaining integer versus floating-point types."""
    try:
        return json.loads(data, object_pairs_hook=_unique_object,
                          parse_constant=lambda value: fail("nonfinite JSON constant: " + value))
    except (UnicodeError, ValueError, RecursionError) as error:
        raise CampaignError("invalid/truncated JSON: " + str(error)) from error


def read_json(path):
    try:
        return load_json(Path(path).read_bytes())
    except OSError as error:
        raise CampaignError("unavailable evidence file: " + str(path)) from error


def _keys(value, expected, path):
    if type(value) is not dict or set(value) != set(expected):
        fail(path + ": missing/unknown object keys")


def _decimal(value, signed=False, path="integer"):
    pattern = SIGNED_DECIMAL if signed else DECIMAL
    if type(value) is not str or not pattern.fullmatch(value):
        fail(path + ": expected canonical decimal string")
    # Reject oversized inputs before Python's integer conversion limit matters.
    if len(value) > 20:
        fail(path + ": integer outside 64-bit range")
    number = int(value)
    minimum, maximum = (-(1 << 63), (1 << 63) - 1) if signed else (0, UINT64_MAX)
    if not minimum <= number <= maximum:
        fail(path + ": integer outside 64-bit range")
    return number


def _compare(actual, expected, path="campaign"):
    """A complete fixed-profile schema and value comparison, including types.

    Expected float fields are C++ float32 quantities. Decimal spellings that
    round to the same float32 are equivalent; integer/identity fields never use
    that rule. All expected decimal strings are actual 64-bit schema fields.
    """
    if type(expected) is dict:
        _keys(actual, expected, path)
        for key, value in expected.items():
            _compare(actual[key], value, path + "." + key)
    elif type(expected) is list:
        if type(actual) is not list or len(actual) != len(expected):
            fail(path + ": missing/extra/truncated sequence")
        for index, value in enumerate(expected):
            _compare(actual[index], value, path + "[" + str(index) + "]")
    elif type(expected) is float:
        if type(actual) not in (int, float):
            fail(path + ": expected finite numeric float field")
        try:
            if not math.isfinite(actual):
                fail(path + ": expected finite numeric float field")
            if struct.pack("!f", actual) != struct.pack("!f", expected):
                fail(path + ": numeric value differs from profile")
        except (OverflowError, struct.error) as error:
            raise CampaignError(path + ": float outside supported range") from error
    else:
        if type(actual) is not type(expected):
            fail(path + ": wrong scalar type")
        if type(expected) is str and SIGNED_DECIMAL.fullmatch(expected):
            _decimal(actual, path.endswith("validator_minus_producer_us"), path)
        if actual != expected:
            fail(path + ": value differs from fixed profile (expected " + repr(expected) + ")")


def _inside(root, relative):
    if type(relative) is not str or not relative or "\\" in relative:
        fail("invalid artifact relative path")
    candidate = Path(relative)
    if candidate.is_absolute() or ":" in relative or any(part in ("", ".", "..") for part in relative.split("/")):
        fail("artifact path must stay below its explicit root")
    root = Path(root).resolve()
    resolved = (root / candidate).resolve()
    try:
        resolved.relative_to(root)
    except ValueError as error:
        raise CampaignError("artifact escapes explicit root") from error
    if not resolved.is_file():
        fail("missing artifact/source: " + relative)
    return resolved


def source_paths(source_root):
    """Return the independently required, sorted project-relative source set."""
    root = Path(source_root).resolve()
    names = set(REQUIRED_SOURCES)
    for pattern in SOURCE_GLOBS:
        group = sorted(root.glob(pattern))
        if not group:
            fail("empty required source group: " + pattern)
        names.update(path.relative_to(root).as_posix() for path in group)
    for name in names:
        _inside(root, name)
    return sorted(names)


def sha256_file(path):
    try:
        digest = hashlib.sha256()
        with Path(path).open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(chunk)
        return digest.hexdigest()
    except OSError as error:
        raise CampaignError("cannot hash artifact: " + str(path)) from error


def _artifact(entry, root, expected_path):
    _keys(entry, ("path", "sha256"), "artifact")
    if entry["path"] != expected_path or type(entry["sha256"]) is not str or not SHA256.fullmatch(entry["sha256"]):
        fail("invalid artifact path/hash")
    path = _inside(root, expected_path)
    if sha256_file(path) != entry["sha256"]:
        fail("artifact hash mismatch: " + expected_path)
    return path


def verify_manifest(manifest, artifact_root, source_root):
    _keys(manifest, ("schema", "profile_id", "sources", "executable", "campaign", "build", "complete"), "manifest")
    if manifest["schema"] != MANIFEST_SCHEMA or manifest["profile_id"] != PROFILE or manifest["complete"] is not True:
        fail("wrong/incomplete artifact manifest")
    expected_paths = source_paths(source_root)
    if type(manifest["sources"]) is not list or len(manifest["sources"]) != len(expected_paths):
        fail("manifest source registry differs from required source set")
    for entry, name in zip(manifest["sources"], expected_paths):
        _artifact(entry, source_root, name)
    _artifact(manifest["executable"], artifact_root, "diagnostic_campaign")
    campaign_path = _artifact(manifest["campaign"], artifact_root, "campaign.json")
    build = manifest["build"]
    _keys(build, ("configuration", "compiler", "defines"), "manifest.build")
    if build["configuration"] not in ("Debug", "Release", "Sanitize"):
        fail("unknown build configuration")
    if type(build["compiler"]) is not str or not build["compiler"].strip():
        fail("missing compiler identity")
    _compare(build["defines"], DEFINES, "manifest.build.defines")
    return campaign_path


def cause(value="None", kind="DiagnosticLocalReason"):
    return {"type": kind, "value": value}


def variant(kind, value):
    return {"kind": kind, "value": value}


def session(owner=7, identifier=100):
    return {"owner": str(owner), "session": str(identifier)}


def clock_map(valid=True):
    return dict(valid=valid, source="7" if valid else "0", session="100" if valid else "0",
                controller_instance_id="11" if valid else "0", map_epoch="1" if valid else "0",
                validator_minus_producer_us="1000" if valid else "0", uncertainty_us="2" if valid else "0",
                valid_from_producer_us="8000" if valid else "0", valid_until_producer_us="10000" if valid else "0")


def physical(now):
    return {"generation": "1", "observer_continuous": True,
            "request_guard_context": {"validator_receive_time_us": str(now), "arm_state": "Disarmed",
                "mode_state": "NotSelected", "clock_mapping_valid": True, "transport_connected": True,
                "required_state_valid": True}, "map": clock_map()}


def authentication():
    return {"mavlink2": True, "parser_authenticated": True,
            "configured_header_channel_source_binding": True, "configured_target_binding": True}


def ingress():
    return {"owner_lifetime_id": "13", "fixture_run_id": "17", "authentication": authentication()}


def empty_admission():
    return {"acknowledgement_current": False, **{key: "0" for key in (
        "acknowledgement_operation_sequence", "acknowledged_status_sequence", "acknowledgement_raw_producer_time_us",
        "mapped_acknowledgement_time_us", "earliest_acknowledgement_time_us", "latest_acknowledgement_time_us",
        "source", "session", "controller_instance_id", "transition_epoch", "map_epoch")}, "map": clock_map(False)}


def common(code="Accepted", rejections=0, latched=False, syntax=False, age=None):
    return {"code": code, "syntactically_valid": syntax, "eligible_for_control": False, "latched": latched,
            "first_latched_code": "ProducerTimeRegressed" if latched else "Accepted",
            "conservative_age_valid": age is not None, "conservative_age_us": str(age if age is not None else 0),
            "rejection_count": str(rejections)}


def console(code, ticket=0, diagnostic=None, consumed=False, action=False):
    return variant("Console", {"disposition": "Submitted" if code == "Submitted" else "NotSubmitted",
        "broker_code": code, "diagnostic_code": diagnostic, "ticket": str(ticket),
        "event_consumed": consumed, "action_performed": action})


def _configuration():
    return {"owner": {"adapter_contract": {"protocol_version": 65536, "permitted_source": "7",
        "disarmed_policy": "ValidateOnly", "acceleration_bounds": {"minimum_m_s2": [-10.0] * 3,
        "maximum_m_s2": [10.0] * 3, "maximum_norm_m_s2": 10.0}, "timing": {
        "minimum_validity_us": "20", "maximum_validity_us": "200", "maximum_command_age_us": "50",
        "maximum_future_skew_us": "10", "maximum_clock_uncertainty_us": "2"}, "quaternion_norm_tolerance": 0.001},
        "adapter_identity": {"controller_instance_id": "11"}, "fixture_run_id": "17", "maximum_receipt_age_us": "50"},
        "broker": {"maximum_confirmation_age_us": "50"}}


class Reference:
    """Small independent state ledger for exactly the externally specified calls."""
    def __init__(self):
        self.now, self.observation, self.report, self.transition, self.selector = 9950, 0, 0, 0, 0
        self.rejections, self.event, self.wire_floor = 0, 0, 0
        self.initialized, self.attached, self.latched, self.history = False, True, False, False
        self.selection, self.reason, self.gate = "Disabled", cause(), "NoTrustedSession"
        self.barrier, self.barrier_time = 0, 0
        self.admission = empty_admission()
        self.calls = []

    def snapshot(self):
        return {"domain": DOMAIN, "identity": {"owner_lifetime_id": "13", "fixture_run_id": "17"},
            "controller_instance_id": "11", "configured": True, "initialized": self.initialized, "terminal": False,
            "selection": self.selection, "selector_epoch": str(self.selector), "selection_reason": copy.deepcopy(self.reason),
            "operator_attached": self.attached, "operator_instance": "19", "adapter": {
                "initialized": self.initialized, "terminal": False, "state_generation": "1" if self.initialized else "0",
                "transition_epoch": str(self.transition), "barrier_report_sequence": str(self.barrier),
                "barrier_validator_time_us": str(self.barrier_time), "last_report_sequence": str(self.report)},
            "session": {"gate": self.gate, "has_last_successful_session": self.history,
                "last_successful_session": session() if self.history else session(0, 0),
                "has_wire_operation_floor": self.wire_floor != 0, "wire_operation_floor": str(self.wire_floor),
                "has_supervisor_floor": self.event != 0, "supervisor_floor": str(self.event)},
            "admission": copy.deepcopy(self.admission), "latched": self.latched,
            "first_latched_code": "ProducerTimeRegressed" if self.latched else "Accepted", "rejection_count": str(self.rejections),
            "has_last_accepted_sequence": False, "last_accepted_sequence": "0", "last_accepted_authenticated": False}

    def binding(self):
        return {"owner_lifetime_id": "13", "fixture_run_id": "17", "controller_instance_id": "11",
            "operator_instance": "19", "state_generation": "1", "transition_epoch": str(self.transition),
            "selector_epoch": str(self.selector), "prospective_map": clock_map()}

    def view(self):
        return {"binding": self.binding(), "validator_time_us": str(self.now), "state": self.snapshot(),
            "physical": physical(self.now), "can_enable": self.selection == "Disabled" and not self.latched,
            "can_disable": True, "can_admin": True}

    def correlation(self, operation, event=0, packet=None, packet_kind="None", close=None):
        incoming = {"packet_kind": "None", **{key: "0" for key in ("source", "session", "controller_instance_id",
            "transition_epoch", "map_epoch", "packet_sequence", "acknowledged_status_sequence")}}
        admin = {"has_packet": False, "packet_source": "0", "packet_session": "0", "packet_operation_sequence": "0",
            "has_supervisor_event": event != 0, "supervisor_event_id": str(event), "supervisor_event_consumed": event != 0}
        if packet and operation == "Establish":
            admin.update(has_packet=True, packet_source=packet["source"], packet_session=packet["session"],
                         packet_operation_sequence=packet["operation_sequence"])
        elif packet:
            wire = packet.get("wire", packet)
            incoming.update(packet_kind=packet_kind, source=wire.get("source_id", wire.get("source")),
                session=wire.get("session_id", wire.get("session")), controller_instance_id=wire["controller_instance_id"],
                transition_epoch=wire["transition_epoch"], map_epoch=wire["clock_mapping_epoch"],
                packet_sequence=wire.get("sequence", wire.get("operation_sequence")),
                acknowledged_status_sequence=wire.get("acknowledged_status_sequence", "0"))
        return {"operation": operation, "ingress": incoming, "admin": admin, "operator_instance": "19",
                "close_reason": close, "authentication": authentication() if packet else None}

    def record(self, kind, code, primary, correlation, decision=None):
        if kind != "Operation":
            self.transition += 1
            self.barrier = self.report + 1
            self.barrier_time = self.now
        self.report += 1
        return {"domain": DOMAIN, "identity": {"owner_lifetime_id": "13", "fixture_run_id": "17"},
            "report_sequence": str(self.report), "transition_epoch": str(self.transition), "selector_epoch": str(self.selector),
            "kind": kind, "code": code, "cause": copy.deepcopy(primary), "correlation": copy.deepcopy(correlation),
            "state_reference_valid": True, "physical": physical(self.now), "snapshot": self.snapshot(),
            "common_decision": decision, "eligible_for_control": False}

    def result(self, code, records, consumed=False, action=False):
        indexes = [index for index, record in enumerate(records) if record["kind"] == "Operation"]
        assert len(indexes) == 1  # Oracle construction invariant, not evidence acceptance.
        return {"code": code, "snapshot": self.snapshot(), "record_count": len(records), "records": records,
            "has_operation_record": True, "operation_record_index": indexes[0], "grant_consumed": consumed,
            "action_performed": action}

    def call(self, name, inputs, outcome, result=None, submission=None, derived=None, owner_outcome=None):
        self.calls.append({"call_id": str(len(self.calls) + 1), "name": name, "observation_id": str(self.observation),
            "input": copy.deepcopy(inputs), "outcome": copy.deepcopy(outcome), "adapter_result": copy.deepcopy(result),
            "owner_submission": copy.deepcopy(submission), "owner_derived_turn": copy.deepcopy(derived), "owner_outcome": owner_outcome})

    def tick(self, now, first=False):
        self.now, self.observation = now, self.observation + 1
        correlation = self.correlation("Poll")
        records = []
        if first:
            self.initialized, self.selector = True, 1
            records.append(self.record("PhysicalBarrier", "Observed", cause(), correlation))
        if self.selection == "Disabled":
            self.rejections += 1
            primary, decision = cause("SelectionRequired"), None
        else:
            primary, decision = cause("NoEligibleRequest", "DecisionCode"), common("NoEligibleRequest", self.rejections)
        records.append(self.record("Operation", "NoEligibleRequest", primary, correlation, decision))
        state_receipt = {"sample": physical(now), "clock": {"fixture_run_id": "17", "observation_id": str(self.observation),
            "validator_time_us": str(now), "continuous": True}, "fixture_state_stream_id": "23"}
        self.call("tickDiagnostic", {"receipt": state_receipt}, variant("Bool", {"returned": True}), self.result("NoEligibleRequest", records))

    def session_packet(self, operation, sequence, raw=0):
        return {"controller_instance_id": "11", "transition_epoch": str(self.transition), "clock_mapping_epoch": "1",
            "source": "7", "session": "100", "operation_sequence": str(sequence),
            "producer_acknowledgement_monotonic_us": str(raw), "acknowledged_status_sequence": str(self.barrier if operation == 3 else 0),
            "protocol_version": 65536, "target_system": 1, "target_component": 1, "operation": operation,
            "flags": 0, "reserved": [0, 0, 0, 0]}

    def request_packet(self, sequence, raw):
        return {"wire": {"source_id": "7", "session_id": "100", "sequence": str(sequence),
            "producer_monotonic_us": str(raw), "producer_expiry_monotonic_us": str(raw + 100), "controller_instance_id": "11",
            "transition_epoch": str(self.transition), "clock_mapping_epoch": "1", "acceleration_x_frd_m_s2": 1.0,
            "acceleration_y_frd_m_s2": 0.0, "acceleration_z_frd_m_s2": 0.0, "protocol_version": 65536,
            "frame": 1, "quantity": 1, "reference_point": 1}, "target_system": 1, "target_component": 1,
            "flags": 0, "reserved": [0, 0, 0, 0]}

    def derived(self, kind, value):
        return {"provenance": "owner_derived", "operation": variant(kind, value)}

    def selection_action(self, enable):
        action = "Enable" if enable else "Disable"
        event = self.event + 1
        submission = {"expected_view": self.view(), "command": variant("DiagnosticSelectionCommand", {
            "action": action, "prospective": session() if enable else session(0, 0), "event_id": str(event)})}
        derived = self.derived("DiagEnable" if enable else "DiagDisable", {"grant": {
            "event_id": str(event), "action": action, "expected": self.binding()}})
        self.rejections += 1  # Both these calls enter with healthy Disabled selector.
        self.event = event
        correlation, records = self.correlation(action, event), []
        if enable:
            self.selection, self.gate, self.selector = "SelectedForValidation", "EstablishmentRequired", self.selector + 1
            self.reason = cause()
            records.append(self.record("SelectorBarrier", "SelectionEnabled", cause(), correlation))
        code = "SelectionEnabled" if enable else "AlreadyDisabled"
        records.append(self.record("Operation", code, cause(), correlation))
        self.call("confirmDiagnosticSelection" if enable else "disableDiagnosticSelection", {"ticket": "1"} if enable else {},
            console("Submitted", 1 if enable else 0, code, True, enable), self.result(code, records, True, enable),
            submission, derived, "Completed")

    def administer(self, clear=False):
        intent, event, ticket = ("ClearLatch", 3, 3) if clear else ("Establish", 2, 2)
        submission = {"expected_view": self.view(), "command": variant("DiagnosticAdministrationCommand", {
            "intent": intent, "session": session(0, 0) if clear else session(), "event_id": str(event)})}
        grant = {"expected": self.binding(), "event": {"event_id": str(event), "expected_state_generation": "1",
            "expected_transition_epoch": str(self.transition), "expected_controller_instance_id": "11",
            "action": "ClearLatchedFault" if clear else "Establish", "has_session_binding": True, "session": session()}}
        packet = None if clear else self.session_packet(1, 1)
        derived = self.derived("DiagClear", {"grant": grant}) if clear else self.derived("DiagEstablish", {
            "packet": packet, "ingress": ingress(), "grant": grant})
        if clear:
            self.rejections += 1  # Prefix poll observes the existing producer latch.
            self.latched, self.gate = False, "EstablishmentRequired"
        else:
            self.history, self.wire_floor, self.gate = True, 1, "Established"
        self.event, self.admission = event, empty_admission()
        correlation = self.correlation("Clear" if clear else "Establish", event, packet)
        code = "LatchCleared" if clear else "SessionEstablished"
        records = [self.record("SessionBarrier", "Observed", cause(), correlation)]
        records.append(self.record("Operation", code, cause("Accepted", "DecisionCode"), correlation, common("Accepted", self.rejections)))
        self.call("confirmDiagnosticAdmin", {"ticket": str(ticket)}, console("Submitted", ticket, code, True, True),
            self.result(code, records, True, True), submission, derived, "Completed")

    def acknowledge(self):
        raw, offset, uncertainty = 8960, 1000, 2
        earliest, latest = raw + offset - uncertainty, raw + offset + uncertainty
        if not self.barrier_time < earliest <= self.now or latest > self.now:
            raise RuntimeError("reference ACK interval is internally inconsistent")
        packet = self.session_packet(3, 2, raw)
        self.wire_floor = 2
        self.admission = {"acknowledgement_current": True, "acknowledgement_operation_sequence": "2",
            "acknowledged_status_sequence": str(self.barrier), "acknowledgement_raw_producer_time_us": str(raw),
            "mapped_acknowledgement_time_us": str(raw + offset), "earliest_acknowledgement_time_us": str(earliest),
            "latest_acknowledgement_time_us": str(latest), "source": "7", "session": "100", "controller_instance_id": "11",
            "transition_epoch": str(self.transition), "map_epoch": "1", "map": clock_map()}
        records = [self.record("Operation", "Acknowledged", cause("Completed", "IngressCode"),
                               self.correlation("Acknowledge", packet=packet, packet_kind="Acknowledgement"))]
        self.call("processDiagnosticAcknowledgement", {"packet": packet, "ingress": ingress()}, variant("Bool", {"returned": True}),
                  self.result("Acknowledged", records, action=True))

    def request(self, sequence, raw, duplicate=False, regression=False):
        packet = self.request_packet(sequence, raw)
        mapped, earliest, expiry = raw + 1000, raw + 1000 - 2, raw + 1000 + 100 - 2
        age = self.now - earliest
        if not (raw > 8960 and earliest > 8960 + 1000 + 2 and 20 <= 100 <= 200 and 0 <= age <= 50
                and mapped + 2 <= self.now + 10 and expiry > self.now):
            raise RuntimeError("reference request arithmetic is inconsistent")
        if duplicate:
            self.rejections += 1
            code, primary, decision = "Rejected", "DuplicateSequence", common("DuplicateSequence", self.rejections)
        elif regression:
            if not raw < 9000:
                raise RuntimeError("reference producer regression is missing")
            self.rejections, self.latched = self.rejections + 1, True
            code, primary = "Rejected", "ProducerTimeRegressed"
            decision = common(primary, self.rejections, latched=True)  # Actual latch representation has no age evidence.
        else:
            code, primary = "RequestValidatedDisarmed", "ValidatedDisarmed"
            decision = common(primary, self.rejections, syntax=True, age=age)
        correlation = self.correlation("Request", packet=packet, packet_kind="Request")
        records = [self.record("Operation", code, cause(primary, "DecisionCode"), correlation, decision)]
        if regression:
            self.selection, self.selector = "Disabled", self.selector + 1
            self.reason, self.gate, self.admission = cause(primary, "DecisionCode"), "EstablishmentRequired", empty_admission()
            records.append(self.record("SelectorBarrier", "SelectionDisabled", self.reason, correlation))
        self.call("processDiagnosticRequest", {"packet": packet, "ingress": ingress()}, variant("Bool", {"returned": True}),
                  self.result(code, records, action=True))


def _flight_sentinel():
    request = {"protocol_version": 65536, "source": "7", "session": "100", "sequence": "777",
        "producer_monotonic_us": "888", "producer_time_in_validator_domain_us": "999", "producer_expiry_monotonic_us": "1111",
        "expiry_time_in_validator_domain_us": "1222", "clock_mapping_uncertainty_us": "2", "frame": "Frd",
        "quantity": "InertialAcceleration", "reference_point": "Cg", "acceleration_frd_m_s2": [3.0, 4.0, 5.0]}
    evidence = {key: False for key in ("captured", "state_reference_valid", "transport_connected", "clock_mapping_valid",
        "packet_mavlink2", "packet_parser_authenticated", "packet_header_source_bound", "packet_target_bound", "acknowledgement_current",
        "has_last_accepted_sequence", "last_accepted_authenticated", "latched", "syntactically_valid", "conservative_age_valid")}
    evidence.update({key: "0" for key in ("controller_instance_id", "clock_mapping_epoch", "trusted_source", "trusted_session",
        "last_accepted_sequence", "rejection_count", "conservative_age_us")})
    evidence.update(arm_state="Disarmed", mode_state="NotSelected", session_gate="NoTrustedSession", first_latched_code="Accepted",
                    decision_origin="None", local_wire_decision_code=0)
    report = {"report_sequence": "0", "transition_epoch": "0", "validator_time_us": "0", "kind": "Observation", "code": "Observed",
        "has_s1_decision": False, "s1_decision": common(), "evidence": evidence}
    adapter = {"code": "EligibleRequest", "admission_closed": False, "snapshot": {"initialized": True, "terminal": False,
        "state_generation": "701", "transition_epoch": "702", "barrier_report_sequence": "703", "barrier_validator_time_us": "704",
        "last_report_sequence": "705"}, "report_count": 0, "reports": [copy.deepcopy(report), copy.deepcopy(report)]}
    return variant("FlightAccess", {"returned": False, "request_before": copy.deepcopy(request), "request_after": request,
        "adapter_before": copy.deepcopy(adapter), "adapter_after": adapter, "request_bytes_unchanged": True, "adapter_bytes_unchanged": True})


def reference_campaign(artifacts):
    """Construct the independently specified reference, never from emitted JSON."""
    model = Reference()
    model.call("attachFixtureOperator", {"operator_instance": "19"}, variant("Bool", {"returned": True}))
    model.tick(9950, first=True)
    model.call("prepareDiagnosticSelection", {"prospective": session()}, console("Ready", 1))
    model.selection_action(True)
    model.call("offer", {"receipt": {"fixture_run_id": "17", "receipt_id": "1", "received_validator_time_us": "9950",
        "packet": model.session_packet(1, 1), "verdict": authentication()}}, variant("Bool", {"returned": True}))
    model.call("prepareDiagnosticAdmin", {"intent": "Establish", "session": session()}, console("Ready", 2))
    model.administer()
    model.tick(9965)
    model.acknowledge()
    model.tick(10010)
    model.request(1, 9000)
    records = [model.record("Operation", "NoEligibleRequest", cause("NoEligibleRequest", "DecisionCode"),
        model.correlation("Access"), common("NoEligibleRequest", model.rejections))]
    model.call("accessDiagnostic", {}, variant("Bool", {"returned": True}), model.result("NoEligibleRequest", records))
    model.request(1, 9000, duplicate=True)
    model.tick(10011)
    model.request(2, 8999, regression=True)
    model.call("prepareDiagnosticAdmin", {"intent": "ClearLatch", "session": session(0, 0)}, console("Ready", 3))
    model.administer(clear=True)
    model.selection_action(False)
    model.rejections += 1  # Disabled prefix poll on lifecycle close.
    model.attached = False
    correlation = model.correlation("LifecycleClose", close="OperatorDetach")
    records = [model.record("Operation", "AlreadyDisabled", cause(), correlation)]
    model.call("detachDiagnosticOperator", {}, console("Submitted", diagnostic="AlreadyDisabled", action=True),
        model.result("AlreadyDisabled", records, action=True), derived=model.derived("LifecycleClose", {"reason": "OperatorDetach"}),
        owner_outcome="Completed")
    model.call("getEligibleRequest", {}, _flight_sentinel())
    return {"schema": SCHEMA, "header": {"profile_id": PROFILE, "compile_domain": DOMAIN, "provenance": "synthetic_offline",
        "identity": {"owner_lifetime_id": "13", "fixture_run_id": "17", "controller_instance_id": "11"},
        "configuration": _configuration(), "artifacts": copy.deepcopy(artifacts)}, "trace": {"calls": model.calls,
        "end": {"call_count": 20, "report_count": model.report, "last_report_sequence": str(model.report), "final_snapshot": model.snapshot()}}}


def check_campaign(campaign, manifest, artifact_root, source_root):
    """Verify actual artifact bytes and every field of the finite evidence trace."""
    campaign_path = verify_manifest(manifest, artifact_root, source_root)
    on_disk = read_json(campaign_path)
    _compare(campaign, on_disk, "supplied_campaign_vs_hashed_file")
    artifacts = {key: copy.deepcopy(manifest[key]) for key in ("sources", "executable", "build")}
    expected = reference_campaign(artifacts)
    _compare(campaign, expected)
    return {"profile_id": PROFILE, "call_count": 20, "report_count": 19, "validated_request_count": 1,
        "provenance": "synthetic_offline", "scope": "finite_owner_reference_only"}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("campaign", type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--source-root", required=True, type=Path)
    parser.add_argument("--artifact-root", type=Path)
    parser.add_argument("--profile", default=PROFILE, choices=[PROFILE])
    args = parser.parse_args(argv)
    try:
        summary = check_campaign(read_json(args.campaign), read_json(args.manifest),
            args.artifact_root or args.campaign.parent, args.source_root)
    except (CampaignError, OSError) as error:
        print("FAIL: " + str(error), file=sys.stderr)
        return 1
    print(json.dumps(summary, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
