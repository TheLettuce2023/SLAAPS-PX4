#!/usr/bin/env python3
"""Extract bounded, hash-bound observations from existing ULogs; never run PX4."""

import argparse
from pathlib import Path

import numpy as np
from pyulog import ULog

from contract import (CAPABILITIES, LIMITATIONS, MAX_LINE_BYTES, MAX_RECEIPTS, SCHEMA,
                      TOPICS, checked_source, digest, encode, fields_for, require, scalar)


def summarize(data, fields):
    """Describe logged chronology without assigning freshness/health thresholds."""
    result = {"publication": {}, "sample": None, "sample_age_at_publication_us": None,
              "field_changes": {}, "nonfinite_values": {}}
    for field, label in (("timestamp", "publication"), ("timestamp_sample", "sample")):
        if field not in fields:
            continue
        values = [int(value) for value in data[field]]
        differences = [b - a for a, b in zip(values, values[1:])]
        result[label] = {"first_us": values[0], "last_us": values[-1],
                         "zero_values": values.count(0),
                         "equal_steps": sum(d == 0 for d in differences),
                         "backward_steps": sum(d < 0 for d in differences),
                         "maximum_forward_gap_us": max([0] + differences)}
    if "timestamp_sample" in fields:
        ages = [int(t) - int(s) for t, s in zip(data["timestamp"], data["timestamp_sample"]) if s > 0]
        if ages:
            result["sample_age_at_publication_us"] = {
                "minimum": min(ages), "maximum": max(ages),
                "negative_count": sum(age < 0 for age in ages)}
    for field in fields:
        values = data[field]
        if values.dtype.kind == "f":
            count = int(np.count_nonzero(~np.isfinite(values)))
            if count:
                result["nonfinite_values"][field] = count
        if field.endswith(("_counter", "_changes", "_valid", "_device_id")) or field.startswith(("cs_", "fs_")) or field == "primary_instance":
            result["field_changes"][field] = int(np.count_nonzero(values[1:] != values[:-1]))
    return result


def collect(source, expected_sha256, destination):
    source = Path(source).resolve()
    destination = Path(destination).absolute()
    checked_source(source, expected_sha256)
    require(not destination.exists(), "output must be a new directory")
    log = ULog(str(source), message_name_filter_list=list(TOPICS))
    require(log.file_corruption is False, "ULog parser reported corruption")
    datasets = sorted((d for d in log.data_list if d.name in TOPICS),
                      key=lambda d: (d.name, d.multi_id))
    require(datasets, "no estimator observations in ULog")
    streams, total, identities = [], 0, set()
    for dataset in datasets:
        identity = (dataset.name, int(dataset.multi_id))
        require(identity not in identities, "duplicate topic instance")
        identities.add(identity)
        data = dataset.data
        require("timestamp" in data, "missing publication timestamp")
        fields, missing = fields_for(dataset.name, data)
        count = len(data["timestamp"])
        require(count > 0 and total + count <= MAX_RECEIPTS, "receipt count outside bound")
        total += count
        for field in fields:
            values = np.asarray(data[field])
            require(values.ndim == 1 and len(values) == count and values.dtype.kind in "biuf",
                    "invalid array: " + field)
            if field in ("timestamp", "timestamp_sample"):
                require(values.dtype.kind in "iu" and all(int(v) >= 0 for v in values),
                        "invalid unsigned timestamp: " + field)
        streams.append({"topic": dataset.name, "instance": int(dataset.multi_id),
                        "count": count, "fields": fields, "missing_fields": missing,
                        "observations": summarize(data, fields)})
    destination.mkdir(parents=True, exist_ok=False)
    partial = destination / "receipts.jsonl.partial"
    with partial.open("x", encoding="utf-8", newline="\n") as output:
        for dataset, stream in zip(datasets, streams):
            for index in range(stream["count"]):
                row = {"topic": dataset.name, "instance": int(dataset.multi_id),
                       "logged_index": index,
                       "values": {key: scalar(dataset.data[key][index]) for key in stream["fields"]}}
                line = encode(row) + "\n"
                require(len(line.encode("utf-8")) <= MAX_LINE_BYTES, "receipt exceeds bound")
                output.write(line)
    # An input changed during extraction cannot produce a completed package.
    checked_source(source, expected_sha256)
    journal = destination / "receipts.jsonl"
    partial.rename(journal)
    result = {"schema": SCHEMA, "collection_complete": True,
              "source": {"sha256": expected_sha256, "bytes": source.stat().st_size},
              "receipt_count": total, "streams": streams,
              "missing_topics": sorted(set(TOPICS) - {d.name for d in datasets}),
              "logger_dropout_count": len(log.dropouts),
              "limitations": list(LIMITATIONS), "collector_capabilities": dict(CAPABILITIES),
              "receipts": {"path": "receipts.jsonl", "sha256": digest(journal), "bytes": journal.stat().st_size}}
    # Manifest is the final completion marker; partial directories are not evidence.
    with (destination / "manifest.json").open("x", encoding="utf-8", newline="\n") as output:
        output.write(encode(result) + "\n")
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--sha256", required=True)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    result = collect(args.source, args.sha256, args.output)
    print(encode({"collection_complete": True, "receipt_count": result["receipt_count"],
                  "streams": len(result["streams"]), "positive_admission": False}))
