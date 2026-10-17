#!/usr/bin/env python3
"""Compile-only standalone state-receipt boundary probes; never runs a vehicle."""

import argparse
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--source-dir", required=True, type=Path)
    args = parser.parse_args()
    source = args.source_dir.resolve()
    header = '#include "StateReceiptChecker.hpp"\nusing namespace research_sitl::state_receipts;\n'
    command = [args.compiler, "-std=c++17", "-x", "c++", "-fsyntax-only", "-", "-I", str(source)]
    opt_in = ["-DRESEARCH_SITL_STATE_RECEIPT_HOST_TEST=1"]
    results = []

    def probe(name, body, defines=(), expected_error=None):
        result = subprocess.run(command + list(defines), input=body, text=True, capture_output=True, timeout=30, check=False)
        success = result.returncode == 0 if expected_error is None else result.returncode != 0 and expected_error in result.stderr
        results.append(success)
        print(f"{'PASS' if success else 'FAIL'}: {name}")
        if not success:
            print(result.stdout)
            print(result.stderr)

    probe("explicit standalone opt-in compiles", header + "void f(const Config &c) { ReceiptChecker checker(c); }\n", opt_in)
    marker_error = "State receipts require the isolated standalone host-test target"
    probe("missing standalone opt-in rejected", header, expected_error=marker_error)
    probe("zero marker is not opt-in", header, ["-DRESEARCH_SITL_STATE_RECEIPT_HOST_TEST=0"], marker_error)
    platform_error = "State receipts are not released for PX4, hardware, lockstep or timestamp proxies"
    probe("non-Linux host rejected", header, opt_in + ["-U__linux__"], platform_error)
    for define in ("__PX4_NUTTX", "__PX4_POSIX", "__PX4_LINUX", "__PX4_ROS2", "__NuttX__", "CONFIG_ARCH_BOARD",
                   "CONFIG_ARCH_BOARD_PX4_SITL", "ENABLE_LOCKSTEP_SCHEDULER", "BOARD_LOCKSTEP",
                   "PX4_SIMULATOR_TIMESTAMP_PROXY", "CONFIG_MUORB_APPS_SYNC_TIMESTAMP"):
        probe("forbidden platform marker " + define, header, opt_in + ["-D" + define + "=1"], platform_error)
    probe("private state cannot be rewritten", header + "void f(ReceiptChecker &c) { c.snapshot_.receipt_sequence = 8; }\n",
          opt_in, "private")
    probe("no reset entry point", header + "void f(ReceiptChecker &c) { c.reset(); }\n", opt_in, "reset")
    probe("no public lifetime rebind", header + "void f(ReceiptChecker &c) { c.rebind(); }\n", opt_in, "rebind")
    probe("no implicit default config", header + "void f() { ReceiptChecker c; }\n", opt_in, "ReceiptChecker")
    probe("checker cannot copy", header + "void f(ReceiptChecker &c) { ReceiptChecker duplicate(c); }\n", opt_in, "deleted")
    probe("checker cannot move", '#include <utility>\n' + header + "void f(ReceiptChecker &c) { ReceiptChecker moved(std::move(c)); }\n",
          opt_in, "deleted")
    probe("snapshot grants no control eligibility", header + "bool f(const Snapshot &s) { return s.eligible_for_control; }\n",
          opt_in, "eligible_for_control")
    print(f"State receipt build isolation: {len(results)} probes, {sum(not result for result in results)} failures")
    return 0 if all(results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
