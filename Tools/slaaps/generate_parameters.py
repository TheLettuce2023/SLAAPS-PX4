#!/usr/bin/env python3
"""Generate PX4/QGC parameter metadata from the configuration's scalar schema."""

import argparse
import csv
import io
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
SCHEMA = ROOT / "src/lib/slaaps/VehicleParameters.def"
OUTPUT = ROOT / "src/modules/slaaps/module.yaml"


def definitions():
    names, fields = set(), set()
    for line in SCHEMA.read_text(encoding="utf-8").splitlines():
        if line.startswith("//") or not line.strip():
            continue
        match = re.fullmatch(r"SLAAPS_(FLOAT|INT)\((.*)\)", line)
        if not match:
            raise ValueError(f"Invalid schema line: {line}")
        kind, body = match.groups()
        parts = next(csv.reader(io.StringIO(body), skipinitialspace=True))
        if len(parts) != 8:
            raise ValueError(f"Expected eight fields: {line}")
        field, name, group, description, unit, lower, upper, default = parts
        assert re.fullmatch(r"[A-Z][A-Z0-9_]{0,15}", name), name
        assert name not in names and field not in fields, name
        names.add(name)
        fields.add(field)
        convert = int if kind == "INT" else float
        lower, upper, default = [convert(s.removesuffix("F")) for s in (lower, upper, default)]
        assert lower <= default <= upper
        yield dict(field=field, name=name, group=group, description=description,
                   unit=unit, min=lower, max=upper, default=default,
                   type="int32" if kind == "INT" else "float")


def render():
    groups = {}
    for item in definitions():
        parameter = dict(
            description=dict(
                short=item["description"],
                long="SLAAPS vehicle configuration. Edit while disabled and apply a complete "
                     "validated configuration before mode entry. Zero physical defaults are "
                     "unconfigured, not vehicle measurements. Saving parameters does not "
                     "establish flight readiness."
            ),
            type=item["type"], min=item["min"], max=item["max"], default=item["default"])
        if item["unit"]:
            parameter["unit"] = item["unit"]
        if item["type"] == "float":
            parameter["decimal"] = 6
        groups.setdefault(item["group"], {})[item["name"]] = parameter
    groups["SLAAPS Configuration"] = {
        "SLA_CFG_APPLY": dict(
            description=dict(short="Apply completed configuration edits",
                             long="Increase this counter after all vehicle parameters have been edited. "
                                  "Requires disarmed status. Each increase validates one complete snapshot. "
                                  "Restarting the supervisor requires a new increase; a saved value never "
                                  "automatically activates a configuration."),
            type="int32", min=0, max=2147483647, default=0),
        "SLA_CFG_KIND": dict(
            description=dict(short="Configuration validation scope",
                             long="Choose the validation scope before increasing SLA_CFG_APPLY. "
                                  "Validation is not flight acceptance or permission to enable outputs."),
            type="enum", values={0: "Servo bench", 1: "Stabilized manual"},
            default=0),
    }
    # JSON is a YAML subset. Keep the artifact deterministic without a second
    # schema or a dependency on a particular YAML serializer.
    document = dict(module_name="SLAAPS",
                    parameters=[dict(group=group, definitions=items) for group, items in groups.items()])
    return json.dumps(document, indent=2) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    expected = render()
    if args.check:
        if not OUTPUT.exists() or OUTPUT.read_text(encoding="utf-8") != expected:
            raise SystemExit("SLAAPS parameter metadata is stale; run Tools/slaaps/generate_parameters.py")
    else:
        OUTPUT.parent.mkdir(parents=True, exist_ok=True)
        OUTPUT.write_text(expected, encoding="utf-8", newline="\n")
    print(f"SLAAPS metadata: {len(list(definitions()))} vehicle parameters plus 2 apply controls")


if __name__ == "__main__":
    main()
