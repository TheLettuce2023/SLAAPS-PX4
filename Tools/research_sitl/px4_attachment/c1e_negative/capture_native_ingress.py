#!/usr/bin/env python3
"""Two separately checked disarmed C1E runs from an explicitly reviewed build.

This is separate from the compile-only builder. Importing or checking its gate
does not start PX4. Execution requires --execute-reviewed and a retained,
hash-bound technical review record; no review record is created here.
"""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import platform
import secrets
import signal
import socket
import subprocess
import tempfile
import time

import check_native_ingress as checker
import send_w1_request as sender


HERE = Path(__file__).resolve().parent
REVIEW_SCHEMA = "research-c1e-native-technical-review/v1"
IDS = ("permitted_source", "controller_instance_id", "owner_lifetime_id",
       "native_run_id", "producer_lifetime_id", "source_clock_id", "transport_binding_id")
ENV_NAMES = ("SOURCE_ID", "CONTROLLER_ID", "OWNER_ID", "RUN_ID", "PRODUCER_ID",
             "CLOCK_ID", "TRANSPORT_ID")


def reviewed_build(build_root: Path, review_path: Path):
    build_root = build_root.resolve()
    checker.require(build_root.parent == Path("/var/tmp") and build_root.name.startswith("c1e_px4_"),
                    "Only an isolated C1E build area may be released")
    review_path = review_path.resolve()
    checker.require(review_path.is_relative_to(build_root) and review_path.is_file()
                    and not review_path.is_symlink(), "Technical review must be retained in build area")
    review = checker.decode(review_path.read_bytes())
    checker.fields(review, "schema decision reviewer scope build_manifest_sha256 source_registry_sha256 "
                   "stack_registry_sha256 generated_registry_sha256 executable_sha256 startup_sha256",
                   "technical review")
    checker.equal(review["schema"], REVIEW_SCHEMA, "technical review schema")
    checker.equal(review["decision"], "approved_for_disarmed_runs", "review decision")
    checker.equal(review["scope"], "negative_diagnostic_disarmed_output_disabled", "review scope")
    checker.require(type(review["reviewer"]) is str and bool(review["reviewer"].strip()),
                    "Missing accountable technical reviewer")
    manifest = checker.checked_build(build_root, review["build_manifest_sha256"])
    for name, value in (("source_registry_sha256", manifest["source_registry_sha256"]),
                        ("stack_registry_sha256", manifest["stack_registry_sha256"]),
                        ("generated_registry_sha256", manifest["generator"]["registry_sha256"]),
                        ("executable_sha256", manifest["executable_sha256"]),
                        ("startup_sha256", checker.sha256_file(HERE / "startup.sh"))):
        checker.equal(checker.hex_hash(review[name], "review." + name), value, "review." + name)
    source_spec = importlib.util.spec_from_file_location("c1e_compile_source_inventory",
                                                       HERE / "run_native_ingress.py")
    source_module = importlib.util.module_from_spec(source_spec)
    source_spec.loader.exec_module(source_module)
    checker.equal(source_module.closed.source_hashes(),
                  checker.decode((build_root / "source_hashes.json").read_bytes()),
                  "Current full source inventory versus reviewed build")
    checker.equal(source_module.check_config(build_root / "build", build_root / "source"),
                  manifest["configuration_audit"], "reviewed output-free compile audit")
    checker.equal(checker.sha256_file(HERE / "commander_negative_ingress.patch"),
                  manifest["overlay"]["patch_sha256"], "reviewed private overlay patch")
    for relative, hashes in manifest["overlay"]["files"].items():
        checker.equal(checker.sha256_file(build_root / "source" / relative),
                      hashes["derived_sha256"], "reviewed private overlay derived file")
    stack = checker.decode((build_root / "stack_files.json").read_bytes())
    checker.require(type(stack) is dict and bool(stack), "Missing reviewed stack file inventory")
    for relative, digest in stack.items():
        path = (build_root / "build" / relative).resolve()
        checker.require(path.is_relative_to(build_root / "build"), "Stack file escaped build")
        checker.equal(checker.sha256_file(path), checker.hex_hash(digest, "stack hash"),
                      "Reviewed stack file changed")
    checker.equal(checker.sha256_file(build_root / "source_hashes.json"),
                  review["source_registry_sha256"], "reviewed source registry bytes")
    checker.equal(checker.sha256_file(build_root / "stack_files.json"),
                  review["stack_registry_sha256"], "reviewed stack registry bytes")
    checker.equal(checker.sha256_file(build_root / "generated_files.json"),
                  review["generated_registry_sha256"], "reviewed generated registry bytes")
    generated_registry = checker.decode((build_root / "generated_files.json").read_bytes())
    checker.require(type(generated_registry) is dict and type(generated_registry.get("files")) is dict
                    and bool(generated_registry["files"]), "Missing generated file inventory")
    for relative, digest in generated_registry["files"].items():
        path = (build_root / "generated" / relative).resolve()
        checker.require(path.is_relative_to(build_root / "generated"), "Generated path escaped build")
        checker.equal(checker.sha256_file(path), checker.hex_hash(digest, "generated hash"),
                      "Reviewed generated binding changed")
    return manifest, review


def _free_port(excluded):
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as endpoint:
        endpoint.bind(("127.0.0.1", 0))
        port = endpoint.getsockname()[1]
    checker.require(port not in excluded, "Fresh endpoint port collided")
    excluded.add(port)
    return port


def _bindings(used_ids, used_ports):
    bindings = {}
    for name in IDS:
        identity = secrets.randbits(64)
        while identity == 0 or identity in used_ids:
            identity = secrets.randbits(64)
        used_ids.add(identity)
        bindings[name] = str(identity)
    bindings["channel"] = 1
    bindings["local_port"] = _free_port(used_ports)
    bindings["peer_port"] = _free_port(used_ports)
    return bindings


def _udp_socket_in_pid(pid: int, port: int) -> bool:
    """Check actual IPv4 loopback binding and ownership by the launched process."""
    table = Path(f"/proc/{pid}/net/udp")
    fds = Path(f"/proc/{pid}/fd")
    try:
        inodes = set()
        for fd in fds.iterdir():
            try:
                link = os.readlink(fd)
            except OSError:  # The process may close an unrelated descriptor.
                continue
            if link.startswith("socket:[") and link.endswith("]"):
                inodes.add(int(link[8:-1]))
        for row in table.read_text().splitlines()[1:]:
            columns = row.split()
            address, hex_port = columns[1].split(":")
            if address == "0100007F" and int(hex_port, 16) == port \
                    and int(columns[9]) in inodes:
                return True
    except (FileNotFoundError, PermissionError, OSError, ValueError, IndexError):
        return False
    return False


def wait_for_endpoint(child, port: int, timeout_seconds: float = 8.0):
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        checker.require(child.poll() is None, "PX4 exited before its bound diagnostic endpoint")
        if _udp_socket_in_pid(child.pid, port):
            return
        time.sleep(0.01)
    raise checker.EvidenceError("Reviewed diagnostic endpoint never bound to owned PX4 PID")


def wait_for_checkpoint(child, stdout_path: Path, timeout_seconds: float = 8.0):
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        checker.require(child.poll() is None, "PX4 exited before source checkpoint")
        point = checker.read_checkpoint(stdout_path.read_bytes())
        if point is not None:
            return point
        time.sleep(0.01)
    raise checker.EvidenceError("Source-bound disarmed checkpoint was not published")


def _key_file(runtime: Path, key: bytes):
    path = runtime / "one-time-signing-key"
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0)
    fd = os.open(path, flags, 0o600)
    try:
        with os.fdopen(fd, "wb") as stream:
            stream.write(key)
            stream.flush()
            os.fsync(stream.fileno())
    except BaseException:
        path.unlink(missing_ok=True)
        raise
    return path


def _environment(bindings, key_path):
    env = {name: value for name, value in os.environ.items()
           if not name.startswith(("PX4_", "SIM_", "GZ_", "C1A_", "C1C_", "C1D_", "C1E_"))
           and name not in {"replay", "replay_mode"}}
    for variable, identity in zip(ENV_NAMES, IDS):
        env["C1E_" + variable] = bindings[identity]
    env.update(C1E_LOCAL_PORT=str(bindings["local_port"]), C1E_PEER_PORT=str(bindings["peer_port"]),
               C1E_KEY_FILE=str(key_path), C1E_WINDOW_US="2000000")
    return env


def _save(path, value):
    path.write_text(json.dumps(value, sort_keys=True, indent=2) + "\n", encoding="utf-8")


def capture_once(build_root, manifest, review, campaign, index, used_ids, used_ports,
                 used_instances):
    runtime = campaign / f"run-{index}"
    runtime.mkdir(mode=0o700)
    (runtime / "startup.sh").write_bytes((HERE / "startup.sh").read_bytes())
    bindings = _bindings(used_ids, used_ports)
    generated = Path(manifest["generator"]["python_output"])
    generated_hash = checker.decode((build_root / "generated_files.json").read_bytes())["files"]["python/diagnostic.py"]
    module = sender.generated_module(generated, generated_hash)
    key = secrets.token_bytes(32)
    instance = secrets.randbelow(50000) + 10000
    while instance in used_instances:
        instance = secrets.randbelow(50000) + 10000
    used_instances.add(instance)
    key_path = runtime / "one-time-signing-key"
    command = [str(build_root / "build/bin/px4"), "-i", str(instance), "-w", str(runtime),
               "-s", str(runtime / "startup.sh")]
    run = {"schema": checker.RUN_SCHEMA, "build_root": str(build_root), "command": command,
           "instance": instance, "runtime": str(runtime), "bindings": bindings,
           "scope": "negative_diagnostic_disarmed_output_disabled", "status": "incomplete",
           "pid": 0, "start_monotonic_ns": 0, "end_monotonic_ns": 0,
           "forced_shutdown": False, "exit_code": None,
           "key_fingerprint_sha256": hashlib.sha256(key).hexdigest(), "artifact_sha256": {},
           "build_manifest_sha256": review["build_manifest_sha256"],
           "source_registry_sha256": review["source_registry_sha256"],
           "stack_registry_sha256": review["stack_registry_sha256"],
           "generated_registry_sha256": review["generated_registry_sha256"],
           "executable_sha256": review["executable_sha256"]}
    _save(runtime / "run.json", run)
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as endpoint:
        try:
            endpoint.bind(("127.0.0.1", bindings["peer_port"]))
            _key_file(runtime, key)
            with (runtime / "stdout.log").open("wb") as log:
                run["start_monotonic_ns"] = time.monotonic_ns()
                child = subprocess.Popen(command, cwd=runtime, env=_environment(bindings, key_path),
                                         stdin=subprocess.PIPE, stdout=log, stderr=subprocess.STDOUT,
                                         start_new_session=True)
                run["pid"] = child.pid
                try:
                    wait_for_endpoint(child, bindings["local_port"])
                    point = wait_for_checkpoint(child, runtime / "stdout.log")
                    request, status_bytes, status = sender.send_one(
                        module, key, bindings, endpoint, int(point["epoch"]),
                        on_request_sent=(runtime / "request.bin").write_bytes,
                        on_status_received=(runtime / "status.bin").write_bytes)
                    parsed = dict(status)
                    parsed["reserved"] = list(parsed["reserved"])
                    _save(runtime / "sender.json", {
                        "request_sent_count": 1, "status_received_count": 1,
                        "signature_verified": True,
                        "request_sha256": hashlib.sha256(request).hexdigest(),
                        "status_sha256": hashlib.sha256(status_bytes).hexdigest(),
                        "status": parsed})
                    child.communicate(b"shutdown\n", timeout=25)
                    checker.require(child.returncode == 0, "PX4 did not exit normally")
                    run["status"] = "captured_unscored"
                except BaseException:
                    run["forced_shutdown"] = True
                    if child.poll() is None:
                        os.killpg(child.pid, signal.SIGTERM)
                        try:
                            child.communicate(timeout=5)
                        except subprocess.TimeoutExpired:
                            os.killpg(child.pid, signal.SIGKILL)
                            child.communicate(timeout=5)
                    raise
                finally:
                    run["exit_code"] = child.returncode
                    run["end_monotonic_ns"] = time.monotonic_ns()
        finally:
            key_path.unlink(missing_ok=True)
            run["artifact_sha256"] = {
                name: checker.sha256_file(runtime / name) for name in checker.ARTIFACTS
                if (runtime / name).is_file()}
            _save(runtime / "run.json", run)
    checker.require(not key_path.exists(), "One-time key file survived endpoint bootstrap")
    result = checker.check_run(runtime, bindings, build_root, review["build_manifest_sha256"])
    _save(runtime / "checked.json", result)
    return runtime, bindings, result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-root", type=Path, required=True)
    parser.add_argument("--review", type=Path, required=True)
    parser.add_argument("--execute-reviewed", action="store_true",
                        help="explicitly release two separate disarmed native process runs")
    args = parser.parse_args()
    checker.require(platform.system() == "Linux" and platform.machine() == "x86_64",
                    "C1E capture requires isolated x86_64 Linux/Ubuntu")
    build_root = args.build_root.resolve()
    manifest, review = reviewed_build(build_root, args.review)
    if not args.execute_reviewed:
        print(json.dumps({"result": "REVIEWED_NOT_RUN", "build_root": str(build_root)}))
        return 0
    campaign = Path(tempfile.mkdtemp(prefix="c1e_capture_", dir="/var/tmp"))
    used_ids, used_ports, used_instances = set(), set(), set()
    records = [capture_once(build_root, manifest, review, campaign, index, used_ids,
                            used_ports, used_instances)
               for index in (1, 2)]
    result = checker.check_campaign(records[0][0], records[0][1], records[1][0],
                                    records[1][1], build_root, review["build_manifest_sha256"])
    _save(campaign / "checked-campaign.json", result)
    print(json.dumps({"result": result["result"], "campaign": str(campaign)}))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (checker.EvidenceError, sender.SenderError, OSError, RuntimeError, ValueError) as error:
        print(json.dumps({"result": "FAIL", "error": str(error)}))
        raise SystemExit(1) from error
