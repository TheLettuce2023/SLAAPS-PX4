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
        if item["name"] == "SLA_IN_SOURCE":
            parameter.update(type="enum", values={0: "Unconfigured", 1: "RC only", 2: "Pinned MAVLink only"})
        elif item["name"] in ("SLA_R_INPUT", "SLA_P_INPUT", "SLA_Y_INPUT"):
            parameter.update(type="enum", values={-1: "Unconfigured", 0: "Roll", 1: "Pitch", 2: "Yaw"})
        elif item["name"] == "SLA_SCH_EN":
            parameter.update(type="enum", values={0: "Disabled", 1: "Empirical 2x2 table"})
        elif item["name"] == "SLA_CMP_EN":
            parameter.update(type="enum", values={0: "Disabled", 1: "Observe configured companion"})
        elif item["name"] == "SLA_CMP_INST":
            parameter.update(type="enum", values={-1: "Unconfigured", **{i: f"MAVLink link {i}" for i in range(4)}})
        elif item["name"] == "SLA_PWR_INST":
            parameter.update(type="enum", values={-1: "Unconfigured", **{i: f"Battery instance {i}" for i in range(3)}})
        elif item["name"] == "SLA_PWR_SRC":
            parameter.update(type="enum", values={-1: "Unconfigured", 0: "Power module", 1: "External", 2: "ESC telemetry"})
        elif item["name"] == "SLA_SCH_BAT_INST":
            parameter.update(type="enum", values={-1: "Unconfigured", **{i: f"Instance {i}" for i in range(3)}})
        elif item["name"] in ("SLA_SCH_AS_INST", "SLA_SCH_AD_INST"):
            parameter.update(type="enum", values={-1: "Unconfigured", **{i: f"Instance {i}" for i in range(4)}})
        elif item["name"] == "SLA_EDF_BMAX":
            parameter["description"]["long"] = (
                "Explicit normalized EDF commissioning ceiling; zero denies commissioning. "
                "Choose a hardware-appropriate limit while disarmed and apply the complete EDF commissioning "
                "profile. This software range is not a qualified hardware ceiling. Select Bench/EDF, "
                "observe low throttle and readiness, deliberately enable, then deliberately arm through Commander. "
                "Profile, limit and duration remain bound; stop, disarm, kill or input/permission loss ends the session. "
                "This is ground commissioning, never flight admission.")
        if item["field"].startswith("companion."):
            parameter["description"]["long"] = (
                "Companion transport source selection. Configure and apply while disarmed. "
                "The exact MAVLink link, signed header system/component and positive 31-bit payload source ID "
                "must match. The age limit bounds local packet receipt, not producer command validity. "
                "Authentication and source selection do not establish a session, trusted clock mapping, "
                "acceleration control or flight permission. Zero age or identity is unconfigured."
            )
        if item["field"].startswith("power."):
            parameter["description"]["long"] = (
                "Explicit propulsion battery selection and voltage limits. Configure and apply while disarmed. "
                "Stabilized manual and EDF commissioning require the exact battery_status instance, ID, source, "
                "positive age limit and ordered positive voltage band. Servo bench may stage incomplete fields. "
                "The selected publisher must remain connected, fresh, without warning, fault or imminent power-off, "
                "and within the operator-configured voltage band. There is no alternate battery fallback. "
                "Age is bounded from the original battery report, not polling time. Zero defaults are unconfigured; "
                "the software voltage range is not hardware qualification, a measured thrust-curve condition or "
                "flight permission. Do not edit an active profile."
            )
        if item["field"].startswith("schedule."):
            parameter["description"]["long"] = (
                "Optional empirical control table, disabled by default. Configure and apply the complete profile "
                "while disarmed. Coordinates are raw indicated airspeed from the pinned airspeed instance and "
                "normalized EDF owner command; neither groundspeed nor inferred thrust is substituted. "
                "Corner indices are [IAS low/high][EDF low/high]. Every corner supplies body-rate P/I/D gains and "
                "signed moment derivatives about CG; the existing moment normalization and calibrated servo "
                "conversion remain fixed. Pinned vehicle_air_data density and battery_status voltage must stay "
                "fresh and inside the declared ranges; no density or voltage scaling is inferred. Selectors have "
                "no automatic fallback. Filter and raw transient limits must be explicitly configured. "
                "Enabling or numerically validating this table establishes neither angle-of-attack/sideslip or "
                "EDF-wash coverage nor a qualified envelope, measured coefficients or flight permission. "
                "Do not edit an active profile.")
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
                                  "An unchanged previously accepted snapshot is restored after a disarmed "
                                  "restart. Later edits require another increase. Restoration grants no output permission."),
            type="int32", min=0, max=2147483647, default=0),
        "SLA_CFG_KIND": dict(
            description=dict(short="Configuration validation scope",
                             long="Choose the validation scope before increasing SLA_CFG_APPLY. "
                                  "Validation is not flight acceptance or permission to enable outputs."),
            type="enum", values={0: "Servo bench", 1: "Stabilized manual", 2: "EDF commissioning"},
            default=0),
    }
    for name, description in (("SLA_CFG_SAVED", "Accepted configuration apply counter"),
                              ("SLA_CFG_SCOPE", "Accepted configuration validation scope"),
                              ("SLA_CFG_ID_LO", "Accepted configuration identity low word"),
                              ("SLA_CFG_ID_HI", "Accepted configuration identity high word")):
        groups["SLAAPS Configuration"][name] = dict(
            description=dict(short=description,
                             long="Supervisor-maintained acceptance marker. Export and restore with all SLAAPS "
                                  "parameters. Do not edit manually. A matching marker permits revalidation "
                                  "after disarmed restart, never arming or output permission."),
            type="int32", min=-2147483648, max=2147483647, default=0)
    groups["SLAAPS Commissioning"] = {
        "SLA_BENCH_EN": dict(
            description=dict(short="Deliberately enable commissioning",
                             long="Select SLAAPS Bench while disarmed, with an applied commissioning profile, "
                                  "valid routing and centered selected input. Set to 1 only after readiness. "
                                  "Set to 0 to stop. The supervisor resets this command on startup, exit or fault; "
                                  "restoring parameters never enables outputs. Servo targets require PX4 prearm permission "
                                  "and keep EDF inhibited. For EDF target 5, select/apply kind 2 disarmed, "
                                  "observe low throttle/readiness, enable, then deliberately arm through Commander. "
                                  "Controls and original duration remain bound. Disarm, kill or loss stops EDF. "
                                  "This command never grants flight admission."),
            type="enum", values={0: "Stop / disabled", 1: "Enable commissioning session"}, default=0),
        "SLA_BENCH_CH": dict(
            description=dict(short="Commissioning target",
                             long="Choose before enabling. Combined uses the applied body-axis bench mix. "
                                  "An individual canard uses the configured logical roll input, mapping and reversal; "
                                  "the other canards target physical neutral. All use the shared calibrated travel "
                                  "and slew limits. EDF target 5 parks every canard at physical zero and requires "
                                  "the accepted EDF commissioning profile, positive SLA_EDF_BMAX, low-throttle "
                                  "entry and deliberate armed permission. Changing this selection stops an active session."),
            type="enum", values={0: "Combined response", 1: "Canard 1", 2: "Canard 2",
                                 3: "Canard 3", 4: "Canard 4", 5: "EDF (deliberately armed)"}, default=0),
        "SLA_BENCH_SEC": dict(
            description=dict(short="Maximum commissioning session duration",
                             long="Set a finite duration before deliberately enabling commissioning. "
                                  "Zero denies enable. Expiry stops the session and requires deliberate re-entry. "
                                  "The 60 second software ceiling is not measured hardware qualification. "
                                  "Input freshness, command leases and stop/kill checks remain independently enforced."),
            type="int32", min=0, max=60, default=0, unit="s"),
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
    print(f"SLAAPS metadata: {len(list(definitions()))} vehicle parameters plus 2 apply controls, 4 acceptance markers and 3 commissioning controls")


if __name__ == "__main__":
    main()
