#!/usr/bin/env python3
"""Compile and exercise the private C1E parser with independent signed frames.

This is an offline parser contract check. It does not start PX4 or qualify any
native owner, physical source, endpoint, estimator, or control behavior.
"""

from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import subprocess
import tempfile

import independent_vectors as vectors


HERE = Path(__file__).resolve().parent
WIRE = HERE.parent
PARSER = WIRE / "native_parser"
STATUS_ID = 200011
STATUS_CRC_EXTRA = 159


def run(command: list[str]) -> str:
    result = subprocess.run(command, text=True, capture_output=True, timeout=120)
    if result.returncode:
        raise RuntimeError(
            f"{' '.join(command)} failed ({result.returncode}):\n"
            f"{result.stdout}\n{result.stderr}"
        )
    return result.stdout


def verify_status(hex_frame: str) -> int:
    packet = bytes.fromhex(hex_frame)
    if packet[0] != 0xFD or packet[2:4] != b"\x01\x00":
        raise AssertionError("signed status envelope")
    payload_length = packet[1]
    if not 132 <= payload_length <= 136 or len(packet) != payload_length + 25:
        raise AssertionError("status length")
    if packet[5:7] != b"\x01\x01" or int.from_bytes(packet[7:10], "little") != STATUS_ID:
        raise AssertionError("status sender or message ID")
    payload = packet[10:10 + payload_length]
    checksum = packet[10 + payload_length:12 + payload_length]
    if checksum != vectors.x25(packet[1:10] + payload + bytes((STATUS_CRC_EXTRA,))).to_bytes(2, "little"):
        raise AssertionError("independent status CRC")
    signature = packet[-13:]
    if signature[0] != 8:
        raise AssertionError("outgoing link")
    expected = hashlib.sha256(vectors.KEY + packet[:-13] + signature[:7]).digest()[:6]
    if signature[7:] != expected:
        raise AssertionError("independent status signature")
    padded = payload + bytes(136 - payload_length)
    if [int.from_bytes(padded[index:index + 8], "little") for index in (0, 8, 16, 24, 32, 40)] != [11, 12, 13, 14, 15, 16]:
        raise AssertionError("status identities")
    if int.from_bytes(padded[116:118], "little") != 4 or padded[118:121] != bytes((1, 1, 2)):
        raise AssertionError("actual rejection or origin")
    if padded[121] != 1 or any(padded[index] for index in range(124, 130)):
        raise AssertionError("status outcome or authority fields")
    if padded[130:132] != bytes((42, 191)):
        raise AssertionError("status target")
    return int.from_bytes(signature[1:7], "little")


def main() -> None:
    argument_parser = argparse.ArgumentParser(description=__doc__)
    argument_parser.add_argument("--optimize", action="store_true")
    argument_parser.add_argument("--sanitize", action="store_true")
    args = argument_parser.parse_args()
    vectors.wire_fixture.validate_xml()
    with tempfile.TemporaryDirectory(prefix="c1e_parser_", dir=os.environ.get("TMPDIR")) as directory:
        temporary = Path(directory)
        generated_c, _ = vectors.wire_fixture.generate(temporary)
        vectors.write_cpp_header(temporary / "SignedVectors.hpp")
        flags = ["-std=c++17", "-Wall", "-Wextra", "-Wpedantic", "-Werror"]
        flags += ["-O2"] if args.optimize else ["-O0", "-g"]
        if args.sanitize:
            flags += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                      "-fno-omit-frame-pointer", "-fno-pie", "-no-pie"]
        executable = temporary / "parser_test"
        run(["g++", *flags, "-isystem", str(generated_c), "-I", str(PARSER),
             "-I", str(temporary), str(HERE / "DiagnosticSignedMavlinkTest.cpp"),
             str(PARSER / "DiagnosticSignedMavlink.cpp"), "-o", str(executable)])
        output = run([str(executable)])
        statuses = [line.split(" ", 1)[1] for line in output.splitlines() if line.startswith("STATUS ")]
        passes = [line for line in output.splitlines() if line.startswith("PASS ")]
        if len(statuses) != 2 or len(passes) != 1:
            raise AssertionError(f"fixture output incomplete: {output}")
        timestamps = [verify_status(value) for value in statuses]
        if timestamps[0] < vectors.TIMESTAMP + 5 or timestamps[1] <= timestamps[0]:
            raise AssertionError("outgoing signing clock failed monotonicity")
        print(f"{passes[0]}; 2 independently checked signed statuses")


if __name__ == "__main__":
    main()
