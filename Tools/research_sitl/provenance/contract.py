"""Offline logged-observation contract. No live admission or clock authority."""

import hashlib
import json
import math
import re


SCHEMA = "closed-provenance-archive/v1"
# Resource ceilings for this offline tool, not vehicle timing/health limits.
MAX_INPUT_BYTES = 128 * 1024 * 1024
MAX_RECEIPTS = 200_000
MAX_JSON_BYTES = 4 * 1024 * 1024
MAX_LINE_BYTES = 32 * 1024
TOPICS = (
    "vehicle_attitude", "estimator_attitude", "vehicle_local_position",
    "estimator_local_position", "estimator_status_flags", "estimator_selector_status",
)
ATTITUDE = ("timestamp", "timestamp_sample", "q[0]", "q[1]", "q[2]", "q[3]",
            "quat_reset_counter")
POSITION = ("timestamp", "timestamp_sample", "x", "y", "z", "vx", "vy", "vz",
            "xy_valid", "z_valid", "v_xy_valid", "v_z_valid", "xy_reset_counter",
            "z_reset_counter", "vxy_reset_counter", "vz_reset_counter",
            "heading_reset_counter")
SELECTOR = ("timestamp", "primary_instance", "instances_available", "instance_changed_count",
            "last_instance_change", "accel_device_id", "baro_device_id", "gyro_device_id",
            "mag_device_id", "gyro_fault_detected", "accel_fault_detected")
FLAGS = ("timestamp", "timestamp_sample", "control_status_changes", "fault_status_changes",
         "cs_tilt_align", "cs_yaw_align")
LIMITATIONS = (
    "offline_ulog_observations_not_native_owner_receipts",
    "per_stream_logged_order_only_no_global_owner_order",
    "logger_sampling_cannot_prove_publisher_or_internal_state_continuity",
    "publisher_lifetime_and_selected_instance_binding_unqualified",
    "producer_to_validator_clock_map_unavailable",
    "operator_capability_and_transport_liveness_unavailable",
    "no_health_freshness_or_control_acceptance",
)
CAPABILITIES = {"can_select_diagnostics": False, "can_establish_session": False,
                "can_install_clock_map": False, "can_publish_control": False}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(path):
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def checked_source(path, expected):
    require(re.fullmatch(r"[0-9a-f]{64}", expected) is not None, "invalid expected SHA-256")
    require(path.is_file() and 0 < path.stat().st_size <= MAX_INPUT_BYTES, "ULog size outside bound")
    require(digest(path) == expected, "ULog SHA-256 mismatch")


def unique_pairs(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, "duplicate JSON key: " + key)
        result[key] = value
    return result


def reject_constant(value):
    raise ValueError("nonfinite JSON constant: " + value)


def finite_number(value):
    result = float(value)
    require(math.isfinite(result), "nonfinite JSON number")
    return result


def loads(text):
    return json.loads(text, object_pairs_hook=unique_pairs, parse_constant=reject_constant,
                      parse_float=finite_number)


def load(path):
    require(path.stat().st_size <= MAX_JSON_BYTES, "JSON exceeds bound")
    return loads(path.read_text(encoding="utf-8"))


def encode(value):
    return json.dumps(value, allow_nan=False, sort_keys=True, separators=(",", ":"))


def fields_for(topic, available):
    if topic.endswith("attitude"):
        required = ATTITUDE
    elif topic.endswith("local_position"):
        required = POSITION
    elif topic == "estimator_selector_status":
        required = SELECTOR
    else:
        required = FLAGS
    # Keep every published health/fault flag, without inventing a health mask.
    extra = {key for key in available if topic == "estimator_status_flags"
             and key.startswith(("cs_", "fs_"))}
    return sorted(set(required) & set(available) | extra), sorted(set(required) - set(available))


def scalar(value):
    if hasattr(value, "item"):
        value = value.item()
    require(type(value) in (int, float, bool), "unsupported logged scalar")
    if type(value) is float and not math.isfinite(value):
        return {"nonfinite": "nan" if math.isnan(value) else ("+inf" if value > 0 else "-inf")}
    return value
