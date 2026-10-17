#!/usr/bin/env python3
"""Independently reconstruct archived rows and summaries from the pinned ULog.

Shares the field contract and JSON primitives, not collector control flow,
summarization or scalar conversion. Verification cannot establish live trust.
"""

import argparse
import hashlib
import math
from pathlib import Path

from pyulog import ULog

from contract import (CAPABILITIES, LIMITATIONS, MAX_LINE_BYTES, MAX_RECEIPTS, SCHEMA,
                      TOPICS, checked_source, digest, encode, fields_for, load, loads, require)


def verify(source, expected_sha256, directory):
    source, directory = Path(source).resolve(), Path(directory).resolve()
    checked_source(source, expected_sha256)
    manifest = load(directory / "manifest.json")
    journal = directory / "receipts.jsonl"
    require(journal.is_file() and not journal.is_symlink(), "missing/nonregular journal")
    require(journal.stat().st_size <= MAX_RECEIPTS * MAX_LINE_BYTES, "journal exceeds bound")
    log = ULog(str(source), message_name_filter_list=list(TOPICS))
    require(log.file_corruption is False, "ULog parser reported corruption")
    datasets = sorted((d for d in log.data_list if d.name in TOPICS), key=lambda d: (d.name, d.multi_id))
    require(datasets, "no estimator observations")
    streams, count, seen = [], 0, set()
    consumed_hash = hashlib.sha256()
    consumed_bytes = 0
    with journal.open("rb") as receipts:
        for dataset in datasets:
            key = (dataset.name, int(dataset.multi_id))
            require(key not in seen, "duplicate stream")
            seen.add(key)
            fields, missing = fields_for(dataset.name, dataset.data)
            size = len(dataset.data["timestamp"])
            require(size > 0 and count + size <= MAX_RECEIPTS, "receipt count outside bound")
            metrics = {"publication": {}, "sample": None, "sample_age_at_publication_us": None,
                       "field_changes": {}, "nonfinite_values": {}}
            previous, age_min, age_max, age_negative = None, None, None, 0
            for index in range(size):
                values = {}
                for field in fields:
                    raw = dataset.data[field][index].item()
                    require(type(raw) in (int, float, bool), "invalid raw scalar")
                    if type(raw) is float and not math.isfinite(raw):
                        values[field] = {"nonfinite": "nan" if math.isnan(raw) else ("+inf" if raw > 0 else "-inf")}
                        metrics["nonfinite_values"][field] = metrics["nonfinite_values"].get(field, 0) + 1
                    else:
                        values[field] = raw
                    if field.endswith(("_counter", "_changes", "_valid", "_device_id")) or field.startswith(("cs_", "fs_")) or field == "primary_instance":
                        changed = previous is not None and raw != previous[field]
                        metrics["field_changes"][field] = metrics["field_changes"].get(field, 0) + int(changed)
                for field, label in (("timestamp", "publication"), ("timestamp_sample", "sample")):
                    if field not in fields:
                        continue
                    stamp = values[field]
                    require(type(stamp) is int and stamp >= 0, "invalid timestamp")
                    if index == 0:
                        metrics[label] = {"first_us": stamp, "last_us": stamp, "zero_values": 0,
                                          "equal_steps": 0, "backward_steps": 0, "maximum_forward_gap_us": 0}
                    timer = metrics[label]
                    timer["zero_values"] += int(stamp == 0)
                    if previous is not None:
                        difference = stamp - previous[field]
                        timer["equal_steps"] += int(difference == 0)
                        timer["backward_steps"] += int(difference < 0)
                        timer["maximum_forward_gap_us"] = max(timer["maximum_forward_gap_us"], difference)
                    timer["last_us"] = stamp
                if values.get("timestamp_sample", 0) > 0:
                    age = values["timestamp"] - values["timestamp_sample"]
                    age_min = age if age_min is None else min(age_min, age)
                    age_max = age if age_max is None else max(age_max, age)
                    age_negative += int(age < 0)
                expected = {"topic": dataset.name, "instance": int(dataset.multi_id),
                            "logged_index": index, "values": values}
                line = receipts.readline(MAX_LINE_BYTES + 1)
                require(line.endswith(b"\n") and len(line) <= MAX_LINE_BYTES, "missing/oversize receipt")
                consumed_hash.update(line)
                consumed_bytes += len(line)
                actual = loads(line.decode("utf-8"))
                # Canonical comparison rejects bool/int substitution and extra fields.
                require(encode(actual) == encode(expected), "receipt differs from raw ULog")
                previous = {field: dataset.data[field][index].item() for field in fields}
            if age_min is not None:
                metrics["sample_age_at_publication_us"] = {"minimum": age_min, "maximum": age_max,
                                                          "negative_count": age_negative}
            streams.append({"topic": dataset.name, "instance": int(dataset.multi_id), "count": size,
                            "fields": fields, "missing_fields": missing, "observations": metrics})
            count += size
        require(receipts.read(1) == b"", "extra receipts")
    checked_source(source, expected_sha256)
    require(digest(journal) == consumed_hash.hexdigest() and journal.stat().st_size == consumed_bytes,
            "journal changed during verification")
    expected = {"schema": SCHEMA, "collection_complete": True,
                "source": {"sha256": expected_sha256, "bytes": source.stat().st_size},
                "receipt_count": count, "streams": streams,
                "missing_topics": sorted(set(TOPICS) - {d.name for d in datasets}),
                "logger_dropout_count": len(log.dropouts),
                "limitations": list(LIMITATIONS), "collector_capabilities": dict(CAPABILITIES),
                "receipts": {"path": "receipts.jsonl", "sha256": digest(journal), "bytes": journal.stat().st_size}}
    require(encode(manifest) == encode(expected), "manifest differs from independent raw reconstruction")
    return {"verified": True, "receipt_count": count, "stream_count": len(streams),
            "positive_admission": False, "publisher_continuity_qualified": False}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--sha256", required=True)
    parser.add_argument("--archive", required=True, type=Path)
    args = parser.parse_args()
    print(encode(verify(args.source, args.sha256, args.archive)))
