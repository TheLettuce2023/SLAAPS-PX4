#!/usr/bin/env python3
"""Score a declared raw-ULog interval against the unchanged approved registry.

This module scores signals only. acceptance.py owns provenance and gate results.
"""

from pathlib import Path

import numpy as np
from pyulog import ULog

from inspect_raw_ulog import digest, load_json, require, stamps


REGISTRY_SHA256 = "d295bcb9a43fae1dec4eb147539a9ea1f980f087a84337f37d749db39239e52d"
REGISTRY_PATH = Path(__file__).with_name("diagnostic_limits_v2.json")
# Constants are from msg/versioned/VehicleStatus.msg and SensorGps fix_type.
STATE_RULES = {
    "vehicle_status": {"arming_state": 1, "pre_flight_checks_pass": 1},
    "vehicle_local_position": {"xy_valid": 1, "z_valid": 1,
                               "v_xy_valid": 1, "v_z_valid": 1},
    "vehicle_global_position": {"lat_lon_valid": 1, "alt_valid": 1},
    "estimator_status_flags": {"cs_tilt_align": 1, "cs_yaw_align": 1,
                               "cs_gnss_pos": 1},
    "airspeed_validated": {"airspeed_source": 1},
}


def registry():
    require(digest(REGISTRY_PATH) == REGISTRY_SHA256, "approved registry changed")
    return load_json(REGISTRY_PATH)


def score_datasets(datasets, start, end, link_required, dropouts=0, synthetic_pressure_verified=False):
    limits = registry()
    require(type(start) is int and type(end) is int and start > 0 and
            end - start == limits["interval"]["score_duration_us"],
            "invalid declared interval")
    require(type(link_required) is bool, "telemetry expectation must be boolean")
    failures, missing, topics = [], [], {}
    if dropouts > limits["validity"]["maximum_logger_dropouts"]:
        failures.append({"check": "logger_dropouts", "count": int(dropouts)})
    by_name = {}
    for dataset in datasets:
        if dataset.multi_id == 0:
            require(dataset.name not in by_name, "duplicate instance 0: " + dataset.name)
            by_name[dataset.name] = dataset.data
    for name in limits["required_raw_topics_instance_0"]:
        if name not in by_name:
            missing.append(name + ": missing instance 0")
            continue
        data = by_name[name]
        if "timestamp" not in data:
            missing.append(name + ": missing timestamp")
            continue
        try:
            all_t = stamps(data, "timestamp", name)
        except Exception as error:
            failures.append({"check": name + ".timestamp", "reason": str(error)})
            continue
        # Preserve ULog record order; never sort away a time discontinuity.
        inside = np.flatnonzero((all_t >= start) & (all_t <= end))
        if not inside.size:
            missing.append(name + ": no records in interval")
            continue
        first, last = int(inside[0]), int(inside[-1])
        left, right = max(0, first - 1), min(all_t.size - 1, last + 1)
        t = all_t[left:right + 1]
        if t[0] > start or t[-1] < end:
            missing.append(name + ": interval not bracketed by logged records")
        active = (t >= start) & (t <= end)
        # Include the prior record for state held at the left boundary.
        state_active = active.copy()
        if t[0] < start:
            state_active[0] = True
        entry = {"instance": 0, "sample_count": int(np.count_nonzero(active)),
                 "first_support_us": int(t[0]), "last_support_us": int(t[-1])}
        topics[name] = entry

        def violation(check, indices):
            indices = np.asarray(indices, dtype=np.int64)
            if indices.size:
                failures.append({"check": name + "." + check,
                                 "timestamps_us": [int(t[i]) for i in indices]})

        def field(key):
            if key not in data:
                missing.append(name + ": missing " + key)
                return None
            values = np.asarray(data[key])
            if values.ndim != 1 or values.size != all_t.size or values.dtype.kind not in "biuf":
                failures.append({"check": name + "." + key, "reason": "invalid field shape/type"})
                return None
            return values[left:right + 1]

        violation("backward_publication", np.flatnonzero(np.diff(t) < 0) + 1)
        overlap = np.maximum(0, np.minimum(t[1:], end) - np.maximum(t[:-1], start))
        entry["maximum_logged_gap_us"] = int(np.max(overlap)) if overlap.size else None
        if name in limits["maximum_logged_gap_us"]:
            violation("logged_gap", np.flatnonzero(overlap > limits["maximum_logged_gap_us"][name]) + 1)
        sample = None
        if "timestamp_sample" in data:
            try:
                sample = stamps(data, "timestamp_sample", name)[left:right + 1]
                require(sample.size == t.size, "sample length mismatch")
                exception = limits["timestamp_policy"]["synthetic_pressure_zero_sample_exception"]
                pressure_exception = False
                if name == exception["topic"]:
                    all_sample = np.asarray(data["timestamp_sample"])
                    if np.any(all_sample == 0) and np.any(all_sample != 0):
                        failures.append({"check": "differential_pressure.mixed_capture_time_availability"})
                if name == exception["topic"] and np.any(sample == 0):
                    identity = field("device_id")
                    pressure_exception = (synthetic_pressure_verified and
                                          np.all(np.asarray(data["timestamp_sample"]) == 0) and
                                          identity is not None and
                                          np.all(np.asarray(data["device_id"]) == exception["device_id"]))
                    if pressure_exception:
                        entry["unavailable_capture_time"] = {"exception": "approved_v2_stock_synthetic_pressure",
                                                             "zero_sample_count": int(np.count_nonzero(active))}
                    else:
                        failures.append({"check": "differential_pressure.unqualified_zero_sample_exception"})
                age = t - sample
                if not pressure_exception:
                    violation("sample_order", np.flatnonzero(np.diff(sample) <= 0) + 1)
                    violation("sample_zero", np.flatnonzero(active & (sample == 0)))
                    violation("negative_age", np.flatnonzero(active & (age < 0)))
                    entry["maximum_sample_age_us"] = int(np.max(age[active]))
                    if name in limits["maximum_publication_minus_sample_age_us"]:
                        violation("sample_age", np.flatnonzero(active &
                                  (age > limits["maximum_publication_minus_sample_age_us"][name])))
            except Exception as error:
                failures.append({"check": name + ".timestamp_sample", "reason": str(error)})
        elif name in limits["maximum_publication_minus_sample_age_us"] or name == "differential_pressure":
            missing.append(name + ": missing timestamp_sample")
        for key in limits["validity"]["required_finite_fields"].get(name, []):
            values = field(key)
            if values is not None:
                violation("finite_" + key, np.flatnonzero(state_active & ~np.isfinite(values)))
        rules = dict(STATE_RULES.get(name, {}))
        if link_required and name == "telemetry_status":
            rules["heartbeat_type_gcs"] = 1
        if link_required and name == "vehicle_status":
            rules["gcs_connection_lost"] = 0
        for key, expected in rules.items():
            values = field(key)
            if values is not None:
                violation(key, np.flatnonzero(state_active & (values != expected)))
        if name == "sensor_gps":
            values = field("fix_type")
            if values is not None:
                violation("fix_type", np.flatnonzero(state_active & (~np.isfinite(values) | (values < 3))))
        if name == "vehicle_attitude" and sample is not None:
            pairs = np.flatnonzero((np.diff(t) == 0) & active[:-1] & active[1:])
            entry["equal_publication_pairs"] = [
                {"timestamp_us": int(t[i]), "sample_us": [int(sample[i]), int(sample[i + 1])],
                 "ages_us": [int(t[i] - sample[i]), int(t[i + 1] - sample[i + 1])]}
                for i in pairs]
        if name == "telemetry_status" and link_required:
            count = field("rx_message_count")
            if count is not None:
                if count.dtype.kind not in "iu" or np.any(count < 0) or np.any(count > 2**32 - 1):
                    failures.append({"check": "telemetry_status.rx_counter", "reason": "invalid counter"})
                else:
                    delta = np.diff(count.astype(np.int64))
                    violation("rx_counter_decrease", np.flatnonzero(delta < 0) + 1)
                    advances = t[1:][(delta > 0) & (t[1:] >= start) & (t[1:] <= end)]
                    rx_gaps = np.diff(np.concatenate(([start], advances, [end])))
                    entry["maximum_rx_advance_gap_us"] = int(np.max(rx_gaps))
                    if np.any(rx_gaps > limits["gui"]["maximum_time_without_rx_advance_us"]):
                        failures.append({"check": "telemetry_status.rx_advance_gap",
                                         "maximum_us": int(np.max(rx_gaps))})
    return {"status": "fail" if failures else "incomplete" if missing else "pass",
            "interval_us": [start, end], "registry_sha256": REGISTRY_SHA256,
            "failures": failures, "missing": missing, "topics": topics}


def score_ulog(path, expected_hash, start, end, link_required, ready_us=None, synthetic_pressure_verified=False):
    require(digest(path) == expected_hash, "ULog hash mismatch")
    log = ULog(str(path), message_name_filter_list=registry()["required_raw_topics_instance_0"])
    result = score_datasets(log.data_list, start, end, link_required, len(log.dropouts), synthetic_pressure_verified)
    if ready_us is not None:
        rules = {name: dict(fields) for name, fields in STATE_RULES.items()}
        rules["vehicle_status"]["nav_state"] = 4
        if link_required:
            rules["telemetry_status"] = {"heartbeat_type_gcs": 1}
        streams = {item.name: item.data for item in log.data_list if item.multi_id == 0}
        rules["sensor_gps"] = {"fix_type": "3d_or_better"}
        for topic, fields in rules.items():
            data = streams.get(topic, {})
            if "timestamp" not in data:
                result["missing"].append("ready raw stream missing: " + topic)
                continue
            indices = np.flatnonzero(np.asarray(data["timestamp"]) <= ready_us)
            if not indices.size:
                result["missing"].append("ready raw record missing: " + topic)
                continue
            for field, expected in fields.items():
                if field not in data:
                    result["missing"].append("ready field missing: " + topic + "." + field)
                else:
                    value = data[field][indices[-1]]
                    valid = value >= 3 if expected == "3d_or_better" else value == expected
                    if not valid:
                        result["failures"].append({"check": "ready." + topic + "." + field})
        if log.initial_parameters.get("SYS_AUTOSTART") != 4003:
            result["failures"].append({"check": "SYS_AUTOSTART is not stock Cessna 4003"})
    if log.start_timestamp > start or log.last_timestamp < end:
        result["missing"].append("ULog does not cover declared interval")
        if result["status"] == "pass":
            result["status"] = "incomplete"
    result["ulog_sha256"] = expected_hash
    result["status"] = "fail" if result["failures"] else "incomplete" if result["missing"] else "pass"
    return result
