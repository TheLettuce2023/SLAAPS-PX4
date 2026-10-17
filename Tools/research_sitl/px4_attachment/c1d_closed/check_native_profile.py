#!/usr/bin/env python3
"""Compile-only isolation probes using the ACTUAL PX4 native command/includes."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shlex
import subprocess


def probe_command(build):
    records = json.loads((build / "compile_commands.json").read_text())
    candidates = [item for item in records if item["file"].endswith("/research_acceleration/NativeClosedDiagnosticOwner.cpp")]
    if len(candidates) != 1:
        raise ValueError("Exactly one actual native owner compilation is required")
    item = candidates[0]
    tokens = shlex.split(item["command"])
    command, skip = [], False
    for token in tokens:
        if skip:
            skip = False
            continue
        if token in {"-o", "-MF", "-MT", "-MQ"}:
            skip = True
        elif token in {"-c", "-MD", "-MMD", "-MP", "-fstack-usage"} or token == item["file"]:
            continue
        else:
            command.append(token)
    if "-DRESEARCH_SITL_DIAGNOSTIC_NATIVE=1" not in command or "-DRESEARCH_C1D_CLOSED_OWNER_TARGET=1" not in command:
        raise ValueError("Not an actual native-profile compile command")
    if any("platforms/ros2/include" in token or "NATIVE_CONTRACT_TEST" in token for token in command):
        raise ValueError("Synthetic native input is forbidden")
    return command + ["-x", "c++", "-fsyntax-only", "-"], Path(item["directory"])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    build = args.build_dir.resolve()
    command, cwd = probe_command(build)
    profile = '#include "DiagnosticBuildProfile.hpp"\n'
    owner = '#include "NativeClosedDiagnosticOwner.hpp"\n'
    adapter = '#include "WireAdapter.hpp"\n'
    probes = []

    def check(name, source, success=True, extra=(), remove=(), diagnostic=None):
        actual = [token for token in command if token not in remove] + list(extra)
        result = subprocess.run(actual, cwd=cwd, input=source, text=True, capture_output=True, timeout=30, check=False)
        passed = ((result.returncode == 0) == success) and (diagnostic is None or diagnostic in result.stderr)
        probes.append({"name": name, "source": source, "command": actual, "expected_success": success,
                       "diagnostic_required": diagnostic, "exit_code": result.returncode,
                       "stdout": result.stdout, "stderr": result.stderr, "pass": passed})
        print(("PASS " if passed else "FAIL ") + name, flush=True)

    check("real native POSIX origin", owner +
          "static_assert(research_acceleration::kDiagnosticExecutionOrigin == research_acceleration::DiagnosticExecutionOrigin::NativePosixSitl);\n")
    check("ordinary Flight construction still usable", adapter +
          "void f(const research_acceleration::ContractConfig &c) { research_acceleration::WireAdapter a(c,{11}); }\n")
    for marker in ("RESEARCH_SITL_DIAGNOSTIC_NATIVE", "RESEARCH_C1D_CLOSED_OWNER_TARGET"):
        check("missing " + marker, profile, False, remove=["-D" + marker + "=1"])
        check("zero " + marker, profile, False, extra=["-D" + marker + "=0"], remove=["-D" + marker + "=1"])
    for marker in ("RESEARCH_SITL_DIAGNOSTIC_OFFLINE", "RESEARCH_SITL_DIAGNOSTIC_NATIVE_CONTRACT_TEST",
                   "RESEARCH_SITL_DIAGNOSTIC_HOST_TEST", "RESEARCH_SITL_DIAGNOSTIC_CAPABILITY",
                   "__PX4_NUTTX", "__PX4_ROS2", "__NuttX__", "ENABLE_LOCKSTEP_SCHEDULER",
                   "BOARD_LOCKSTEP", "PX4_SIMULATOR_TIMESTAMP_PROXY", "CONFIG_MUORB_APPS_SYNC_TIMESTAMP"):
        check("forbidden " + marker, profile, False, extra=["-D" + marker + "=1"], diagnostic="#error")
    check("owner constructor private", owner +
          "void f(const research_sitl::NativeClosedOwnerConfig &c) { research_sitl::NativeClosedDiagnosticOwner o(c); }\n",
          False, diagnostic="private")
    for peer in ("WireAdapterStateTestPeer", "WireAdapterSessionTestPeer", "WireAdapterAdmissionTestPeer", "DiagnosticAdapterTestPeer"):
        check("actual native excludes " + peer, adapter +
              "namespace research_acceleration { struct " + peer + " { static void f(WireAdapter &a) {(void)a.guard_;} }; }\n",
              False, diagnostic="private")
    check("actual native excludes DiagnosticGuardTestPeer", adapter +
          "namespace research_acceleration { struct DiagnosticGuardTestPeer { static void f(RequestGuard &g) {g.rejection_count_=0;} }; }\n",
          False, diagnostic="private")
    check("actual native excludes NativeClosedOwnerTestPeer", owner +
          "namespace research_sitl { struct NativeClosedOwnerTestPeer { static void f(NativeClosedDiagnosticOwner &o) {o.stop({1,0},1);} }; }\n",
          False, diagnostic="private")
    check("no offline constructor tag", adapter + "research_acceleration::DiagnosticOfflineTag tag;\n",
          False, diagnostic="DiagnosticOfflineTag")
    check("native identities have no fixture_run_id", adapter +
          "void f(research_acceleration::NativeDiagnosticIdentity &id) {(void)id.fixture_run_id;}\n", False,
          diagnostic="fixture_run_id")
    check("native reports reject frozen Flight mapper", '#include "WireStatus.hpp"\n' + adapter +
          "void f(const research_acceleration::DiagnosticReport &r,const research_acceleration::StatusRecipient &s,"
          "research_acceleration::WireStatusV1 &out) {buildStatusV1(r,s,out);}\n", False)
    manifest = {"schema": "research-c1d-native-profile-probes/v1", "compile_commands_sha256":
                hashlib.sha256((build / "compile_commands.json").read_bytes()).hexdigest(),
                "cwd": str(cwd), "probes": probes, "pass": all(row["pass"] for row in probes)}
    args.output.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
    return 0 if manifest["pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
