#!/usr/bin/env python3
"""Capture one fresh, disarmed stock Cessna run in WSL; never arm the vehicle."""

import argparse
import errno
import os
from pathlib import Path
import re
import select
import shutil
import signal
import subprocess
import time

from acceptance import reference
from inspect_raw_ulog import digest, load_json, require
from prepare_campaign import mounted_source, now, source_inventory, write_new
from score_interval import REGISTRY_SHA256, STATE_RULES, registry


ANSI = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")


def processes():
    result = []
    for proc in Path("/proc").iterdir():
        if not proc.name.isdigit():
            continue
        try:
            comm = (proc / "comm").read_text().strip()
            argv = (proc / "cmdline").read_bytes().decode(errors="replace").split("\0")
            if comm != "px4" and not (comm.startswith("gz") or argv[0].startswith("gz sim ") or
                                      any(Path(arg).name == "gz" for arg in argv if arg)):
                continue
            stat = (proc / "stat").read_text().rsplit(") ", 1)[1].split()
            if stat[0] == "Z":
                continue
            result.append({"pid": int(proc.name), "ppid": int(stat[1]),
                           "session": int(stat[3]), "start_ticks": int(stat[19]),
                           "comm": comm, "argv": argv})
        except (FileNotFoundError, ProcessLookupError):
            pass
    return result


class Console:
    def __init__(self, command, cwd, environment, transcript):
        self.stream = transcript.open("x", encoding="utf-8")
        self.master, slave = os.openpty()
        self.process = subprocess.Popen(command, cwd=cwd, env=environment, stdin=slave,
                                        stdout=slave, stderr=slave, start_new_session=True)
        os.close(slave)
        self.text = ""

    def drain(self, seconds):
        until = time.monotonic() + seconds
        while time.monotonic() < until:
            ready, _, _ = select.select([self.master], [], [], min(0.2, max(0, until - time.monotonic())))
            if ready:
                try:
                    block = os.read(self.master, 65536)
                except OSError as error:
                    if error.errno == errno.EIO:
                        break
                    raise
                if not block:
                    break
                text = ANSI.sub("", block.decode(errors="replace")).replace("\r", "\n")
                self.text += text
                self.stream.write(text)
                self.stream.flush()

    def wait_prompt(self, offset=0, timeout=180, command=None):
        until = time.monotonic() + timeout
        while time.monotonic() < until:
            self.drain(0.2)
            tail = self.text[offset:]
            if offset == 0 and "Startup script returned successfully" in tail:
                return tail
            if command is not None:
                echoed = tail.find("pxh> " + command + "\n")
                if echoed >= 0 and re.search(r"(?:^|\n)pxh> ", tail[echoed + len(command) + 6:]):
                    return tail
            if command is None and "\n" in tail and tail.rstrip().endswith("pxh>"):
                return tail
            require(self.process.poll() is None, "PX4 launch exited before prompt")
        raise TimeoutError("PX4 prompt timeout")

    def command(self, command):
        self.drain(0.1)
        offset = len(self.text)
        os.write(self.master, (command + "\n").encode())
        return self.wait_prompt(offset, 20, command)

    def note(self, text):
        self.text += text + "\n"
        self.stream.write(text + "\n")
        self.stream.flush()


def fields(text):
    return dict(re.findall(r"^\s+([a-zA-Z0-9_]+):\s+(\S+)", text, re.MULTILINE))


def valid_ready(values):
    for topic, rules in STATE_RULES.items():
        for key, expected in rules.items():
            actual = values.get(topic, {}).get(key)
            if actual not in (str(expected), "True" if expected == 1 else "False"):
                return False
    return (values["vehicle_status"].get("nav_state") == "4" and
            values["vehicle_status"].get("gcs_connection_lost") == "False" and
            values.get("telemetry_status", {}).get("heartbeat_type_gcs") == "True" and
            int(values.get("sensor_gps", {}).get("fix_type", "0")) >= 3)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("root", "source", "mount", "build"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--mode", choices=("gui", "headless"), required=True)
    parser.add_argument("--run-id", required=True)
    parser.add_argument("--operator", required=True)
    parser.add_argument("--linux-user", default="kspry04")
    args = parser.parse_args()
    root = args.root.resolve()
    require(re.fullmatch(r"[a-zA-Z0-9_-]+", args.run_id), "invalid run ID")
    run = root / args.run_id
    run.mkdir(exist_ok=False)
    limits = registry()
    initial = processes()
    require(not initial, "existing PX4/Gazebo processes; refusing launch")
    print("Verifying source against archived build inputs", flush=True)
    require(source_inventory(args.source) == load_json(root / "source-index.json"),
            "source differs from archived build inputs")
    build = load_json(root / "build.json")
    binary = args.build / "px4_sitl_default/bin/px4"
    require(digest(binary) == build["executable"]["sha256"], "executable changed since build")
    rootfs = args.build / "px4_sitl_default/rootfs"
    before_logs = set((rootfs / "fs/log").rglob("*.ulg"))
    for name in ("parameters.bson", "parameters_backup.bson"):
        shutil.copyfile(root / "parameters.bson", rootfs / "fs" / name)
    artifacts = {key: reference(root / filename, root) for key, filename in (
        ("source_index", "source-index.json"), ("source_archive", "source.tar.gz"),
        ("build", "build.json"), ("executable", "px4"), ("parameters", "parameters.bson"),
        ("registry", "registry.json"), ("approval", "approval.md"), ("signal_inventory", "signal-inventory.md"))}
    plan = {"schema": "stock-cessna-run-plan/v1", "run_id": args.run_id,
            "campaign_id": limits["campaign_id"], "created_utc": now(), "mode": args.mode,
            "operator": args.operator, "registry_sha256": REGISTRY_SHA256,
            "interval_rule": {"ready_offset_us": 10000000, "duration_us": 60000000},
            "telemetry_link_required": True, "headless_telemetry_policy": "same GCS heartbeat/RX criteria as GUI",
            "random_seed": {"value": None, "reason": "no seed override; exact determinism not claimed"},
            "artifacts": artifacts}
    write_new(run / "plan.json", plan)
    command = ["runuser", "-u", args.linux_user, "--", "make", "px4_sitl", "gz_rc_cessna"]
    environment = os.environ.copy()
    environment.pop("HEADLESS", None)
    if args.mode == "headless":
        environment["HEADLESS"] = "1"
    write_new(run / "launch.json", {"run_id": args.run_id, "utc": now(),
              "plan_sha256": digest(run / "plan.json"), "preexisting_px4_gz": initial,
              "parameter_sha256": digest(rootfs / "fs/parameters.bson"),
              "executable_sha256": digest(binary), "command": command,
              "environment": {key: environment.get(key) for key in ("DISPLAY", "WAYLAND_DISPLAY", "XDG_RUNTIME_DIR", "HEADLESS")}})
    owned = {}
    console = None
    normal, error = False, None
    with mounted_source(args.source, args.mount, args.build):
        console = Console(command, args.mount, environment, run / "transcript.txt")

        def remember():
            for item in processes():
                require(item["session"] == console.process.pid, "unowned simulator process detected")
                owned[(item["pid"], item["start_ticks"])] = item

        def clock():
            data = fields(console.command("listener vehicle_status -i 0"))
            remember()
            return {"px4_us": int(data["timestamp"]), "utc": now()}

        try:
            console.wait_prompt()
            remember()
            write_new(run / "owned-processes.json", {"run_id": args.run_id, "processes": list(owned.values())})
            print("LIVE " + args.run_id, flush=True)
            env_outputs = []
            for cmd in (["uname", "-a"], ["ip", "-4", "route"], ["ss", "-u", "-a", "-n"],
                        ["cmake", "--version"], ["ninja", "--version"], ["gz", "sim", "--versions"]):
                text = subprocess.check_output(cmd, text=True, stderr=subprocess.STDOUT, timeout=20)
                env_outputs.append(text)
                console.note("$ " + " ".join(cmd) + "\n" + text)
            commands = ("commander status", "commander check", "gz_bridge status", "control_allocator status",
                        "mavlink status", "sensor_airspeed_sim status")
            status = {cmd: console.command(cmd) for cmd in commands}
            gz_topics = subprocess.check_output(["runuser", "-u", args.linux_user, "--", "env",
                                                "GZ_IP=127.0.0.1", "gz", "topic", "-l"], text=True, timeout=20)
            console.note("$ gz topic -l (simulator user and loopback transport)\n" + gz_topics)
            require("/world/default/clock" in gz_topics and "/world/default/pose/info" in gz_topics,
                    "Gazebo clock/pose topic missing")
            ready_topics = list(STATE_RULES) + ["sensor_gps", "telemetry_status"]
            deadline = time.monotonic() + 180
            while True:
                values = {topic: fields(console.command("listener " + topic + " -i 0")) for topic in ready_topics}
                if valid_ready(values):
                    break
                require(time.monotonic() < deadline, "ready checkpoint conditions never satisfied")
                console.drain(2)
            point = clock()
            write_new(run / "checkpoint.json", {"run_id": args.run_id, **point, "observed_fields": values})
            checks = {"run_id": args.run_id}
            for name, quote in (
                ("model_spawned", "world: default, model: rc_cessna_0"),
                ("bridge_clock_pose", "/world/default/clock"),
                ("synthetic_airspeed_source", next((line.strip() for line in status["sensor_airspeed_sim status"].splitlines()
                                                    if line.strip().lower() == "running"), "MISSING_RUNNING_MODULE")),
                ("disarmed_hold_ready", "navigation mode: Hold"),
                ("estimator_position_gnss_ready", "cs_gnss_pos: True"),
                ("actuator_channels", "Param prefix: SIM_GZ_EC"),
                ("environment_network", env_outputs[0].strip())):
                checks[name] = {"status": "pass" if quote in console.text else "incomplete", "quote": quote}
            write_new(run / "runtime_checks.json", checks)
            start, end = point["px4_us"] + 10000000, point["px4_us"] + 70000000
            bounds = {"run_id": args.run_id}
            previous = point
            print("READY " + str(point["px4_us"]) + " SCORE " + str(start) + ".." + str(end), flush=True)
            deadline = time.monotonic() + 600
            while previous["px4_us"] < end:
                console.drain(0.7)
                current = clock()
                for label, value in (("start", start), ("end", end)):
                    if label not in bounds and previous["px4_us"] <= value <= current["px4_us"]:
                        bounds[label] = [previous, current]
                previous = current
                require(time.monotonic() < deadline, "simulation clock did not complete scored interval")
            write_new(run / "clock_bounds.json", bounds)
            console.drain(3)
            print("SCORING_COMPLETE " + args.run_id, flush=True)
            if args.mode == "gui":
                print("WAITING_FOR_QGC_RECORD " + str(run / "qgc.json"), flush=True)
                deadline = time.monotonic() + 600
                while not (run / "qgc.json").exists() and time.monotonic() < deadline:
                    console.drain(1)
                    remember()
            console.command("commander status")
            console.command("mavlink status")
            remember()
            normal = True
        except Exception as caught:
            error = repr(caught)
            print("CAPTURE_ERROR " + error, flush=True)
        finally:
            shutdown_time = now()
            if console.process.poll() is None:
                os.write(console.master, b"shutdown\n")
                console.drain(3)
            # PID plus kernel start time and owned session prevent PID-reuse kills.
            for item in processes():
                key = (item["pid"], item["start_ticks"])
                if key in owned and item["comm"] != "px4":
                    os.kill(item["pid"], signal.SIGINT)
            console.process.wait(timeout=15)
            console.drain(0.2)
            remaining = [item for item in processes() if (item["pid"], item["start_ticks"]) in owned]
            write_new(run / "lifecycle.json", {"run_id": args.run_id, "shutdown_utc": shutdown_time,
                      "finished_utc": now(), "normal_shutdown": normal, "capture_error": error,
                      "wrapper_exit_code": console.process.returncode,
                      "owned_processes": list(owned.values()), "remaining_owned_processes": remaining})
            console.stream.close()
            os.close(console.master)
    new_logs = set((rootfs / "fs/log").rglob("*.ulg")) - before_logs
    require(len(new_logs) == 1, "expected exactly one new ULog")
    shutil.copyfile(new_logs.pop(), run / "log.ulg")
    source_unchanged = source_inventory(args.source) == load_json(root / "source-index.json")
    write_new(run / "source_verification.json", {"run_id": args.run_id,
              "source_index_sha256": digest(root / "source-index.json"),
              "verified_before_launch": True, "verified_after_shutdown": source_unchanged,
              "executable_unchanged": digest(binary) == build["executable"]["sha256"]})
    names = {"ulog": "log.ulg", "transcript": "transcript.txt", "launch": "launch.json",
             "checkpoint": "checkpoint.json", "lifecycle": "lifecycle.json", "clock_bounds": "clock_bounds.json",
             "runtime_checks": "runtime_checks.json", "qgc": "qgc.json",
             "source_verification": "source_verification.json"}
    write_new(run / "manifest.json", {"schema": "stock-cessna-run-evidence/v1",
              "plan": reference(run / "plan.json", root),
              "artifacts": {key: reference(run / name, root) for key, name in names.items() if (run / name).exists()}})
    print("CLOSED " + args.run_id, flush=True)


if __name__ == "__main__":
    main()
