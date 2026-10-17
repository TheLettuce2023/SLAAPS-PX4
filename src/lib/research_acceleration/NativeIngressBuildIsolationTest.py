#!/usr/bin/env python3
"""Compile-only C1E marker and private-API probes; no PX4 runtime claim."""

import argparse
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--source-dir", required=True)
    args = parser.parse_args()
    source = Path(args.source_dir).resolve()
    command = [args.compiler, "-std=c++17", "-x", "c++", "-fsyntax-only", "-"]
    for include in (source, source.parent, source.parents[2] / "platforms/ros2/include",
                    source.parents[2] / "Tools/research_sitl/diagnostic",
                    source.parents[2] / "Tools/research_sitl/supervisor"):
        command.extend(["-I", str(include)])
    profile = '#include "DiagnosticBuildProfile.hpp"\n'
    owner = '#include "NativeIngressDiagnosticOwner.hpp"\nusing namespace research_sitl;\n'
    adapter = '#include "WireAdapter.hpp"\nusing namespace research_acceleration;\n'
    contract = ["-DRESEARCH_SITL_DIAGNOSTIC_NATIVE_CONTRACT_TEST=1",
                "-DRESEARCH_C1E_NEGATIVE_CONTRACT_TEST=1"]
    c1d = ["-DRESEARCH_SITL_DIAGNOSTIC_NATIVE_CONTRACT_TEST=1"]
    offline = ["-DRESEARCH_SITL_DIAGNOSTIC_OFFLINE=1",
               "-DRESEARCH_SITL_DIAGNOSTIC_HOST_TEST=1"]
    results = []

    def probe(name, body, defines=(), success=True, diagnostic=None):
        result = subprocess.run(command + list(defines), input=body, text=True,
                                capture_output=True, timeout=30, check=False)
        passed = (result.returncode == 0) == success
        if diagnostic is not None:
            passed = passed and diagnostic in result.stderr
        results.append(passed)
        print(f"{'PASS' if passed else 'FAIL'}: {name}")
        if not passed:
            print(result.stdout)
            print(result.stderr)

    probe("ordinary Flight adapter remains available", adapter +
          "void f(const ContractConfig &c) { WireAdapter a(c, {11}); }\n")
    probe("private C1E synthetic contract profile compiles", owner +
          "NativeIngressOwnerConfig c{}; NativeIngressMailbox m{}; NativeIngressOwnerSummary s{};\n",
          contract)
    probe("C1D contract profile remains distinct", '#include "NativeClosedDiagnosticOwner.hpp"\n', c1d)
    probe("C1E owner absent from ordinary profile", owner, success=False)
    probe("C1E owner absent from offline profile", owner, offline, success=False)
    probe("C1E owner absent from C1D contract profile", owner, c1d, success=False,
          diagnostic="C1E ingress owner requires its private native or contract profile")
    probe("caller cannot construct C1E owner", owner +
          "void f(const NativeIngressOwnerConfig &c, NativeIngressMailbox &m)"
          " { NativeIngressDiagnosticOwner o(c,m); }\n",
          contract, success=False, diagnostic="private")
    for method, args_text in (("initial", "RawState{1,0},100"),
                              ("transition", "RawState{1,0},RawState{1,14},100"),
                              ("sample", "RawState{1,0},100"),
                              ("resolveReady", "RawState{1,0},100"),
                              ("stopOrFault", "NativeIngressCloseCause::OwnerStop,RawState{1,0},100")):
        probe("source method " + method + " is private", owner +
              "void f(NativeIngressDiagnosticOwner &o) { o." + method + "(" + args_text + "); }\n",
              contract, success=False, diagnostic="private")
    for method in ("adapter", "authenticate", "attachDiagnosticOperator", "enable",
                   "grant", "acknowledge", "reset", "getEligibleRequest"):
        probe("C1E public owner has no " + method, owner +
              "void f(NativeIngressDiagnosticOwner &o) { o." + method + "(); }\n",
              contract, success=False)
    probe("candidate cannot carry caller authentication verdict", owner +
          "void f(NativeIngressCandidate &c) { c.parser_authenticated = true; }\n",
          contract, success=False)
    probe("C1E owner is neither copyable nor movable", owner +
          "#include <type_traits>\n"
          "static_assert(!std::is_copy_constructible<NativeIngressDiagnosticOwner>::value);\n"
          "static_assert(!std::is_move_constructible<NativeIngressDiagnosticOwner>::value);\n",
          contract)
    probe("C1D target cannot be mixed with C1E synthetic target", profile,
          contract + ["-DRESEARCH_C1D_CLOSED_OWNER_TARGET=1"], success=False,
          diagnostic="Native diagnostic contract tests require isolated Linux host provenance")
    probe("C1E synthetic marker must equal one", profile,
          c1d + ["-DRESEARCH_C1E_NEGATIVE_CONTRACT_TEST=0"], success=False,
          diagnostic="C1E synthetic target marker must equal one")
    probe("orphan C1E synthetic marker rejected", profile,
          ["-DRESEARCH_C1E_NEGATIVE_CONTRACT_TEST=1"], success=False,
          diagnostic="Diagnostic target marker requires its matching profile")
    probe("orphan C1E native marker rejected", profile,
          ["-DRESEARCH_C1E_NEGATIVE_OWNER_TARGET=1"], success=False,
          diagnostic="Diagnostic target marker requires its matching profile")
    probe("native and synthetic C1E target markers reject mixing", profile,
          contract + ["-DRESEARCH_C1E_NEGATIVE_OWNER_TARGET=1"], success=False,
          diagnostic="C1E native and synthetic target markers are mutually exclusive")
    probe("caller cannot spoof derived capability", profile,
          contract + ["-DRESEARCH_SITL_DIAGNOSTIC_CAPABILITY=1"], success=False,
          diagnostic="Diagnostic capability must be derived from a validated profile")
    for forbidden in ("__PX4_NUTTX", "__PX4_POSIX", "__PX4_ROS2", "CONFIG_ARCH_BOARD_PX4_SITL",
                      "ENABLE_LOCKSTEP_SCHEDULER", "PX4_SIMULATOR_TIMESTAMP_PROXY"):
        probe("synthetic contract rejects " + forbidden, profile,
              contract + ["-D" + forbidden + "=1"], success=False,
              diagnostic="Native diagnostic contract tests require isolated Linux host provenance")
    print(f"{len(results)} C1E compile-only isolation probes, {results.count(False)} failures")
    return 0 if all(results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
