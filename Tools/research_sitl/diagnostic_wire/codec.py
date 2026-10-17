"""Strict standard-library payload codec for the isolated C1E diagnostic wire."""

from __future__ import annotations

import math
import struct


REQUEST_FORMAT = "<10Q3fI8B4s"
STATUS_FORMAT = "<14QIH14B4s"
REQUEST_SIZE = struct.calcsize(REQUEST_FORMAT)
STATUS_SIZE = struct.calcsize(STATUS_FORMAT)
REQUEST_MIN_SIZE = 103
STATUS_MIN_SIZE = 132

PROTOCOL_VERSION = 0x10000
DOMAIN_DIAGNOSTIC = 1
PROFILE_NATIVE_DIAGNOSTIC = 1
EXECUTION_SYNTHETIC_NATIVE_CONTRACT = 1
EXECUTION_NATIVE_POSIX_SITL = 2
OUTCOME_REJECTED = 1
OUTCOME_CLOSED = 2

REASONS = {
    "ClockMappingInvalid": 1,
    "TransportDisconnected": 2,
    "StateEstimateInvalid": 3,
    "SelectionRequired": 4,
    "ContextChanged": 5,
    "SourceContinuityLost": 6,
    "OwnerStopped": 7,
    "ResourceExhausted": 8,
}

_REQUEST_FIELDS = (
    "native_run_id",
    "owner_lifetime_id",
    "controller_instance_id",
    "source_id",
    "transport_session_id",
    "request_sequence",
    "expected_transition_epoch",
    "producer_monotonic_us",
    "producer_expiry_monotonic_us",
    "clock_mapping_epoch",
    "acceleration_x_frd_m_s2",
    "acceleration_y_frd_m_s2",
    "acceleration_z_frd_m_s2",
    "protocol_version",
    "domain",
    "profile",
    "target_system",
    "target_component",
    "frame",
    "quantity",
    "reference_point",
    "flags",
    "reserved",
)

_STATUS_FIELDS = (
    "native_run_id",
    "owner_lifetime_id",
    "controller_instance_id",
    "source_id",
    "transport_session_id",
    "request_sequence",
    "status_sequence",
    "transition_epoch",
    "source_sequence",
    "state_generation",
    "arrival_native_us",
    "evaluation_native_us",
    "source_checked_native_us",
    "clock_mapping_epoch",
    "protocol_version",
    "reason",
    "domain",
    "profile",
    "execution_origin",
    "outcome",
    "arm_state",
    "raw_nav_state",
    "research_mode_state",
    "selector_state",
    "estimator_valid",
    "clock_map_valid",
    "eligible_for_control",
    "flags",
    "target_system",
    "target_component",
    "reserved",
)


class CodecError(ValueError):
    """A payload is malformed, out of contract, or not canonically encoded."""


def _exact_fields(value: object, fields: tuple[str, ...], label: str) -> dict:
    if type(value) is not dict or set(value) != set(fields):
        raise CodecError(f"{label} must be a dict with exactly the defined fields")
    return value


def _integer(value: object, label: str, low: int, high: int) -> int:
    if type(value) is not int or not low <= value <= high:
        raise CodecError(f"{label} must be an integer in [{low}, {high}]")
    return value


def _uint64(value: object, label: str, *, zero: bool = False) -> int:
    return _integer(value, label, 0 if zero else 1, 0xFFFFFFFFFFFFFFFF)


def _float32(value: object, label: str) -> float:
    if type(value) not in (int, float):
        raise CodecError(f"{label} must be a real number, not bool or another type")
    try:
        converted = float(value)
        if not math.isfinite(converted):
            raise CodecError(f"{label} must be finite")
        packed = struct.pack("<f", converted)
        rounded = struct.unpack("<f", packed)[0]
    except (OverflowError, struct.error) as error:
        raise CodecError(f"{label} is outside the finite binary32 range") from error
    if not math.isfinite(rounded):
        raise CodecError(f"{label} is outside the finite binary32 range")
    return rounded


def _fixed(value: object, label: str) -> bytes:
    if type(value) is not bytes or value != b"\0\0\0\0":
        raise CodecError(f"{label} must be exactly four zero bytes")
    return value


def _trim(payload: bytes, minimum: int, enabled: bool) -> bytes:
    if type(enabled) is not bool:
        raise CodecError("trim must be bool")
    if not enabled:
        return payload
    return payload[:max(minimum, len(payload.rstrip(b"\0")))]


def _payload(data: object, minimum: int, maximum: int, label: str) -> bytes:
    if type(data) is not bytes:
        raise CodecError(f"{label} payload must be bytes")
    if not minimum <= len(data) <= maximum:
        raise CodecError(f"{label} payload length must be in [{minimum}, {maximum}]")
    return data + bytes(maximum - len(data))


def encode_request(value: dict, *, trim: bool = False) -> bytes:
    """Encode an exact request dictionary; optionally omit its zero tail."""
    value = _exact_fields(value, _REQUEST_FIELDS, "request")
    q = [_uint64(value[name], name, zero=(name == "clock_mapping_epoch"))
         for name in _REQUEST_FIELDS[:10]]
    if q[9] != 0:
        raise CodecError("clock_mapping_epoch must be zero when no qualified map exists")
    if q[8] <= q[7]:
        raise CodecError("producer_expiry_monotonic_us must exceed producer_monotonic_us")
    floats = [_float32(value[name], name) for name in _REQUEST_FIELDS[10:13]]
    protocol = _integer(value["protocol_version"], "protocol_version", 0, 0xFFFFFFFF)
    if protocol != PROTOCOL_VERSION:
        raise CodecError("unsupported protocol_version")
    byte_names = _REQUEST_FIELDS[14:22]
    byte_values = [_integer(value[name], name, 0, 255) for name in byte_names]
    (domain, profile, target_system, target_component, frame, quantity,
     reference_point, flags) = byte_values
    if domain != DOMAIN_DIAGNOSTIC or profile != PROFILE_NATIVE_DIAGNOSTIC:
        raise CodecError("wrong diagnostic domain or profile")
    if target_system == 0 or target_component == 0:
        raise CodecError("target_system and target_component must be nonzero")
    if (frame, quantity, reference_point, flags) != (1, 1, 1, 0):
        raise CodecError("unsupported frame, quantity, reference_point, or flags")
    reserved = _fixed(value["reserved"], "reserved")
    try:
        encoded = struct.pack(REQUEST_FORMAT, *q, *floats, protocol, *byte_values, reserved)
    except struct.error as error:
        raise CodecError("request field cannot be represented by the wire format") from error
    return _trim(encoded, REQUEST_MIN_SIZE, trim)


def decode_request(data: bytes) -> dict:
    """Decode a full or zero-tail-trimmed request into a complete dictionary."""
    padded = _payload(data, REQUEST_MIN_SIZE, REQUEST_SIZE, "request")
    try:
        values = struct.unpack(REQUEST_FORMAT, padded)
    except struct.error as error:
        raise CodecError("invalid request payload") from error
    result = dict(zip(_REQUEST_FIELDS, values))
    # Reuse the encoder's validation without exposing an intermediate object.
    encode_request(result)
    return result


def encode_status(value: dict, *, trim: bool = False) -> bytes:
    """Encode an exact negative-only status dictionary."""
    value = _exact_fields(value, _STATUS_FIELDS, "status")
    q_names = _STATUS_FIELDS[:14]
    q = [_uint64(value[name], name, zero=(name == "clock_mapping_epoch")) for name in q_names]
    if q[13] != 0:
        raise CodecError("clock_mapping_epoch must be zero when no qualified map exists")
    if q[12] > q[11]:
        raise CodecError("source_checked_native_us must not exceed evaluation_native_us")
    if q[10] > q[11]:
        raise CodecError("arrival_native_us must not exceed evaluation_native_us")
    if q[8] < q[9]:
        raise CodecError("source_sequence must be at least state_generation")
    protocol = _integer(value["protocol_version"], "protocol_version", 0, 0xFFFFFFFF)
    if protocol != PROTOCOL_VERSION:
        raise CodecError("unsupported protocol_version")
    reason = _integer(value["reason"], "reason", 1, 8)
    execution_origin = _integer(value["execution_origin"], "execution_origin", 1, 2)
    outcome = _integer(value["outcome"], "outcome", 1, 2)
    if outcome == OUTCOME_REJECTED and reason not in range(1, 6):
        raise CodecError("Rejected outcome requires a rejection reason")
    if outcome == OUTCOME_CLOSED and reason not in range(6, 9):
        raise CodecError("Closed outcome requires a closure reason")
    byte_names = _STATUS_FIELDS[16:30]
    byte_values = [_integer(value[name], name, 0, 255) for name in byte_names]
    (domain, profile, origin, encoded_outcome, arm_state, raw_nav_state,
     research_mode_state, selector_state, estimator_valid, clock_map_valid,
     eligible, flags, target_system, target_component) = byte_values
    if origin != execution_origin or encoded_outcome != outcome:
        raise CodecError("status enum fields are inconsistent")
    if domain != DOMAIN_DIAGNOSTIC or profile != PROFILE_NATIVE_DIAGNOSTIC:
        raise CodecError("wrong diagnostic domain or profile")
    if arm_state not in (1, 2):
        raise CodecError("arm_state must be Disarmed(1) or Armed(2)")
    if raw_nav_state > 30 or raw_nav_state in (9, 11, 16):
        raise CodecError("raw_nav_state is outside the pinned non-reserved set")
    if (research_mode_state, selector_state, estimator_valid, clock_map_valid,
            eligible, flags) != (0, 0, 0, 0, 0, 0):
        raise CodecError("status must remain unselected, unqualified, and ineligible")
    if target_system == 0 or target_component == 0:
        raise CodecError("target_system and target_component must be nonzero")
    reserved = _fixed(value["reserved"], "reserved")
    try:
        encoded = struct.pack(STATUS_FORMAT, *q, protocol, reason, *byte_values, reserved)
    except struct.error as error:
        raise CodecError("status field cannot be represented by the wire format") from error
    return _trim(encoded, STATUS_MIN_SIZE, trim)


def decode_status(data: bytes) -> dict:
    """Decode a full or zero-tail-trimmed status into a complete dictionary."""
    padded = _payload(data, STATUS_MIN_SIZE, STATUS_SIZE, "status")
    try:
        values = struct.unpack(STATUS_FORMAT, padded)
    except struct.error as error:
        raise CodecError("invalid status payload") from error
    result = dict(zip(_STATUS_FIELDS, values))
    encode_status(result)
    return result


__all__ = [
    "CodecError",
    "DOMAIN_DIAGNOSTIC",
    "EXECUTION_NATIVE_POSIX_SITL",
    "EXECUTION_SYNTHETIC_NATIVE_CONTRACT",
    "OUTCOME_CLOSED",
    "OUTCOME_REJECTED",
    "PROFILE_NATIVE_DIAGNOSTIC",
    "PROTOCOL_VERSION",
    "REASONS",
    "REQUEST_FORMAT",
    "REQUEST_MIN_SIZE",
    "REQUEST_SIZE",
    "STATUS_FORMAT",
    "STATUS_MIN_SIZE",
    "STATUS_SIZE",
    "decode_request",
    "decode_status",
    "encode_request",
    "encode_status",
]
