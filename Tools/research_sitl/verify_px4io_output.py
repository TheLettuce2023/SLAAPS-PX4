#!/usr/bin/env python3
"""Verify verbatim PX4IO methods with injected transport; never operate hardware."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SIGNATURES = (
    "bool PX4IO::updateOutputs(",
    "int PX4IO::io_reg_set(uint8_t page, uint8_t offset, const uint16_t *values, unsigned num_values)",
)


def methods(text):
    result = []
    for signature in SIGNATURES:
        assert text.count(signature) == 1, signature
        start = text.index(signature)
        opening = text.index("{", start)
        depth = 1
        end = opening + 1
        while depth:
            depth += (text[end] == "{") - (text[end] == "}")
            end += 1
        result.append(text[start:end])
    return "\n\n".join(result) + "\n"


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--baseline-ref", default="472a7d6786fcbb475c8396cd7e7a3b4d407d17fc")
    args = parser.parse_args()
    out = args.output or Path(tempfile.mkdtemp(prefix="research-px4io-output-", dir="/var/tmp"))
    out.mkdir(parents=True, exist_ok=True)
    assert not any(out.iterdir()), "Use an empty evidence directory"
    source = ROOT / "src/drivers/px4io/px4io.cpp"
    fixture = ROOT / "Tools/research_sitl/px4io_output/Px4IOOutputTest.cpp"
    baseline = subprocess.check_output(
        ["git", "show", args.baseline_ref + ":src/drivers/px4io/px4io.cpp"], cwd=ROOT, text=True)
    (out / "baseline").mkdir()
    (out / "corrected").mkdir()
    (out / "baseline/Px4IOOutputMethods.inc").write_text(methods(baseline))
    (out / "corrected/Px4IOOutputMethods.inc").write_text(methods(source.read_text()))
    bindings = {}
    paths = [source, fixture, Path(__file__), ROOT / "src/modules/px4iofirmware/protocol.h"]
    for directory in ("src/lib/research_stabilized_manual", "src/lib/research_actuator_command",
                      "src/lib/rate_control", "src/lib/matrix"):
        paths.extend(p for p in (ROOT / directory).rglob("*") if p.is_file())
    for path in paths:
        relative = path.relative_to(ROOT)
        target = out / "inputs" / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(path.read_bytes())
        bindings[str(relative)] = digest(path)

    def run(command, name, expected=0):
        result = subprocess.run(command, cwd=ROOT, text=True, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT)
        (out / (name + ".log")).write_text(result.stdout)
        (out / (name + ".command.json")).write_text(json.dumps(command, indent=2))
        assert result.returncode == expected, f"{name}: {result.returncode}; see {out}"
        return result.stdout

    results = {}
    for mode in ("debug", "release", "sanitize"):
        build = out / mode
        flags = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-pie"] if mode == "sanitize" else []
        link = ["-no-pie"] if mode == "sanitize" else []
        run(["cmake", "-S", str(ROOT / "src/lib/research_stabilized_manual"), "-B", str(build),
             "-DCMAKE_BUILD_TYPE=" + ("Release" if mode == "release" else "Debug"),
             "-DCMAKE_CXX_FLAGS=" + " ".join(flags),
             "-DCMAKE_EXE_LINKER_FLAGS=" + " ".join(link)], mode + "-configure")
        run(["cmake", "--build", str(build), "-j4"], mode + "-build")
        run(["ctest", "--test-dir", str(build), "-V"], mode + "-regression")
        for variant in (("baseline", "corrected") if mode == "debug" else ("corrected",)):
            executable = out / (mode + "-" + variant)
            command = ["g++", "-std=c++17", "-D__EXPORT=", "-Wall", "-Wextra", "-Werror",
                       "-Wno-unused-parameter", *flags, *link]
            for include in (ROOT / "src", ROOT / "src/lib", ROOT / "src/lib/matrix",
                            ROOT / "platforms/common", ROOT / "platforms/ros2/include",
                            build / "generated", out / variant):
                command.extend(["-I", str(include)])
            command += [str(fixture), str(build / "libresearch_stabilized_manual.a"), "-o", str(executable)]
            run(command, mode + "-" + variant + "-compile")
            results[mode + "-" + variant] = run(
                [str(executable)], mode + "-" + variant + "-test", 1 if variant == "baseline" else 0).splitlines()[-1]
    assert all(digest(ROOT / p) == sha for p, sha in bindings.items()), "Sources changed during verification"
    record = {
        "scope": "verbatim output/register methods; injected assignments and transport, not UART or hardware",
        "hardware_operated": False, "firmware_linked": False,
        "baseline_ref": args.baseline_ref,
        "source_bindings": bindings, "results": results,
        "files": {str(p.relative_to(out)): digest(p) for p in out.rglob("*")
                  if p.is_file() and not any(part in ("debug", "release", "sanitize") for part in p.relative_to(out).parts)},
    }
    (out / "manifest.json").write_text(json.dumps(record, indent=2) + "\n")
    print(json.dumps({"evidence": str(out), "results": results}, indent=2))


if __name__ == "__main__":
    main()
