#!/usr/bin/env python3
"""Compile-only native-contract HOST isolation; never actual PX4 build evidence."""
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
    host = ["-DRESEARCH_SITL_DIAGNOSTIC_NATIVE_CONTRACT_TEST=1"]
    offline = ["-DRESEARCH_SITL_DIAGNOSTIC_OFFLINE=1", "-DRESEARCH_SITL_DIAGNOSTIC_HOST_TEST=1"]
    profile = '#include "DiagnosticBuildProfile.hpp"\n'
    adapter = '#include "WireAdapter.hpp"\nusing namespace research_acceleration;\n'
    owner = '#include "NativeClosedDiagnosticOwner.hpp"\nusing namespace research_sitl;\n'
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

    probe("ordinary Flight header remains usable", adapter +
          "void f(const ContractConfig &c) { WireAdapter a(c, {11}); }\n")
    probe("native-contract HOST header (not native PX4)", owner +
          "NativeClosedOwnerConfig configuration{}; NativeOwnerSummary summary{};\n", host)
    probe("ordinary Flight construction under native-contract HOST", adapter +
          "void f(const ContractConfig &c) { WireAdapter a(c, {11}); }\n", host)
    probe("legacy offline identity and constructor remain usable", adapter +
          "void f(const ContractConfig &c) { DiagnosticIdentity id{13,17};"
          "(void)id.fixture_run_id; WireAdapter a(c,{11},DiagnosticOfflineTag{},id); }\n", offline)
    probe("native owner absent from ordinary profile", owner, success=False)
    probe("native owner absent from offline profile", owner, offline, success=False)
    probe("native owner construction private", owner +
          "void f(const NativeClosedOwnerConfig &c) { NativeClosedDiagnosticOwner owner(c); }\n",
          host, success=False, diagnostic="private")
    for method, arguments in (("initial", "RawState{1,0},100"),
                              ("transition", "RawState{1,0},RawState{1,1},100"),
                              ("sample", "RawState{1,0},100"), ("stop", "RawState{1,0},100"),
                              ("sourceContinuityLost", "")):
        probe("native source " + method + " remains private", owner +
              "void f(NativeClosedDiagnosticOwner &o) { o." + method + "(" + arguments + "); }\n",
              host, success=False, diagnostic="private")
    for method in ("adapter", "reset", "attachDiagnosticOperator", "diagnosticTurn",
                   "processDiagnosticRequest", "getEligibleRequest"):
        probe("native owner exposes no " + method, owner +
              "void f(NativeClosedDiagnosticOwner &o) { o." + method + "(); }\n", host, success=False)
    probe("native owner is neither copyable nor movable", owner +
          "#include <type_traits>\n"
          "static_assert(!std::is_copy_constructible<NativeClosedDiagnosticOwner>::value);\n"
          "static_assert(!std::is_move_constructible<NativeClosedDiagnosticOwner>::value);\n", host)
    probe("native identity has no fixture lifetime field", adapter +
          "void f(NativeDiagnosticIdentity &id) { (void)id.fixture_run_id; }\n", host, success=False)
    probe("native profile cannot construct through offline tag", adapter +
          "DiagnosticOfflineTag tag;\n", host, success=False, diagnostic="DiagnosticOfflineTag")
    probe("native adapter construction private to real owner", adapter +
          "void f(const ContractConfig &c) { WireAdapter a(c,{11},DiagnosticNativeSitlTag{}, {13,17}); }\n",
          host, success=False, diagnostic="private")
    probe("legacy diagnostic guard peer has no native-contract friendship", adapter +
          "namespace research_acceleration { struct DiagnosticGuardTestPeer { static void f(RequestGuard &g) {"
          "g.rejection_count_=0; } }; }\n", host, success=False, diagnostic="private")
    probe("legacy diagnostic adapter peer has no native-contract friendship", adapter +
          "namespace research_acceleration { struct DiagnosticAdapterTestPeer { static void f(WireAdapter &a) {"
          "a.attachDiagnosticOperator(19); } }; }\n", host, success=False, diagnostic="private")
    probe("native reports cannot use frozen Flight status mapper", '#include "WireStatus.hpp"\n' + adapter +
          "void f(const DiagnosticReport &r,const StatusRecipient &s,WireStatusV1 &out) {buildStatusV1(r,s,out);}\n",
          host, success=False)
    offline_json = ('#include "DiagnosticJson.hpp"\n'
                    'auto f(const research_acceleration::DiagnosticResult &result) {'
                    'return research_sitl::diagnostic_json::value(result); }\n')
    probe("offline JSON serializer include and result overload remain usable", offline_json, offline)
    probe("native result cannot enter offline JSON serializer", offline_json, host,
          success=False, diagnostic="DiagnosticIdentity")
    for value in (0, 1):
        probe("caller cannot supply derived capability " + str(value), profile,
              [f"-DRESEARCH_SITL_DIAGNOSTIC_CAPABILITY={value}"], success=False,
              diagnostic="Diagnostic capability must be derived from a validated profile")
    native_error = "Native diagnostic profile requires dedicated target and forbids host/hardware/lockstep/proxy markers"
    host_error = "Native diagnostic contract tests require isolated Linux host provenance"
    for name in ("RESEARCH_SITL_DIAGNOSTIC_OFFLINE", "RESEARCH_SITL_DIAGNOSTIC_NATIVE_CONTRACT_TEST",
                 "RESEARCH_SITL_DIAGNOSTIC_NATIVE"):
        expected = ("Diagnostic types require the isolated offline test target" if name.endswith("OFFLINE")
                    else host_error if name.endswith("CONTRACT_TEST") else native_error)
        probe("zero is not profile opt-in: " + name, profile, ["-D" + name + "=0"], success=False, diagnostic=expected)
    for name in ("RESEARCH_SITL_DIAGNOSTIC_OFFLINE", "RESEARCH_SITL_DIAGNOSTIC_NATIVE",
                 "RESEARCH_SITL_DIAGNOSTIC_HOST_TEST", "RESEARCH_C1D_CLOSED_OWNER_TARGET"):
        expected = "Diagnostic build profiles are mutually exclusive" if name in {
            "RESEARCH_SITL_DIAGNOSTIC_OFFLINE", "RESEARCH_SITL_DIAGNOSTIC_NATIVE"} else host_error
        probe("contract HOST rejects mixed " + name, profile, host + ["-D" + name + "=1"], success=False, diagnostic=expected)
    probe("offline and actual native are mutually exclusive", profile,
          offline + ["-DRESEARCH_SITL_DIAGNOSTIC_NATIVE=1", "-DRESEARCH_C1D_CLOSED_OWNER_TARGET=1"], success=False,
          diagnostic="Diagnostic build profiles are mutually exclusive")
    for forbidden in ("__PX4_NUTTX", "__NuttX__", "__PX4_POSIX", "__PX4_LINUX", "__PX4_ROS2",
                      "CONFIG_ARCH_BOARD", "CONFIG_ARCH_BOARD_PX4_SITL", "ENABLE_LOCKSTEP_SCHEDULER",
                      "BOARD_LOCKSTEP", "PX4_SIMULATOR_TIMESTAMP_PROXY", "CONFIG_MUORB_APPS_SYNC_TIMESTAMP"):
        probe("contract HOST rejects " + forbidden, profile, host + ["-D" + forbidden + "=1"], success=False, diagnostic=host_error)
    probe("contract HOST requires Linux", profile, host + ["-U__linux__"], success=False, diagnostic=host_error)
    # These are malformed-native negative preprocessor probes only. Their macro
    # inputs do not establish a real PX4 target, native include path, or runtime.
    native = ["-DRESEARCH_SITL_DIAGNOSTIC_NATIVE=1"]
    probe("malformed-native missing target marker", profile, native, success=False, diagnostic=native_error)
    probe("malformed-native zero target marker", profile,
          native + ["-DRESEARCH_C1D_CLOSED_OWNER_TARGET=0", "-D__PX4_POSIX=1"], success=False, diagnostic=native_error)
    probe("malformed-native rejects offline HOST marker", profile,
          native + ["-DRESEARCH_C1D_CLOSED_OWNER_TARGET=1", "-D__PX4_POSIX=1",
                    "-DRESEARCH_SITL_DIAGNOSTIC_HOST_TEST=1"], success=False, diagnostic=native_error)
    for forbidden in ("__PX4_NUTTX", "__PX4_ROS2", "__NuttX__", "ENABLE_LOCKSTEP_SCHEDULER", "BOARD_LOCKSTEP",
                      "PX4_SIMULATOR_TIMESTAMP_PROXY", "CONFIG_MUORB_APPS_SYNC_TIMESTAMP"):
        probe("malformed-native early rejection: " + forbidden, profile,
              native + ["-DRESEARCH_C1D_CLOSED_OWNER_TARGET=1", "-D" + forbidden + "=1"],
              success=False, diagnostic=native_error)
    for marker in ("RESEARCH_SITL_DIAGNOSTIC_HOST_TEST", "RESEARCH_C1D_CLOSED_OWNER_TARGET"):
        probe("orphan target marker rejected: " + marker, profile, ["-D" + marker + "=1"], success=False,
              diagnostic="Diagnostic target marker requires its matching profile")
    print(f"{len(results)} native-contract HOST/synthetic-negative isolation probes, {results.count(False)} failures")
    return 0 if all(results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
