#!/usr/bin/env python3
"""Exercise verbatim PWMOut handling with injected register calls, never hardware."""
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
BASELINE = "472a7d6786fcbb475c8396cd7e7a3b4d407d17fc"


def method(source):
    signature = "bool PWMOut::updateOutputs("
    assert source.count(signature) == 1
    start = source.index(signature)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    out = Path(tempfile.mkdtemp(prefix="slaaps-pwm-output-", dir="/var/tmp"))
    print(f"Evidence: {out}", flush=True)
    source = ROOT / "src/drivers/pwm_out/PWMOut.cpp"
    fixture = ROOT / "Tools/slaaps/pwm_output/PwmOutputTest.cpp"
    paths = [source, fixture, Path(__file__), ROOT / "src/drivers/drv_pwm_output.h",
             ROOT / "platforms/nuttx/src/px4/stm/stm32_common/io_pins/pwm_servo.c"]
    hashes = {}
    for path in paths:
        name = path.relative_to(ROOT)
        target = out / "inputs" / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(path.read_bytes())
        hashes[name.as_posix()] = digest(path)
    baseline = subprocess.check_output(["git", "show", BASELINE + ":src/drivers/pwm_out/PWMOut.cpp"], cwd=ROOT, text=True)
    for variant, text in (("baseline", baseline), ("corrected", source.read_text())):
        directory = out / variant
        directory.mkdir()
        (directory / "PwmOutputMethod.inc").write_text(method(text))
    results = {}
    for mode, flags in (("debug", ["-O0", "-g"]), ("release", ["-O2", "-DNDEBUG"]),
                        ("sanitized", ["-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie"])):
        for variant in (("baseline", "corrected") if mode == "debug" else ("corrected",)):
            label = mode + "-" + variant
            binary = out / label
            command = ["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", *flags,
                       "-I", str(out / variant), str(out / "inputs" / fixture.relative_to(ROOT)), "-o", str(binary)]
            built = subprocess.run(command, capture_output=True, text=True)
            (out / (label + "-build.log")).write_text(built.stdout + built.stderr)
            (out / (label + "-command.json")).write_text(json.dumps(command, indent=2))
            assert built.returncode == 0, str(out)
            tested = subprocess.run([str(binary)], capture_output=True, text=True)
            (out / (label + "-test.log")).write_text(tested.stdout + tested.stderr)
            assert tested.returncode == (1 if variant == "baseline" else 0), str(out)
            match = re.search(r"PWM_OUTPUT_RESULT checks=(\d+) failures=(\d+)", tested.stdout)
            assert match and int(match[1]) == 25
            assert int(match[2]) == (9 if variant == "baseline" else 0)
            results[label] = dict(checks=int(match[1]), failures=int(match[2]))
    assert all(digest(ROOT / name) == value for name, value in hashes.items())
    receipt = dict(passed=True, scope="verbatim_PWMOut_method_injected_registers_no_hardware",
                   firmware_linked=False, hardware_operated=False, baseline_ref=BASELINE,
                   source_hashes=hashes, results=results)
    receipt["artifacts"] = {p.relative_to(out).as_posix(): digest(p) for p in out.rglob("*") if p.is_file()}
    (out / "result.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps(dict(evidence=str(out), results=results)), flush=True)


if __name__ == "__main__":
    main()
