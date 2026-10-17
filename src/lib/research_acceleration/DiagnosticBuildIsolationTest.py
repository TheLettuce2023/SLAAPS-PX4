#!/usr/bin/env python3
"""Compile-only C1B guard/adapter/owner isolation probes; no vehicle is run."""

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
    for include in (source, source.parent, source.parents[2] / "platforms/ros2/include"):
        command.extend(["-I", str(include)])
    command.extend(["-I", str(source.parents[2] / "Tools/research_sitl/supervisor")])
    opt_in = ["-DRESEARCH_SITL_DIAGNOSTIC_OFFLINE=1", "-DRESEARCH_SITL_DIAGNOSTIC_HOST_TEST=1"]
    header = '#include "RequestGuard.hpp"\nusing namespace research_acceleration;\n'
    checks = []

    def probe(name, body, defines=(), expected_error=None):
        result = subprocess.run(command + list(defines), input=body, text=True,
                                capture_output=True, timeout=30, check=False)
        success = result.returncode == 0 if expected_error is None else (
            result.returncode != 0 and expected_error in result.stderr)
        checks.append(success)
        print(f"{'PASS' if success else 'FAIL'}: {name}")
        if not success:
            print(result.stdout)
            print(result.stderr)

    probe("ordinary header compiles", header + "void f(const ContractConfig &c) { RequestGuard g(c); }\n")
    probe("opted-in header compiles", header + "DiagnosticOfflineTag tag;\n", opt_in)
    probe("diagnostic type absent from ordinary header", header + "DiagnosticOfflineTag tag;\n",
          expected_error="DiagnosticOfflineTag")
    probe("direct diagnostic include requires opt-in", '#include "DiagnosticTypes.hpp"\n',
          expected_error="Diagnostic types require the isolated offline test target")
    probe("macro zero is not opt-in", header,
          ["-DRESEARCH_SITL_DIAGNOSTIC_OFFLINE=0", opt_in[1]],
          "Diagnostic types require the isolated offline test target")
    probe("missing standalone marker fails", header, opt_in[:1],
          "Diagnostic types require the standalone host-test build")
    probe("constructor is private", header +
          "void f(const ContractConfig &c) { RequestGuard g(c, DiagnosticOfflineTag{}); }\n",
          opt_in, "private")
    probe("diagnostic evaluation is private", header +
          "void f(RequestGuard &g, const CanonicalRequest &r, const EvaluationContext &c) {"
          "g.evaluateDiagnostic(r, c, DiagnosticSelectionState::SelectedForValidation); }\n",
          opt_in, "private")
    probe("no implicit conversion to Flight Decision", header +
          "Decision f(const DiagnosticGuardResult &r) { return r; }\n", opt_in, "convert")
    adapter_header = '#include "WireAdapter.hpp"\nusing namespace research_acceleration;\n'
    probe("ordinary adapter header compiles", adapter_header +
          "void f(const ContractConfig &c) { WireAdapter a(c, {11}); }\n")
    probe("diagnostic adapter types absent by default", adapter_header +
          "DiagnosticResult result;\n", expected_error="DiagnosticResult")
    probe("diagnostic adapter constructor compiles", adapter_header +
          "void f(const ContractConfig &c) { WireAdapter a(c, {11}, DiagnosticOfflineTag{}, {13,17}); }\n", opt_in)
    probe("operator attachment is private", adapter_header +
          "void f(WireAdapter &a) { a.attachDiagnosticOperator(19); }\n", opt_in, "private")
    probe("lifecycle closure is private", adapter_header +
          "void f(WireAdapter &a) { a.closeDiagnosticLifecycle(std::nullopt, DiagnosticCloseReason::OperatorLost); }\n",
          opt_in, "private")
    probe("diagnostic result cannot convert to Flight result", adapter_header +
          "AdapterResult f(const DiagnosticResult &r) { return r; }\n", opt_in, "convert")
    probe("diagnostic report has no frozen v1 mapper", '#include "WireStatus.hpp"\n' + adapter_header +
          "void f(const DiagnosticReport &r, const StatusRecipient &s, WireStatusV1 &out) { buildStatusV1(r,s,out); }\n",
          opt_in, "no matching function")
    owner_header = '#include "HarnessSupervisorOwner.hpp"\nusing namespace research_sitl;\n'
    probe("ordinary owner header compiles", owner_header +
          "void f(const OwnerHarnessConfig &c) { HarnessSupervisorOwner o(c); }\n")
    probe("diagnostic owner types absent by default", owner_header +
          "DiagnosticOwnerView view;\n", expected_error="DiagnosticOwnerView")
    probe("diagnostic owner construction compiles", owner_header +
          "void f(const OwnerHarnessConfig &c) { HarnessSupervisorOwner o(c, research_acceleration::DiagnosticOfflineTag{}, 13); }\n", opt_in)
    probe("facade cannot access mutable owner", owner_header +
          "void f(SupervisorConsoleFacade &c) { c.owner(); }\n", opt_in, "no member")
    probe("facade cannot inject requests", owner_header +
          "void f(SupervisorConsoleFacade &c) { c.processDiagnosticRequest(); }\n", opt_in, "no member")
    probe("console result cannot expose full snapshot", owner_header +
          "void f(DiagnosticConsoleResult &r) { (void)r.snapshot; }\n", opt_in, "no member")
    probe("diagnostic owner has no heartbeat admission API", owner_header +
          "void f(HarnessSupervisorOwner &o) { o.processHeartbeat(); }\n", opt_in, "no member")
    probe("diagnostic owner has no status admission API", owner_header +
          "void f(HarnessSupervisorOwner &o) { o.processStatus(); }\n", opt_in, "no member")
    for forbidden in ("__PX4_NUTTX", "__PX4_POSIX", "__PX4_LINUX", "__NuttX__",
                      "CONFIG_ARCH_BOARD", "ENABLE_LOCKSTEP_SCHEDULER", "BOARD_LOCKSTEP",
                      "PX4_SIMULATOR_TIMESTAMP_PROXY"):
        probe(f"reject {forbidden}", header, opt_in + [f"-D{forbidden}=1"],
              "Diagnostic validation is not released for PX4")
    probe("reject non-Linux compile", header, opt_in + ["-U__linux__"],
          "Diagnostic validation is not released for PX4")
    print(f"{len(checks)} compile-isolation probes, {checks.count(False)} failures")
    return 0 if all(checks) else 1


if __name__ == "__main__":
    raise SystemExit(main())
