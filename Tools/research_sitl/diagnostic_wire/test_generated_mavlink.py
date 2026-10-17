#!/usr/bin/env python3
"""Offline C1E diagnostic MAVLink 2 generated-code conformance evidence.

Generic MAVLink delivery is not diagnostic admission, authentication, signing,
or native ingress. The frozen W1 codec separately enforces payload semantics.
"""

import argparse
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile

PIN_MAVLINK = "1b1e896aa3be0d591166e086e6b9d8613bd03716"
PIN_PYMAVLINK = "9e9a991731cc209ef9fc04006ff11721f0695726"
ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
MAVLINK = ROOT / "src/modules/mavlink/mavlink"
PYMAVLINK = MAVLINK / "pymavlink"
MAVGEN = PYMAVLINK / "tools/mavgen.py"
SCHEMA = PYMAVLINK / "generator/mavschema.xsd"
XML = HERE / "diagnostic.xml"
FIXTURE = HERE / "GeneratedMavlinkFixture.cpp"
CODEC = HERE / "codec.py"
IDS = {"RESEARCH_DIAGNOSTIC_REQUEST": 200010, "RESEARCH_DIAGNOSTIC_STATUS": 200011}
MINIMUM = {"RESEARCH_DIAGNOSTIC_REQUEST": 103, "RESEARCH_DIAGNOSTIC_STATUS": 132}
FULL = {"RESEARCH_DIAGNOSTIC_REQUEST": 108, "RESEARCH_DIAGNOSTIC_STATUS": 136}
REQUEST_Q = ("native_run_id", "owner_lifetime_id", "controller_instance_id", "source_id",
             "transport_session_id", "request_sequence", "expected_transition_epoch",
             "producer_monotonic_us", "producer_expiry_monotonic_us", "clock_mapping_epoch")
REQUEST_F = ("acceleration_x_frd_m_s2", "acceleration_y_frd_m_s2", "acceleration_z_frd_m_s2")
REQUEST_B = ("domain", "profile", "target_system", "target_component", "frame", "quantity",
             "reference_point", "flags")
STATUS_Q = ("native_run_id", "owner_lifetime_id", "controller_instance_id", "source_id",
            "transport_session_id", "request_sequence", "status_sequence", "transition_epoch",
            "source_sequence", "state_generation", "arrival_native_us", "evaluation_native_us",
            "source_checked_native_us", "clock_mapping_epoch")
STATUS_B = ("domain", "profile", "execution_origin", "outcome", "arm_state", "raw_nav_state",
            "research_mode_state", "selector_state", "estimator_valid", "clock_map_valid",
            "eligible_for_control", "flags", "target_system", "target_component")
FIELDS = {
    "RESEARCH_DIAGNOSTIC_REQUEST": (
        [("uint64_t", name, 8 * i) for i, name in enumerate(REQUEST_Q)]
        + [("float", name, 80 + 4 * i) for i, name in enumerate(REQUEST_F)]
        + [("uint32_t", "protocol_version", 92)]
        + [("uint8_t", name, 96 + i) for i, name in enumerate(REQUEST_B)]
        + [("uint8_t[4]", "reserved", 104)]),
    "RESEARCH_DIAGNOSTIC_STATUS": (
        [("uint64_t", name, 8 * i) for i, name in enumerate(STATUS_Q)]
        + [("uint32_t", "protocol_version", 112), ("uint16_t", "reason", 116)]
        + [("uint8_t", name, 118 + i) for i, name in enumerate(STATUS_B)]
        + [("uint8_t[4]", "reserved", 132)]),
}
# Independently transcribed W1 fixture bytes, not produced by the codec under test.
PAYLOADS = {
    "RESEARCH_DIAGNOSTIC_REQUEST": bytes.fromhex(
        "0b000000000000000c000000000000000d000000000000000e00000000000000"
        "0f00000000000000100000000000000011000000000000006400000000000000"
        "b40000000000000000000000000000000000c03f000010c00000003f00000100"
        "01012abf0101010000000000"),
    "RESEARCH_DIAGNOSTIC_STATUS": bytes.fromhex(
        "0b000000000000000c000000000000000d000000000000000e00000000000000"
        "0f00000000000000100000000000000015000000000000001100000000000000"
        "03000000000000000200000000000000e803000000000000ea03000000000000"
        "e903000000000000000000000000000000000100010001010101010300000000"
        "00002abf00000000"),
}
COUNTS = {"roundtrip_vectors": 0, "framing_rejections": 0,
          "wrapper_rejections": 0, "unknown_dispatch_rejections": 0,
          "semantic_rejections": 0}


def fail(message):
    raise RuntimeError(message)


def run(command, *, env=None):
    result = subprocess.run(command, text=True, capture_output=True, env=env, timeout=90)
    if result.returncode:
        fail("%s failed (%d):\n%s\n%s" % (" ".join(map(str, command)), result.returncode,
                                            result.stdout, result.stderr))
    if "validation will not be performed" in (result.stdout + result.stderr).lower():
        fail("generator skipped mandatory validation")
    return result.stdout


def pinned_clean(path, expected):
    git = ["git", "-c", "core.autocrlf=true", "-C", str(path)]
    if run(git + ["rev-parse", "HEAD"]).strip() != expected:
        fail("pinned revision mismatch: %s" % path)
    if run(git + ["status", "--porcelain"]).strip():
        fail("pinned tree is dirty: %s" % path)


def x25(data):
    value = 0xffff
    for byte in data:
        temporary = byte ^ (value & 0xff)
        temporary = (temporary ^ (temporary << 4)) & 0xff
        value = ((value >> 8) ^ (temporary << 8) ^ (temporary << 3) ^ (temporary >> 4)) & 0xffff
    return value


def crc_extra(name, fields):
    encoded = bytearray((name + " ").encode("ascii"))
    for field_type, field_name, _ in fields:
        base = field_type.split("[")[0]
        encoded.extend((base + " " + field_name + " ").encode("ascii"))
        if "[" in field_type:
            encoded.append(int(field_type[field_type.index("[") + 1:-1]))
    value = x25(encoded)
    return (value & 0xff) ^ (value >> 8)


def validate_xml():
    try:
        from lxml import etree
    except ImportError as error:
        fail("lxml is mandatory for XSD validation: %s" % error)
    if not all(path.is_file() for path in (XML, SCHEMA, MAVGEN, FIXTURE, CODEC)):
        fail("required diagnostic source is absent")
    document = etree.parse(str(XML))
    etree.XMLSchema(etree.parse(str(SCHEMA))).assertValid(document)
    messages = document.xpath("/mavlink/messages/message")
    if len(messages) != 2 or document.xpath("/mavlink/include") or document.xpath("//extensions"):
        fail("diagnostic XML must contain exactly two base messages")
    found = {}
    for message in messages:
        name, identifier = message.get("name"), int(message.get("id"))
        actual = [(field.get("type"), field.get("name")) for field in message.findall("field")]
        expected = [(field_type, field_name) for field_type, field_name, _ in FIELDS.get(name, ())]
        if IDS.get(name) != identifier or actual != expected:
            fail("XML differs from frozen layout: %s" % name)
        found[identifier] = name
    if found != {identifier: name for name, identifier in IDS.items()}:
        fail("XML IDs or names differ from frozen allocation")
    for path in MAVLINK.glob("message_definitions/v1.0/**/*.xml"):
        for message in etree.parse(str(path)).xpath("/mavlink/messages/message"):
            if int(message.get("id")) in found or message.get("name") in IDS:
                fail("pinned XML collision in %s" % path)


def generate(temp):
    environment = os.environ.copy()
    environment["PYTHONPATH"] = str(MAVLINK)
    origin = run([sys.executable, "-c", "import pymavlink; print(pymavlink.__file__)"],
                 env=environment).strip()
    if Path(origin).resolve() != (PYMAVLINK / "__init__.py").resolve():
        fail("pymavlink import escaped pinned tree")
    c_output, py_output = temp / "c", temp / "python"
    py_output.mkdir()
    run([sys.executable, str(MAVGEN), "--lang=C", "--wire-protocol=2.0",
         "--output", str(c_output), str(XML)], env=environment)
    run([sys.executable, str(MAVGEN), "--lang=Python3", "--wire-protocol=2.0",
         "--output", str(py_output / "diagnostic.py"), str(XML)], env=environment)
    header = c_output / "diagnostic/mavlink.h"
    modules = list(py_output.glob("*.py"))
    if not header.is_file() or len(modules) != 1:
        fail("generator output is missing or ambiguous")
    return c_output, modules[0]


def check_header(c_output):
    headers = "\n".join(path.read_text(encoding="utf-8") for path in c_output.rglob("mavlink_msg_*.h"))
    for name, identifier in IDS.items():
        required = ("MAVLINK_MSG_ID_%s %d" % (name, identifier),
                    "MAVLINK_MSG_ID_%s_LEN %d" % (name, FULL[name]),
                    "MAVLINK_MSG_ID_%s_MIN_LEN %d" % (name, FULL[name]),
                    "MAVLINK_MSG_ID_%s_CRC %d" % (name, crc_extra(name, FIELDS[name])))
        if any(item not in headers for item in required):
            fail("generated C metadata differs: %s" % name)


def compiler_flags(arguments):
    flags = ["-std=c++17", "-Wall", "-Wextra", "-Wpedantic", "-Werror"]
    flags += ["-O2"] if arguments.optimize else ["-O0", "-g"]
    if arguments.sanitize:
        flags += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                  "-fno-omit-frame-pointer", "-fno-pie", "-no-pie"]
    return flags


def compile_fixture(c_output, temp, arguments):
    compiler = shutil.which("g++")
    if compiler is None:
        fail("g++ is mandatory")
    assertions = ["#include <diagnostic/mavlink.h>", "#include <cstddef>", "#include <type_traits>"]
    for name, fields in FIELDS.items():
        structure = "mavlink_%s_t" % name.lower()
        for field_type, field_name, offset in fields:
            assertions.append("static_assert(offsetof(%s, %s) == %d);" % (structure, field_name, offset))
            assertions.append("static_assert(std::is_same<decltype(%s::%s), %s>::value);" %
                              (structure, field_name, field_type))
    assertions.append("int main() { return 0; }")
    source = temp / "layout.cpp"
    source.write_text("\n".join(assertions), encoding="utf-8")
    flags = compiler_flags(arguments)
    run([compiler, *flags, "-isystem", str(c_output), "-c", str(source), "-o", str(temp / "layout.o")])
    executable = temp / "fixture"
    run([compiler, *flags, "-isystem", str(c_output), str(FIXTURE), "-o", str(executable)])
    return executable


def load_generated(path):
    specification = importlib.util.spec_from_file_location("generated_diagnostic", path)
    module = importlib.util.module_from_spec(specification)
    specification.loader.exec_module(module)
    if Path(module.__file__).resolve() != path.resolve():
        fail("generated Python import location mismatch")
    return module


def load_codec():
    specification = importlib.util.spec_from_file_location("strict_diagnostic_codec", CODEC)
    module = importlib.util.module_from_spec(specification)
    specification.loader.exec_module(module)
    if Path(module.__file__).resolve() != CODEC.resolve():
        fail("strict codec import location mismatch")
    return module


def field_bytes(field_type, value):
    if "[" in field_type:
        return bytes(value)
    if field_type == "float":
        return struct.pack("<f", value)
    return value.to_bytes(int(re.search(r"\d+", field_type).group()) // 8, "little")


def python_frame(module, name, payload):
    values = []
    for field_type, _, offset in FIELDS[name]:
        size = 4 if field_type == "float" else int(re.search(r"\d+", field_type).group()) // 8
        if "[" in field_type:
            values.append(list(payload[offset:offset + int(field_type.split("[")[1][:-1])]))
        elif field_type == "float":
            values.append(struct.unpack("<f", payload[offset:offset + size])[0])
        else:
            values.append(int.from_bytes(payload[offset:offset + size], "little"))
    stream = io.BytesIO()
    mav = module.MAVLink(stream, srcSystem=42, srcComponent=191)
    mav.seq = 17
    mav.send(getattr(module, "MAVLink_%s_message" % name.lower())(*values))
    return stream.getvalue()


def frame_for(identifier, payload, extra):
    header = bytes((0xfd, len(payload), 0, 0, 17, 42, 191)) + identifier.to_bytes(3, "little")
    checksum = x25(header[1:] + payload + bytes((extra,)))
    return header + payload + checksum.to_bytes(2, "little")


def canonical(name, payload):
    return frame_for(IDS[name], payload.rstrip(b"\0"), crc_extra(name, FIELDS[name]))


def python_parse(module, name, frame, payload):
    message = module.MAVLink(None).parse_char(frame)
    if message is None or message.get_msgId() != IDS[name] or message.get_type() != name:
        fail("generated Python did not deliver expected typed message")
    for field_type, field_name, offset in FIELDS[name]:
        expected = field_bytes(field_type, getattr(message, field_name))
        if expected != payload[offset:offset + len(expected)]:
            fail("generated Python decoded field bits differ: %s" % field_name)


def c_parse(executable, frame, name, payload):
    result = run([str(executable), "--parse", frame.hex()]).strip()
    if result != "%d %s" % (IDS[name], payload.hex()):
        fail("generated C parsed payload differs: %s" % name)


def roundtrip(executable, module, name, payload, incoming=None):
    expected = canonical(name, payload)
    if python_frame(module, name, payload) != expected:
        fail("generated Python frame differs from independent oracle")
    incoming = expected if incoming is None else incoming
    python_parse(module, name, incoming, payload)
    c_parse(executable, incoming, name, payload)
    if run([str(executable), "--roundtrip", incoming.hex()]).strip() != expected.hex():
        fail("generated C reencoded frame differs from independent oracle")
    COUNTS["roundtrip_vectors"] += 1


def reject(executable, module, frame, category="framing_rejections"):
    result = subprocess.run([str(executable), "--parse", frame.hex()], text=True,
                            capture_output=True, timeout=15)
    if result.returncode != 1 or result.stdout or result.stderr:
        fail("C malformed/unknown frame was not cleanly rejected")
    try:
        decoded = module.MAVLink(None).parse_char(frame)
    except module.MAVError:
        decoded = None
    if decoded is not None and decoded.get_msgId() in IDS.values():
        fail("Python delivered a diagnostic message for malformed framing")
    COUNTS[category] += 1


def reject_wrapper(executable, frame):
    result = subprocess.run([str(executable), "--parse", frame.hex()], text=True,
                            capture_output=True, timeout=15)
    if result.returncode != 1 or result.stdout or result.stderr:
        fail("C exact-frame wrapper did not reject invalid diagnostic input")
    COUNTS["wrapper_rejections"] += 1


def exercise(executable, module, codec):
    emitted = run([str(executable), "--emit"]).splitlines()
    if len(emitted) != 2 or any(not re.fullmatch("[0-9a-f]+", row) for row in emitted):
        fail("C emit protocol is malformed")
    for name, row in zip(IDS, emitted):
        payload = PAYLOADS[name]
        if len(payload) != FULL[name] or bytes.fromhex(row) != canonical(name, payload):
            fail("C generated frame differs from frozen full-frame oracle")
        roundtrip(executable, module, name, payload, bytes.fromhex(row))
        for length in range(MINIMUM[name], FULL[name] + 1):
            roundtrip(executable, module, name, payload,
                      frame_for(IDS[name], payload[:length], crc_extra(name, FIELDS[name])))
        invalid_crc = bytearray(bytes.fromhex(row))
        invalid_crc[-1] ^= 1
        reject(executable, module, bytes(invalid_crc))
        reject(executable, module, bytes.fromhex(row)[:-1])
        # Generated parsers can deliver a CRC-correct, under-minimum message.
        # The local exact-frame wrapper enforces the W1 logical minimum.
        reject_wrapper(executable,
                       frame_for(IDS[name], payload[:MINIMUM[name] - 1], crc_extra(name, FIELDS[name])))
        reject_wrapper(executable, bytes.fromhex(row) + b"\0")
        reject_wrapper(executable, bytes.fromhex(row) + bytes.fromhex(row))
        unsupported = bytearray(bytes.fromhex(row))
        unsupported[2] = 0x01
        reject_wrapper(executable, bytes(unsupported))
        unsupported[2] = 0
        unsupported[3] = 0x01
        reject_wrapper(executable, bytes(unsupported))
        # Both IDs exceed the MAVLink 1 message-ID width.
        parsed = module.MAVLink(None).parse_char(bytes.fromhex(row))
        try:
            parsed.pack(module.MAVLink(None), force_mavlink1=True)
        except (struct.error, ValueError, OverflowError):
            pass
        else:
            fail("diagnostic ID unexpectedly encoded in MAVLink 1")
    reject(executable, module, frame_for(200012, PAYLOADS[next(iter(IDS))][:103], 0),
           "unknown_dispatch_rejections")
    heartbeat_header = bytes((0xfe, 9, 17, 42, 191, 0))
    heartbeat = bytes(9)
    heartbeat_crc = x25(heartbeat_header[1:] + heartbeat + bytes((50,)))
    reject(executable, module, heartbeat_header + heartbeat + heartbeat_crc.to_bytes(2, "little"),
           "wrapper_rejections")

    request, status = IDS
    maximum = (1 << 64) - 1
    boundary_request = bytearray(PAYLOADS[request])
    for index in range(7):
        boundary_request[index * 8:index * 8 + 8] = maximum.to_bytes(8, "little")
    boundary_request[56:64] = (maximum - 1).to_bytes(8, "little")
    boundary_request[64:72] = maximum.to_bytes(8, "little")
    boundary_request[80:92] = struct.pack("<III", 0x80000000, 0x00000001, 0x7f7fffff)
    boundary_request[98:100] = b"\xff\xff"
    if codec.encode_request(codec.decode_request(bytes(boundary_request))) != boundary_request:
        fail("strict W1 request boundary vector did not preserve payload")
    roundtrip(executable, module, request, bytes(boundary_request))

    boundary_status = bytearray(PAYLOADS[status])
    for index in range(13):
        boundary_status[index * 8:index * 8 + 8] = maximum.to_bytes(8, "little")
    boundary_status[116:118] = (8).to_bytes(2, "little")
    boundary_status[120:122] = b"\x02\x02"  # Native POSIX origin, Closed outcome.
    boundary_status[122:124] = b"\x02\x1e"  # Armed, highest pinned nav value.
    boundary_status[130:132] = b"\xff\xff"
    if codec.encode_status(codec.decode_status(bytes(boundary_status))) != boundary_status:
        fail("strict W1 status boundary vector did not preserve payload")
    roundtrip(executable, module, status, bytes(boundary_status))

    # Each mutation has a valid diagnostic MAVLink CRC and must decode in both
    # generated bindings. Rejection belongs to the independent W1 codec only.
    mutations = (
        (request, 0, bytes(8), codec.decode_request, "native run identity"),
        (request, 40, bytes(8), codec.decode_request, "request sequence"),
        (request, 64, (100).to_bytes(8, "little"), codec.decode_request, "expiry"),
        (request, 80, struct.pack("<I", 0x7fc00000), codec.decode_request, "nonfinite demand"),
        (request, 84, struct.pack("<I", 0x7f800000), codec.decode_request, "infinite demand"),
        (request, 92, bytes(4), codec.decode_request, "protocol version"),
        (request, 96, b"\x02", codec.decode_request, "domain"),
        (request, 97, b"\x02", codec.decode_request, "profile"),
        (request, 98, b"\0", codec.decode_request, "target"),
        (request, 99, b"\0", codec.decode_request, "target component"),
        (request, 100, b"\x02", codec.decode_request, "frame"),
        (request, 101, b"\x02", codec.decode_request, "quantity"),
        (request, 102, b"\x02", codec.decode_request, "reference point"),
        (request, 103, b"\x01", codec.decode_request, "flags"),
        (request, 104, b"\x01", codec.decode_request, "reserved"),
        (request, 72, b"\x01", codec.decode_request, "clock map"),
        (status, 0, bytes(8), codec.decode_status, "native run identity"),
        (status, 48, bytes(8), codec.decode_status, "status sequence"),
        (status, 72, b"\x04", codec.decode_status, "generation exceeds source sequence"),
        (status, 80, (1003).to_bytes(8, "little"), codec.decode_status, "arrival after evaluation"),
        (status, 96, (1003).to_bytes(8, "little"), codec.decode_status, "source checked after evaluation"),
        (status, 104, b"\x01", codec.decode_status, "clock map epoch"),
        (status, 112, bytes(4), codec.decode_status, "protocol version"),
        (status, 116, bytes(2), codec.decode_status, "reason"),
        (status, 118, b"\x02", codec.decode_status, "domain"),
        (status, 119, b"\x02", codec.decode_status, "profile"),
        (status, 120, b"\x03", codec.decode_status, "execution origin"),
        (status, 121, b"\x02", codec.decode_status, "reason/outcome pair"),
        (status, 122, b"\x03", codec.decode_status, "arm state"),
        (status, 123, b"\x09", codec.decode_status, "reserved nav state"),
        (status, 124, b"\x01", codec.decode_status, "research mode"),
        (status, 125, b"\x01", codec.decode_status, "selector"),
        (status, 126, b"\x01", codec.decode_status, "estimator"),
        (status, 127, b"\x01", codec.decode_status, "clock map valid"),
        (status, 128, b"\x01", codec.decode_status, "eligible for control"),
        (status, 129, b"\x01", codec.decode_status, "flags"),
        (status, 130, b"\x00", codec.decode_status, "target system"),
        (status, 132, b"\x01", codec.decode_status, "reserved"),
    )
    for name, offset, replacement, decoder, label in mutations:
        payload = bytearray(PAYLOADS[name])
        payload[offset:offset + len(replacement)] = replacement
        roundtrip(executable, module, name, bytes(payload))
        try:
            decoder(bytes(payload))
        except codec.CodecError:
            COUNTS["semantic_rejections"] += 1
        else:
            fail("strict codec accepted correctly framed semantic negative: %s" % label)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--keep-dir", action="store_true")
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--optimize", action="store_true")
    arguments = parser.parse_args()
    if "MAV_IGNORE_CRC" in os.environ:
        fail("MAV_IGNORE_CRC environment setting invalidates CRC conformance")
    pinned_clean(MAVLINK, PIN_MAVLINK)
    pinned_clean(PYMAVLINK, PIN_PYMAVLINK)
    if x25(b"123456789") != 0x6f91:
        fail("independent X25 known-answer check failed")
    validate_xml()
    source_files = (XML, CODEC, FIXTURE, Path(__file__), SCHEMA, MAVGEN)
    source_hashes = {str(path): hashlib.sha256(path.read_bytes()).hexdigest()
                     for path in source_files}
    temp = Path(tempfile.mkdtemp(prefix="c1e_w2_mavlink_"))
    try:
        c_output, module_path = generate(temp)
        check_header(c_output)
        executable = compile_fixture(c_output, temp, arguments)
        exercise(executable, load_generated(module_path), load_codec())
        if any(hashlib.sha256(path.read_bytes()).hexdigest() != source_hashes[str(path)]
               for path in source_files):
            fail("conformance source changed during run")
        import lxml.etree
        report = {
            "result": "PASS", "pins": [PIN_MAVLINK, PIN_PYMAVLINK],
            "git_text_conversion": "command-local core.autocrlf=true",
            "python": sys.version.split()[0], "lxml": lxml.etree.LXML_VERSION,
            "compiler": run(["g++", "--version"]).splitlines()[0],
            "crc_extras": {name: crc_extra(name, FIELDS[name]) for name in IDS},
            "source_sha256": {path.name: source_hashes[str(path)] for path in source_files},
            "generated_python_sha256": hashlib.sha256(module_path.read_bytes()).hexdigest(),
            "counts": COUNTS, "flags": compiler_flags(arguments),
        }
        (temp / "report.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n",
                                          encoding="utf-8")
        print(json.dumps(report, sort_keys=True))
    finally:
        if arguments.keep_dir:
            print("temporary output retained: %s" % temp)
        else:
            shutil.rmtree(temp)  # Only this invocation's newly created directory.


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print("C1E-W2 FAIL: %s" % error, file=sys.stderr)
        raise SystemExit(1)
