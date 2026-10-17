#!/usr/bin/env python3
"""Check P2F runtime traces produced by an alternate build configuration."""

import argparse
import importlib.util
import json
from pathlib import Path


HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location(
    "p2f_check", HERE / "check_selected_state.py")
p2f_check = importlib.util.module_from_spec(spec)
spec.loader.exec_module(p2f_check)


def check_case(runtime, run_id, case, sensor):
    runtime = Path(runtime).resolve()
    merged = p2f_check.read(runtime / "merged.json")
    raw = p2f_check.read(runtime / "observations.json") if case == "normal" else None
    p2f_check.p2d_check.trace(merged, run_id, case, raw)
    committed = p2f_check.read(runtime / "committed.json")
    p2f_check.p2e_check.committed_trace(committed, run_id, case, merged, sensor)
    return p2f_check.selected_trace(
        p2f_check.read(runtime / "selected.json"), run_id, case, committed)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--configuration", required=True)
    parser.add_argument("--normal", type=Path, required=True)
    parser.add_argument("--normal-run-id", required=True)
    parser.add_argument("--restart", type=Path, required=True)
    parser.add_argument("--restart-run-id", required=True)
    args = parser.parse_args()
    sensor = p2f_check.read(args.manifest)["sensor_provenance"]
    result = {
        "configuration": args.configuration,
        "cases": [
            check_case(args.normal, args.normal_run_id, "normal", sensor),
            check_case(args.restart, args.restart_run_id, "restart", sensor),
        ],
    }
    print(json.dumps(result, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
