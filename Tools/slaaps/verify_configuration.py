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
        ROOT / "src/lib/mixer_module/output_functions.yaml",
        ROOT / "src/lib/mixer_module/generate_function_header.py",
        *sorted((ROOT / "src/lib/research_stabilized_manual").glob("*pp")),
        *sorted((ROOT / "src/lib/research_actuator_command").glob("*pp")),
        ROOT / "src/lib/rate_control/rate_control.cpp",
        ROOT / "src/lib/rate_control/rate_control.hpp",
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
    actual = {p.attrib["name"]: p for p in ET.parse(out / "parameters.xml").iter("parameter")}
    assert set(actual) == set(expected), (set(expected) - set(actual), set(actual) - set(expected))
    for name, item in expected.items():
        assert float(actual[name].attrib["default"]) == item["default"], name
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
