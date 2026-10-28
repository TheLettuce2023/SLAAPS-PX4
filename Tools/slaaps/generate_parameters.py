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
        if item["name"] == "SLA_DATA_STATE":
            parameter.update(type="enum", values={0: "Incomplete", 1: "Calibrated", 2: "Qualified"})
        elif item["name"] == "SLA_IN_SOURCE":
            parameter.update(type="enum", values={0: "Unconfigured", 1: "RC only", 2: "Pinned MAVLink only"})
        elif item["name"] in ("SLA_R_INPUT", "SLA_P_INPUT", "SLA_Y_INPUT"):
            parameter.update(type="enum", values={-1: "Unconfigured", 0: "Roll", 1: "Pitch", 2: "Yaw"})
        elif item["name"] == "SLA_FLT_STAGE":
            parameter.update(type="enum", values={0: "Off", 1: "Captive", 2: "Launcher"})
        elif item["name"] == "SLA_LAUNCH_AS_I":
            parameter.update(type="enum", values={-1: "Unconfigured", **{i: f"Instance {i}" for i in range(4)}})
        elif item["name"] == "SLA_SCH_EN":
            parameter.update(type="enum", values={0: "Disabled", 1: "Empirical 2x2 table"})
        elif item["name"] == "SLA_WR_EN":
            parameter.update(type="enum", values={0: "Disabled", 1: "Declared measured wrench model"})
        elif item["name"] == "SLA_FB_EN":
            parameter.update(type="enum", values={0: "Disabled", 1: "Configured acceleration feedback"})
        elif item["name"] == "SLA_ACC_CTL_EN":
            parameter.update(type="enum", values={0: "Disabled", 1: "Configured numerical controller"})
        elif item["name"] == "SLA_CMP_EN":
            parameter.update(type="enum", values={0: "Disabled", 1: "Observe configured companion"})
        elif item["name"] == "SLA_CMP_ADMIT":
            parameter.update(type="enum", values={0: "Receipt only", 1: "Configured request validation"})
        elif item["name"] == "SLA_CMP_DISARM":
            parameter.update(type="enum", values={0: "Unconfigured", 1: "Reject requests", 2: "Validate without control eligibility"})
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
        elif item["name"] == "SLA_MAN_WAIT":
            parameter["description"]["long"] = (
                "Explicit finite window for a deliberate disarmed Manual entry to await live readiness "
                "and an operator arming action. Zero leaves Manual readiness unconfigured. The entry window "
                "is not renewed by healthy updates, stick activity or parameter notifications. It does not "
                "extend bank preparation, activation-reply, first-command or output deadlines. Configure "
                "while disabled and deliberately apply the complete profile. This software wait bound "
                "does not grant flight permission or qualify vehicle timing, health or measured conditions."
            )
        elif item["name"] == "SLA_EDF_BMAX":
            parameter["description"]["long"] = (
                "Explicit normalized EDF commissioning ceiling; zero denies commissioning. "
                "Choose a hardware-appropriate limit while disarmed and apply the complete EDF commissioning "
                "profile. This software range is not a qualified hardware ceiling. Select Bench/EDF, "
                "observe low throttle and readiness, deliberately enable, then deliberately arm through Commander. "
                "Profile, limit and duration remain bound; stop, disarm, kill or input/permission loss ends the session. "
                "This is ground commissioning, never flight admission.")
        if item["name"].startswith("SLA_DATA_"):
            parameter["description"]["long"] = (
                "Operator-declared provenance for an external calibration or qualification record. "
                "SLA_DATA_VER identifies the SLAAPS profile schema, not an external artifact format. "
                "SLA_DATA_REC identifies the external "
                "record and SLA_DATA_REV its revision. Incomplete permits staged commissioning data. "
                "Calibrated or Qualified requires a positive record ID and revision. The firmware does not "
                "verify that the record exists, authenticate it, or independently verify the operator's "
                "assessment. Selecting Qualified never grants flight, measured-condition, session or "
                "actuator permission, and does not replace required runtime health and ownership checks. "
                "Configure while disabled and deliberately apply the coherent complete profile."
            )
        if item["field"].startswith("companion."):
            parameter["description"]["long"] = (
                "Companion source and optional request-admission policy. Configure and apply while disarmed. "
                "The exact MAVLink link, signed header system/component and positive 31-bit payload source ID "
                "must match. SLA_CMP_AGE bounds local receipt, independently of raw request validity and conservative "
                "command age. Admission is disabled by default; enabled admission requires explicit acceleration, "
                "timing and clock bounds, disarmed policy and shared SLA_QUAT_TOL. Acceleration is inertial at CG "
                "in body FRD, never specific force or an actuator demand. Zero required bounds are unconfigured. "
                "Use separate deliberate SLA_CMP_ACT actions for preparation, grant, revoke and latch clear. "
                "Saved configuration, authentication and valid numbers establish neither a trusted session/map "
                "nor a qualified clock, acceleration controller or flight/output permission."
            )
        if item["name"] in ("SLA_CMP_DRIFT", "SLA_CMP_STAMP_U", "SLA_CMP_RTT", "SLA_CMP_MAP_US", "SLA_CMP_CLOCK_U"):
            parameter["description"]["long"] += (
                " Relative drift bounds |d(validator time)/d(producer time)-1| in integer ppm. Timestamp uncertainty "
                "includes declared timestamp, quantization and translator errors. The local map must conservatively "
                "include measured round-trip uncertainty and drift over its entire frozen producer interval, "
                "within SLA_CMP_CLOCK_U. Stock TIMESYNC convergence alone is not this guarantee; these declared "
                "bounds need deployment evidence. No offset, session or map epoch is configured by a parameter."
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
        if (item["field"].startswith("wrench.") or ".force_derivative" in item["field"]
                or (item["field"].startswith("schedule.corner")
                    and any(key in item["field"] for key in ("trim_force", "trim_moment", "edf_force_per_thrust", "edf_moment_per_thrust")))):
            parameter["description"]["long"] += (
                " Optional measured physical affine wrench, declared explicitly by SLA_WR_EN. All vectors use "
                "body FRD; moments are about CG. Canard force derivatives are N/rad and reuse the existing signed "
                "rad/normalized deflection slopes and moment derivatives. Total nongravitational trim wrench is "
                "measured at the configured canard trims and SLA_TEST_THR for the fixed model, or at the shared "
                "EDF thrust curve value for that scheduled corner's EDF-command knot. Installed EDF force/N and "
                "moment/N include measured installation effects; no unit-vector normalization, lever arm, torque "
                "or wash is inferred. Individual measured coefficients may be zero. Interpolate complete affine "
                "intercepts and columns at the same filtered schedule coordinates; do not interpolate reference "
                "products. Positive force/moment residual tolerances are explicit physical acceptance bounds. "
                "Disabled or incomplete wrench data leaves acceleration conversion unavailable without removing "
                "Manual or Bench calibration. Declaration, parameter saving and numerical validity do not prove "
                "measurement authenticity, operating-envelope coverage or flight/output admission."
            )
        if item["field"].startswith("acceleration_control."):
            parameter["description"]["long"] = (
                "Numerical body-FRD inertial CG acceleration and absolute NED-from-body quaternion control. "
                "Explicit positive outer proportional gains and correction limits are required; zero is "
                "unconfigured. Rotational PID gains and the physical affine wrench use the same filtered "
                "empirical IAS/actual EDF-command schedule. Current canards and all six residuals are solved "
                "at the current owner command's calibrated thrust, not a future target or measured thrust. "
                "No spool, hover, entry-attitude hold, recovery or physical timing qualification is inferred. "
                "Incomplete configuration does not disable Manual or Bench. Configuration supplies no "
                "authenticated request, measured-condition, flight or actuator permission."
            )
        if item["field"].startswith("feedback."):
            parameter["description"]["long"] = (
                "Optional selected acceleration feedback at the CG. Configure and apply while disarmed. "
                "All enabled-policy bounds and gravity must be explicit and positive. Original acceleration "
                "sample age must exceed declared relative sensor/filter latency. Existing SLA_STATE_SKEW "
                "bounds combined raw timestamp skew plus this latency; no duplicate skew limit is supplied. "
                "The explicit stable-source settling interval must cover the declared latency and cannot "
                "renew any original sample or bias expiry. The 60-second settling range is a software bound, "
                "not a measured filter qualification. EKF2_IMU_POS_X/Y/Z already give CG-to-IMU body-FRD offsets "
                "and are captured atomically with this profile; SLAAPS does not subtract its separate CG "
                "coordinate or define duplicate position parameters. IMU_ACCEL_CUTOFF is captured native intent "
                "(zero disables its filter), not proof of the producer's actual applied cutoff. The consumer "
                "must check actual source/filter metadata and continuity. Disabled or incomplete feedback "
                "does not remove existing Manual or Bench model validity. Configuration, settling and "
                "numerical validity confer no flight admission or measured latency/vehicle qualification."
            )
        if item["name"] == "SLA_STATE_SKEW":
            parameter["description"]["long"] += (
                " Acceleration feedback reuses this bound for combined raw attitude/rate/acceleration "
                "timestamp skew plus the separately declared SLA_FB_LAT_US; the latency must leave positive "
                "raw-skew budget. Manual attitude/rate validation keeps its existing semantics."
            )
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
    groups["SLAAPS Companion Actions"] = {
        "SLA_CMP_ACT": dict(
            description=dict(short="Deliberate disarmed companion action",
                             long="Transient command outside the accepted vehicle profile. All actions require fresh "
                                  "actual disarmed authority. Prepare binds an already-staged authenticated SESSION "
                                  "candidate for bounded clock-map preparation and grants no session authority. "
                                  "Grant independently authorizes only that exact prepared session with a current "
                                  "fresh authenticated SESSION. Revoke closes the current trusted session; clear "
                                  "only clears a latch and never grants a session or acknowledgment. The supervisor "
                                  "resets this command on startup and after consumption, including rejection. "
                                  "Saved commands cannot restore authority. No action enables a flight controller "
                                  "or outputs; missing configuration, state, clock or bindings deny the action."),
            type="enum", values={0: "None", 1: "Prepare staged authenticated session", 2: "Grant exact prepared session",
                                 3: "Revoke session", 4: "Clear latched fault"}, default=0),
    }
    net_long = ("D6 operator reference only, outside the vehicle profile: no guidance, position control or geofence "
                "is implied. The ground-station location is QGC's own position received over MAVLink FOLLOW_TARGET "
                "(enable QGC Application Settings, Stream GCS Position). During a recovery the QGC alarm and "
                "slaaps_status report the vehicle's distance and bearing to the net.")
    groups["SLAAPS Recovery Net"] = {
        "SLA_NET_EN": dict(
            description=dict(short="Report the recovery net position", long=net_long),
            type="enum", values={0: "Disabled", 1: "Report net position"}, default=0),
        "SLA_NET_N": dict(
            description=dict(short="Recovery net north of the ground station", long=net_long),
            type="float", min=-5000.0, max=5000.0, default=0.0, unit="m", decimal=1),
        "SLA_NET_E": dict(
            description=dict(short="Recovery net east of the ground station", long=net_long),
            type="float", min=-5000.0, max=5000.0, default=0.0, unit="m", decimal=1),
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
    print(f"SLAAPS metadata: {len(list(definitions()))} vehicle parameters plus 2 apply controls, 4 acceptance markers, 3 commissioning controls, 1 companion action and 3 net references")


if __name__ == "__main__":
    main()
