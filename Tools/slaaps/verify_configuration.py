#!/usr/bin/env python3
"""Build and exercise the real SLAAPS configuration adapters without hardware."""

import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]


def main():
    out = Path(tempfile.mkdtemp(prefix="slaaps-configuration-", dir="/var/tmp"))
    print(f"Evidence: {out}", flush=True)
    inputs = [
        *sorted((ROOT / "src/lib/slaaps").glob("*")),
        *sorted((ROOT / "Tools/slaaps/configuration").glob("*")),
        ROOT / "Tools/slaaps/generate_parameters.py",
        ROOT / "Tools/slaaps/verify_configuration.py",
        ROOT / "src/modules/slaaps/module.yaml",
        ROOT / "msg/SlaapsOutputBank.msg",
        ROOT / "msg/SlaapsManualCommand.msg",
        ROOT / "msg/SlaapsManualAck.msg",
        ROOT / "msg/SlaapsManualPreparation.msg",
        ROOT / "msg/SlaapsManualActivation.msg",
        ROOT / "msg/SlaapsManualPermission.msg",
        ROOT / "src/lib/mixer_module/output_functions.yaml",
        ROOT / "src/lib/mixer_module/generate_function_header.py",
        *sorted((ROOT / "src/lib/research_stabilized_manual").glob("*pp")),
        *sorted((ROOT / "src/lib/research_actuator_command").glob("*pp")),
        ROOT / "src/lib/rate_control/rate_control.cpp",
        ROOT / "src/lib/rate_control/rate_control.hpp",
        *[ROOT / "src/lib/research_acceleration" / name for name in
          ("TrustedTimeMap.hpp", "RequestGuard.hpp", "DiagnosticBuildProfile.hpp", "FrameMath.hpp", "FrameMath.cpp")],
    ]
    hashes = {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
              for p in inputs if p.is_file()}
    (out / "input-hashes.json").write_text(json.dumps(hashes, indent=2) + "\n")
    for relative in hashes:
        destination = out / "source-snapshot" / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / relative, destination)

    def run(name, command):
        with (out / f"{name}.log").open("w") as log:
            result = subprocess.run([str(x) for x in command], stdout=log, stderr=subprocess.STDOUT)
        if result.returncode:
            print((out / f"{name}.log").read_text(), flush=True)
            raise RuntimeError(f"{name} failed with {result.returncode}")
        print(f"PASS {name}", flush=True)

    run("metadata", [sys.executable, ROOT / "Tools/slaaps/generate_parameters.py", "--check"])
    param_dir = out / "parameter-source"
    param_dir.mkdir()
    run("px4-parameter-source", [sys.executable, ROOT / "Tools/module_config/generate_params.py",
                                "--config-files", ROOT / "src/modules/slaaps/module.yaml",
                                "--params-file", param_dir / "slaaps_params.c"])
    run("px4-parameter-xml", [sys.executable, ROOT / "src/lib/parameters/px_process_params.py",
                             "--src-path", param_dir, "--xml", out / "parameters.xml",
                             "--json", out / "parameters.json"])
    from generate_parameters import definitions
    expected = {p["name"]: p for p in definitions()}
    vehicle_parameter_count = len(expected)
    expected.update(SLA_CFG_APPLY={"default": 0}, SLA_CFG_KIND={"default": 0})
    expected.update({name: {"default": 0} for name in
                     ("SLA_CFG_SAVED", "SLA_CFG_SCOPE", "SLA_CFG_ID_LO", "SLA_CFG_ID_HI")})
    expected.update({name: {"default": 0} for name in
                     ("SLA_BENCH_EN", "SLA_BENCH_CH", "SLA_BENCH_SEC")})
    expected.update(SLA_CMP_ACT={"default": 0})
    actual = {p.attrib["name"]: p for p in ET.parse(out / "parameters.xml").iter("parameter")}
    assert set(actual) == set(expected), (set(expected) - set(actual), set(actual) - set(expected))
    for name, item in expected.items():
        assert float(actual[name].attrib["default"]) == item["default"], name
    selector_values = {
        "SLA_ACC_CTL_EN": {"0": "Disabled", "1": "Configured numerical controller"},
        "SLA_DATA_STATE": {"0": "Incomplete", "1": "Calibrated", "2": "Qualified"},
        "SLA_IN_SOURCE": {"0": "Unconfigured", "1": "RC only", "2": "Pinned MAVLink only"},
        "SLA_CFG_KIND": {"0": "Servo bench", "1": "Stabilized manual", "2": "EDF commissioning"},
        "SLA_BENCH_EN": {"0": "Stop / disabled", "1": "Enable commissioning session"},
        "SLA_BENCH_CH": {"0": "Combined response", "1": "Canard 1", "2": "Canard 2",
                         "3": "Canard 3", "4": "Canard 4", "5": "EDF (deliberately armed)"},
        "SLA_CMP_ACT": {"0": "None", "1": "Prepare staged authenticated session",
                        "2": "Grant exact prepared session", "3": "Revoke session", "4": "Clear latched fault"},
        "SLA_SCH_EN": {"0": "Disabled", "1": "Empirical 2x2 table"},
        "SLA_PWR_INST": {"-1": "Unconfigured", **{str(i): f"Battery instance {i}" for i in range(3)}},
        "SLA_PWR_SRC": {"-1": "Unconfigured", "0": "Power module", "1": "External", "2": "ESC telemetry"},
        "SLA_SCH_BAT_INST": {"-1": "Unconfigured", **{str(i): f"Instance {i}" for i in range(3)}},
        **{name: {"-1": "Unconfigured", **{str(i): f"Instance {i}" for i in range(4)}}
           for name in ("SLA_SCH_AS_INST", "SLA_SCH_AD_INST")},
        **{name: {"-1": "Unconfigured", "0": "Roll", "1": "Pitch", "2": "Yaw"}
           for name in ("SLA_R_INPUT", "SLA_P_INPUT", "SLA_Y_INPUT")},
    }
    for name, labels in selector_values.items():
        values = {value.attrib["code"]: value.text for value in actual[name].findall("values/value")}
        assert values == labels, (name, values)
    assert actual["SLA_BENCH_SEC"].findtext("min") == "0"
    assert actual["SLA_DATA_VER"].findtext("min") == "1"
    assert actual["SLA_DATA_VER"].findtext("max") == "1"
    assert actual["SLA_DATA_REC"].findtext("min") == "0"
    assert actual["SLA_DATA_REC"].findtext("max") == "2147483647"
    assert actual["SLA_BENCH_SEC"].findtext("max") == "60"
    assert actual["SLA_SCH_AGE"].findtext("min") == "0"
    assert actual["SLA_SCH_AGE"].findtext("max") == "1000000"
    for name in ("SLA_SCH_EDF_LO", "SLA_SCH_EDF_HI"):
        assert float(actual[name].findtext("min")) == 0
        assert float(actual[name].findtext("max")) == 1
    check_counts = {}
    for name, build_type, sanitizers in (("debug", "Debug", "OFF"), ("release", "Release", "OFF"),
                                         ("sanitized", "Debug", "ON")):
        build = out / name
        run(f"{name}-configure", ["cmake", "-S", ROOT / "Tools/slaaps/configuration", "-B", build,
                                 "-G", "Ninja", f"-DCMAKE_BUILD_TYPE={build_type}",
                                 f"-DSLAAPS_SANITIZERS={sanitizers}"])
        run(f"{name}-build", ["cmake", "--build", build, "-j", "6"])
        run(f"{name}-test", ["ctest", "--test-dir", build, "--output-on-failure", "-V"])
        match = re.search(r"PASS (\d+) checks;", (out / f"{name}-test.log").read_text())
        assert match, name
        check_counts[name] = int(match.group(1))
    for relative, digest in hashes.items():
        assert hashlib.sha256((ROOT / relative).read_bytes()).hexdigest() == digest, relative
    receipt = dict(passed=True, scope="host_configuration_adapters_and_actual_PX4_parameter_metadata",
                   parameter_count=len(expected), vehicle_parameter_count=vehicle_parameter_count,
                   checks_per_configuration=check_counts,
                   source_hashes=hashes, native_module_verified=False, flight_modes_verified=False,
                   physical_outputs=False, vehicle_acceptance=False)
    (out / "result.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps({k: v for k, v in receipt.items() if k != "source_hashes"}), flush=True)


if __name__ == "__main__":
    main()
