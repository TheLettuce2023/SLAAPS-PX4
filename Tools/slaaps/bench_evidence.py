#!/usr/bin/env python3
"""Summarize SLAAPS bench ulogs as evidence for the flight-test plan gates (docs/SLAAPS_FLIGHT_TEST_PLAN.md).

G0: armed minutes per SLAAPS mode, every "control lost" event with its latched fault record, entry rejections and bank
faults, and the measured activation reply time of each bank. G2: every recovery state change with what preceded it
(pilot input age, mode and arming changes) and how the EDF output fell. Also the logged pilot input and airspeed rates.

Observation only: it reports what the log shows and never grants a gate. Requires pyulog (pip install pyulog).
Example: python Tools/slaaps/bench_evidence.py "test_data/Logs/EDF bench testing" --g0-minutes 30
"""

import argparse
import re
import sys
from pathlib import Path

try:
    from pyulog import ULog
except ImportError:
    sys.exit("pyulog is required: pip install pyulog")

ARMED = 2
RECOVERY = {0: "none", 1: "D1 hold", 2: "D3 ramp off"}
FAULT_SITE = {0: "none", 1: "core worker fault after a handled frame", 2: "permission or health loss",
              3: "mode, power or condition loss"}
FAULT_REASON = {0: "", 1: "mode left", 2: "power lost", 3: "condition lost"}
ACTIVATION = {0: "inactive", 1: "waiting", 2: "ready", 3: "consumed", 4: "rejected"}
REPLY = re.compile(r"activation (flight|bench) status=(\d+) reply main=(\d+) aux=(\d+) us banks=(\d+) received=(\d+) "
                   r"limit=(\d+) us")
NOTABLE = re.compile(r"control lost|entry rejected|bank \d fault|fault detail|Disarmed by|Armed by|recovery|launch airspeed"
                     r"|pilot input|Failsafe",
                     re.IGNORECASE)


def topic(ulog, name, multi=0):
    for data in ulog.data_list:
        if data.name == name and data.multi_id == multi:
            return data.data
    return None


def seconds(ulog, timestamp):
    return (int(timestamp) - ulog.start_timestamp) / 1e6


def intervals(ulog, modes):
    """Armed intervals per navigation state, from vehicle_status."""
    status = topic(ulog, "vehicle_status")
    if status is None:
        return []
    result, start, nav = [], None, None
    for t, arming, state in zip(status["timestamp"], status["arming_state"], status["nav_state"]):
        armed = arming == ARMED
        if start is not None and (not armed or state != nav):
            result.append((start, seconds(ulog, t), nav))
            start = None
        if armed and start is None:
            start, nav = seconds(ulog, t), state
    if start is not None:
        result.append((start, seconds(ulog, status["timestamp"][-1]), nav))
    return [(a, b, modes.get(int(n), f"nav {int(n)}")) for a, b, n in result]


def rate(ulog, name, multi=0):
    data = topic(ulog, name, multi)
    if data is None or len(data["timestamp"]) < 2:
        return None
    t = data["timestamp"].astype("int64")
    span = (t[-1] - t[0]) / 1e6
    # Gaps between distinct samples: a republished stale value is not a new sample.
    sampled = sorted(set(data.get("timestamp_sample", data["timestamp"]).astype("int64").tolist()))
    gaps = [(b - a) / 1e3 for a, b in zip(sampled, sampled[1:])] or [0.0]
    return len(t) / span if span > 0 else 0.0, float(max(gaps))


def report(path, args, totals):
    ulog = ULog(str(path))
    modes = {args.manual_nav: "Manual", args.acceleration_nav: "Acceleration", args.bench_nav: "Bench"}
    print(f"\n== {path.name}  ({(ulog.last_timestamp - ulog.start_timestamp) / 1e6:.1f} s)")

    armed = intervals(ulog, modes)
    for start, end, mode in armed:
        print(f"  armed {mode:12s} {start:8.2f}s to {end:8.2f}s  ({end - start:6.1f} s)")
        totals["armed_s"][mode] = totals["armed_s"].get(mode, 0.0) + (end - start)
    if not armed:
        print("  never armed")

    flight = topic(ulog, "slaaps_flight_status")
    recorded = flight is not None and "fault_count" in flight
    if recorded:
        # Only logs that carry the fault record can show zero events; other armed minutes do not count toward G0.
        totals["g0_s"] += sum(end - start for start, end, mode in armed if mode == "Manual")
    if flight is not None and "fault_count" not in flight:
        print("  slaaps_flight_status predates the fault record (image before 73a739b5de): no control-lost or recovery report")
        flight = None
    if flight is not None:
        seen = 0
        for i, count in enumerate(flight["fault_count"]):
            if count > seen:
                site = int(flight["fault_site"][i])
                reason = FAULT_REASON.get(int(flight["fault_reason"][i]), "")
                print(f"  CONTROL LOST #{int(count)} at {seconds(ulog, flight['fault_time'][i]):8.2f}s: site {site} "
                      f"({FAULT_SITE.get(site, '?')}{', ' + reason if reason else ''}), worker status "
                      f"{int(flight['fault_worker_status'][i])}, core line {int(flight['fault_core_line'][i])}, transport line "
                      f"{int(flight['fault_transport_line'][i])}, schedule {int(flight['fault_schedule_status'][i])}, "
                      f"output margin {int(flight['fault_output_margin_us'][i])} us, permission margin "
                      f"{int(flight['fault_permission_margin_us'][i])} us, state age {int(flight['fault_state_age_us'][i])} us, "
                      f"pilot age {int(flight['fault_pilot_age_us'][i])} us")
                totals["control_lost"] += int(count) - seen
                seen = int(count)

        previous = 0
        manual = topic(ulog, "manual_control_setpoint")
        status = topic(ulog, "vehicle_status")
        edf = topic(ulog, "actuator_outputs", args.edf_instance)
        for i, state in enumerate(flight["recovery_state"]):
            state = int(state)
            if state == previous:
                continue
            t_us = int(flight["timestamp"][i])
            t = seconds(ulog, t_us)
            line = f"  recovery {RECOVERY.get(previous, previous)} -> {RECOVERY.get(state, state)} at {t:8.2f}s"
            if manual is not None:
                # Commander republishes the last pilot value until it declares the loss, so age is taken from the
                # sample time, not the publication time.
                published = manual["timestamp"] <= t_us
                sampled = manual.get("timestamp_sample", manual["timestamp"])[published]
                if len(sampled):
                    line += f"; last pilot sample {(t_us - int(sampled.max())) / 1e3:.0f} ms before"
            if status is not None:
                window = (status["timestamp"] > t_us - 2_000_000) & (status["timestamp"] <= t_us)
                for j in [k for k in range(len(window)) if window[k]]:
                    if j and (status["nav_state"][j] != status["nav_state"][j - 1]
                              or status["arming_state"][j] != status["arming_state"][j - 1]):
                        line += (f"; {(t_us - int(status['timestamp'][j])) / 1e3:.0f} ms before: nav "
                                 f"{int(status['nav_state'][j - 1])}->{int(status['nav_state'][j])}, arming "
                                 f"{int(status['arming_state'][j - 1])}->{int(status['arming_state'][j])}")
            print(line)
            if edf is not None and state:
                after = edf["timestamp"] >= t_us
                values = edf[f"output[{args.edf_channel}]"][after]
                times = edf["timestamp"][after]
                if len(values):
                    stop = [k for k in range(len(values)) if values[k] <= args.edf_stop_us]
                    reached = f"reached {args.edf_stop_us} us after {(int(times[stop[0]]) - t_us) / 1e3:.0f} ms" if stop \
                        else "did not reach the stop pulse in this log"
                    print(f"      EDF output {values[0]:.0f} us at the change, {reached}")
            previous = state

    for message in ulog.logged_messages:
        text = message.message.strip()
        t = seconds(ulog, message.timestamp)
        match = REPLY.search(text)
        if match:
            kind, status, main, aux, banks, received, limit = match.groups()
            print(f"  activation {kind} at {t:8.2f}s: {ACTIVATION.get(int(status), status)}, MAIN {main} us, AUX {aux} us, "
                  f"limit {limit} us, banks {banks}, received {received}")
            totals["replies"].setdefault(kind, []).append((int(main), int(aux), int(limit), int(banks)))
        elif NOTABLE.search(text):
            print(f"  log {t:8.2f}s {message.log_level_str()}: {text}")

    for name, label in (("manual_control_setpoint", "pilot input"), ("airspeed", "airspeed")):
        measured = rate(ulog, name)
        if measured:
            print(f"  {label}: logged {measured[0]:.1f} Hz, largest gap {measured[1]:.0f} ms "
                  f"(a rate at the logger's interval is the logger's cap, not the source's)")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("paths", nargs="+", type=Path, help="ulog files or directories of them")
    parser.add_argument("--g0-minutes", type=float, default=30.0, help="armed Manual minutes the G0 count needs (lead)")
    parser.add_argument("--manual-nav", type=int, default=23, help="nav_state of SLAAPS Manual (slaaps status: modes)")
    parser.add_argument("--acceleration-nav", type=int, default=24)
    parser.add_argument("--bench-nav", type=int, default=25)
    parser.add_argument("--edf-instance", type=int, default=1, help="actuator_outputs instance of the EDF bank (AUX)")
    parser.add_argument("--edf-channel", type=int, default=0, help="EDF channel in that instance (AUX1 = 0)")
    parser.add_argument("--edf-stop-us", type=float, default=1000.0, help="EDF stop pulse (PWM_AUX_DIS1)")
    args = parser.parse_args()

    files = []
    for path in args.paths:
        files += sorted(path.glob("*.ulg")) if path.is_dir() else [path]
    if not files:
        sys.exit("no .ulg files found")

    totals = {"armed_s": {}, "g0_s": 0.0, "control_lost": 0, "replies": {}}
    for path in files:
        report(path, args, totals)

    manual_minutes = totals["g0_s"] / 60
    print("\n== Summary (observation only; the lead closes gates)")
    for mode, armed_s in sorted(totals["armed_s"].items()):
        print(f"  armed {mode}: {armed_s / 60:.1f} min")
    print(f"  G0 control lost: {totals['control_lost']} event(s) over {manual_minutes:.1f} armed Manual minutes with the fault record "
          f"(needs 0 over {args.g0_minutes:g}): {'MET' if totals['control_lost'] == 0 and manual_minutes >= args.g0_minutes else 'NOT MET'}")
    for kind, replies in sorted(totals["replies"].items()):
        mains = [r[0] for r in replies if r[3] & 1]
        auxs = [r[1] for r in replies if r[3] & 2]
        limit = replies[-1][2]
        print(f"  {kind} activation replies over {len(replies)} attempt(s): MAIN max {max(mains) if mains else '-'} us, "
              f"AUX max {max(auxs) if auxs else '-'} us, limit {limit} us")


if __name__ == "__main__":
    main()
