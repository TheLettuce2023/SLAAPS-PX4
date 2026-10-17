#!/usr/bin/env python3
"""Recompute stock baseline ULog measurements; never grant Phase 1 pass.

Requires PyULog and NumPy in the WSL analysis environment. An expected ULog
hash must come from a separately retained inventory or predeclared manifest.
"""

import argparse
import hashlib
import json
import math
import re
import sys
from pathlib import Path

import numpy as np
from pyulog import ULog


SHA256 = re.compile(r"[0-9a-f]{64}\Z")
NUMERIC_FIELDS = {
    "vehicle_attitude": ("q[0]", "q[1]", "q[2]", "q[3]"),
    "vehicle_local_position": ("x", "y", "z", "vx", "vy", "vz"),
    "vehicle_global_position": ("lat", "lon", "alt"),
    "sensor_gps": ("latitude_deg", "longitude_deg", "altitude_msl_m"),
    "sensor_baro": ("pressure", "temperature"),
    "sensor_mag": ("x", "y", "z"),
    "airspeed_validated": ("calibrated_airspeed_m_s", "true_airspeed_m_s"),
    "differential_pressure": ("differential_pressure_pa",),
}
REQUIRED_TOPICS = (
    "vehicle_status", "vehicle_local_position", "vehicle_global_position",
    "vehicle_attitude", "vehicle_imu", "sensor_gps", "sensor_baro",
    "sensor_mag", "estimator_status_flags", "actuator_motors",
    "actuator_servos", "airspeed_validated", "differential_pressure",
    "telemetry_status", "sensor_combined",
)
ARCHIVED_TOPICS = set(REQUIRED_TOPICS) - {"sensor_combined"}


class EvidenceError(Exception):
    pass


def require(condition, reason):
    if not condition:
        raise EvidenceError(reason)


def equal_exact(left, right):
    return type(left) is type(right) and left == right


def digest(path):
    value = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def unique_pairs(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, "duplicate JSON key: " + key)
        result[key] = value
    return result


def finite_float(value):
    number = float(value)
    require(math.isfinite(number), "non-finite JSON number")
    return number


def nonfinite_constant(value):
    raise EvidenceError("non-finite JSON constant: " + value)


def load_json(path):
    with path.open(encoding="utf-8-sig") as source:
        return json.load(source, object_pairs_hook=unique_pairs,
                         parse_float=finite_float, parse_constant=nonfinite_constant)


def stamps(data, field, topic):
    require(field in data, "missing " + topic + "." + field)
    values = np.asarray(data[field])
    require(values.ndim == 1 and values.size > 0 and values.dtype.kind in "iu",
            "invalid " + topic + "." + field)
    require(np.min(values) >= 0 and np.max(values) <= np.iinfo(np.int64).max,
            "out-of-range " + topic + "." + field)
    return values.astype(np.int64)


def timing(values):
    gaps = np.diff(values)
    return {
        "samples": int(values.size),
        "first_us": int(values[0]),
        "last_us": int(values[-1]),
        "zero_steps": int(np.count_nonzero(gaps == 0)),
        "negative_steps": int(np.count_nonzero(gaps < 0)),
        "max_gap_us": int(np.max(gaps)) if gaps.size else None,
    }


def inspect_dataset(dataset):
    topic = dataset.name
    data = dataset.data
    publication = stamps(data, "timestamp", topic)
    summary = {"instance": int(dataset.multi_id), "timestamp": timing(publication),
               "numeric_nonfinite": {}}
    if "timestamp_sample" in data:
        sample = stamps(data, "timestamp_sample", topic)
        require(sample.size == publication.size, "sample length mismatch: " + topic)
        active = sample > 0
        age = publication[active] - sample[active]
        summary["timestamp_sample"] = timing(sample)
        summary["sample_age_us"] = {
            "negative": int(np.count_nonzero(age < 0)),
            "max": int(np.max(age)) if age.size else None,
        }
    for field in NUMERIC_FIELDS.get(topic, ()):
        require(field in data, "missing " + topic + "." + field)
        values = np.asarray(data[field], dtype=np.float64)
        require(values.ndim == 1 and values.size == publication.size,
                "invalid numeric field: " + topic + "." + field)
        summary["numeric_nonfinite"][field] = int(np.count_nonzero(~np.isfinite(values)))
    if topic == "vehicle_attitude":
        require("timestamp_sample" in data, "missing attitude sample timestamp")
        pairs = np.flatnonzero(np.diff(publication) == 0)
        sample = stamps(data, "timestamp_sample", topic)
        q = np.stack([np.asarray(data[field]) for field in NUMERIC_FIELDS[topic]], axis=1)
        gaps = sample[pairs + 1] - sample[pairs]
        summary["equal_publication_pairs"] = {
            "count": int(pairs.size),
            "same_sample": int(np.count_nonzero(gaps == 0)),
            "same_quaternion": int(np.count_nonzero(np.all(q[pairs] == q[pairs + 1], axis=1))),
            "sample_gap_counts_us": {str(int(value)): int(count)
                                     for value, count in zip(*np.unique(gaps, return_counts=True))},
            "first_age_counts_us": {str(int(value)): int(count)
                                    for value, count in zip(*np.unique(publication[pairs] - sample[pairs],
                                                                        return_counts=True))},
            "second_age_counts_us": {str(int(value)): int(count)
                                     for value, count in zip(*np.unique(publication[pairs + 1] - sample[pairs + 1],
                                                                         return_counts=True))},
        }
    return summary


def inspect_log(path, expected_sha256):
    require(SHA256.fullmatch(expected_sha256) is not None,
            "expected SHA-256 must be 64 lowercase hex characters")
    actual = digest(path)
    require(actual == expected_sha256, "ULog SHA-256 mismatch")
    log = ULog(str(path), message_name_filter_list=list(REQUIRED_TOPICS))
    by_topic = {}
    for dataset in log.data_list:
        if dataset.name in REQUIRED_TOPICS and dataset.multi_id == 0:
            require(dataset.name not in by_topic, "duplicate topic instance 0: " + dataset.name)
            by_topic[dataset.name] = inspect_dataset(dataset)
    missing = sorted(set(REQUIRED_TOPICS) - set(by_topic))
    return {"ulog_sha256": actual, "ulog_bytes": path.stat().st_size,
            "dropouts": len(log.dropouts), "missing_topics": missing,
            "topics": by_topic}


def compare_archived(current, archived):
    require(type(archived) is dict and
            archived.get("schema") == "sitl-baseline-ulog-measurements/v0" and
            archived.get("ulog_sha256") == current["ulog_sha256"] and
            equal_exact(archived.get("ulog_bytes"), current["ulog_bytes"]),
            "archived measurement identity mismatch")
    require(equal_exact(archived.get("dropouts"), current["dropouts"]) and
            archived.get("missing_topics") == [], "archived dropout/topic mismatch")
    old_topics = archived.get("topics")
    require(type(old_topics) is dict and ARCHIVED_TOPICS == set(old_topics),
            "archived topic set mismatch")
    for topic in sorted(ARCHIVED_TOPICS):
        records = old_topics[topic]
        require(type(records) is list and records and records[0].get("instance") == 0,
                "archived instance 0 missing: " + topic)
        old = records[0]
        new = current["topics"].get(topic)
        require(new is not None, "raw instance 0 missing: " + topic)
        for field in ("samples", "first_us", "last_us", "zero_steps",
                      "negative_steps", "max_gap_us"):
            require(equal_exact(old.get("timestamp", {}).get(field), new["timestamp"][field]),
                    "archived timing mismatch: " + topic + "." + field)
        for field, count in new["numeric_nonfinite"].items():
            require(equal_exact(old.get("required_numeric", {}).get(field, {}).get("nonfinite"), count),
                    "archived finite-field mismatch: " + topic + "." + field)
        if topic == "vehicle_attitude":
            pairs = old.get("equal_timestamp_pairs", {})
            recomputed = new["equal_publication_pairs"]
            for old_key, new_key in (("count", "count"),
                                     ("same_timestamp_sample", "same_sample"),
                                     ("same_quaternion", "same_quaternion")):
                require(equal_exact(pairs.get(old_key), recomputed[new_key]),
                        "archived attitude pair mismatch: " + old_key)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("ulog", type=Path)
    parser.add_argument("--expected-sha256", required=True)
    parser.add_argument("--archived-measurements", type=Path)
    parser.add_argument("--output", type=Path,
                        help="create a new JSON report; existing files are never replaced")
    args = parser.parse_args()
    try:
        result = inspect_log(args.ulog, args.expected_sha256)
        if args.archived_measurements:
            compare_archived(result, load_json(args.archived_measurements))
        result.update({"schema": "sitl-baseline-raw-ulog-inspection/v0",
                       "status": "incomplete", "phase1_result": "incomplete",
                       "reason": "no approved scoring registry or predeclared interval",
                       "archived_measurements_matched": args.archived_measurements is not None,
                       "inspector_sha256": digest(Path(__file__))})
        code = 3
    except Exception as error:  # Parser and schema failures must not become a pass.
        result = {"schema": "sitl-baseline-raw-ulog-inspection/v0",
                  "status": "fail", "phase1_result": "fail",
                  "reason": str(error)}
        code = 2
    serialized = json.dumps(result, indent=2, allow_nan=False) + "\n"
    if args.output:
        try:
            with args.output.open("x", encoding="utf-8") as destination:
                destination.write(serialized)
        except OSError as error:
            print("cannot create output: " + str(error), file=sys.stderr)
            return 2
        print("status=" + result["status"] + " report=" + str(args.output))
    else:
        print(serialized, end="")
    return code


if __name__ == "__main__":
    sys.exit(main())
