#!/usr/bin/env python3
"""Fail-closed, temporary generated MAVLink-2 conformance harness.

The default is the offline C0c dialect; --production checks the production
dialect's generated bindings. Both are generated-code evidence only, not a
production parser, transport, authentication, adapter, or control path.
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
import subprocess
import struct
import sys
import tempfile

PIN_MAVLINK = "1b1e896aa3be0d591166e086e6b9d8613bd03716"
PIN_PYMAVLINK = "9e9a991731cc209ef9fc04006ff11721f0695726"
ROOT = Path(__file__).resolve().parents[3]
WIRE = Path(__file__).resolve().parent
MAVLINK = ROOT / "src/modules/mavlink/mavlink"
PYMAVLINK = MAVLINK / "pymavlink"
MAVGEN = PYMAVLINK / "tools/mavgen.py"
SCHEMA = PYMAVLINK / "generator/mavschema.xsd"
XML = WIRE / "research.xml"
PRODUCTION_XML = ROOT / "src/modules/mavlink/slaaps.xml"
PRODUCTION_INCLUDE = "mavlink/message_definitions/v1.0/development.xml"
FIXTURE = WIRE / "GeneratedMavlinkFixture.cpp"
IDS = {
    "RESEARCH_ACCELERATION_REQUEST": 200000,
    "RESEARCH_ACCELERATION_SESSION": 200001,
    "RESEARCH_ACCELERATION_STATUS": 200002,
}
PRODUCTION_IDS = {
    "RESEARCH_ACCELERATION_REQUEST": 200010,
    "RESEARCH_ACCELERATION_SESSION": 200011,
    "RESEARCH_ACCELERATION_STATUS": 200012,
}
INHERITED = {"HEARTBEAT": (0, 50), "TIMESYNC": (111, 34)}
PAYLOADS = {
    "RESEARCH_ACCELERATION_REQUEST": bytes.fromhex(
        "0100000000000000020000000000000003000000000000000400000000000000"
        "0500000000000000060000000000000007000000000000000800000000000000"
        "0000c03f000010c00000003f00000100090a0101010000000000"),
    "RESEARCH_ACCELERATION_SESSION": bytes.fromhex(
        "0b000000000000000c000000000000000d000000000000000e00000000000000"
        "0f00000000000000100000000000000011000000000000001200000000000000"
        "000001001314030000000000"),
    "RESEARCH_ACCELERATION_STATUS": bytes.fromhex(
        "1500000000000000160000000000000017000000000000001800000000000000"
        "19000000000000001a000000000000001b000000000000001c00000000000000"
        "1d000000000000001e000000000000001f000000000000000000010001000000"
        "3b002122010202020101"),
}
MINIMUM = {
    "RESEARCH_ACCELERATION_REQUEST": 85,
    "RESEARCH_ACCELERATION_SESSION": 71,
    "RESEARCH_ACCELERATION_STATUS": 106,
}
GENERATED_MINIMUM = {name: len(payload) for name, payload in PAYLOADS.items()}
FIELDS = {
    "RESEARCH_ACCELERATION_REQUEST": [
        ("uint64_t", "source_id", 0), ("uint64_t", "session_id", 8), ("uint64_t", "sequence", 16),
        ("uint64_t", "producer_monotonic_us", 24), ("uint64_t", "producer_expiry_monotonic_us", 32),
        ("uint64_t", "controller_instance_id", 40), ("uint64_t", "transition_epoch", 48),
        ("uint64_t", "clock_mapping_epoch", 56), ("float", "acceleration_x_frd_m_s2", 64),
        ("float", "acceleration_y_frd_m_s2", 68), ("float", "acceleration_z_frd_m_s2", 72),
        ("uint32_t", "protocol_version", 76), ("uint8_t", "target_system", 80),
        ("uint8_t", "target_component", 81), ("uint8_t", "frame", 82), ("uint8_t", "quantity", 83),
        ("uint8_t", "reference_point", 84), ("uint8_t", "flags", 85), ("uint8_t[4]", "reserved", 86),
    ],
    "RESEARCH_ACCELERATION_SESSION": [
        ("uint64_t", "controller_instance_id", 0), ("uint64_t", "transition_epoch", 8),
        ("uint64_t", "clock_mapping_epoch", 16), ("uint64_t", "source_id", 24),
        ("uint64_t", "session_id", 32), ("uint64_t", "operation_sequence", 40),
        ("uint64_t", "producer_acknowledgement_monotonic_us", 48),
        ("uint64_t", "acknowledged_status_sequence", 56), ("uint32_t", "protocol_version", 64),
        ("uint8_t", "target_system", 68), ("uint8_t", "target_component", 69),
        ("uint8_t", "operation", 70), ("uint8_t", "flags", 71), ("uint8_t[4]", "reserved", 72),
    ],
    "RESEARCH_ACCELERATION_STATUS": [
        *((("uint64_t", name, index * 8) for index, name in enumerate((
            "controller_instance_id", "transition_epoch", "clock_mapping_epoch", "source_id", "session_id",
            "correlation_sequence", "last_accepted_sequence", "validator_time_us", "status_sequence",
            "rejection_count", "conservative_age_us")))),
        ("uint32_t", "protocol_version", 88), ("uint16_t", "decision_code", 92),
        ("uint16_t", "first_latched_code", 94), ("uint16_t", "status_flags", 96),
        ("uint8_t", "target_system", 98), ("uint8_t", "target_component", 99),
        ("uint8_t", "status_class", 100), ("uint8_t", "arm_state", 101),
        ("uint8_t", "research_mode_state", 102), ("uint8_t", "session_state", 103),
        ("uint8_t", "transport_state", 104), ("uint8_t", "clock_mapping_state", 105),
    ],
}

REQUEST_NAME = "RESEARCH_ACCELERATION_REQUEST"
ATTITUDE_EXTENSION = ("float[4]", "desired_attitude_ned_from_body", 90)


def layout_fields(name):
    return FIELDS[name] + ([ATTITUDE_EXTENSION] if XML == PRODUCTION_XML and name == REQUEST_NAME else [])


def payload_size(name):
    return len(PAYLOADS[name]) + (16 if XML == PRODUCTION_XML and name == REQUEST_NAME else 0)


def fail(message):
    raise RuntimeError(message)


def run(arguments, *, env=None, input_text=None):
    result = subprocess.run(arguments, text=True, input=input_text, capture_output=True, env=env, timeout=60)
    if result.returncode:
        fail("%s failed (%d):\n%s\n%s" % (" ".join(map(str, arguments)), result.returncode,
                                            result.stdout, result.stderr))
    if "validation will not be performed" in (result.stdout + result.stderr).lower():
        fail("generator attempted to skip mandatory validation")
    return result.stdout


def git_clean(path, expected):
    if run(["git", "-c", "core.autocrlf=true", "-C", path, "rev-parse", "HEAD"]).strip() != expected:
        fail("pinned revision mismatch: %s" % path)
    if run(["git", "-c", "core.autocrlf=true", "-C", path, "status", "--porcelain"]).strip():
        fail("pinned source is dirty: %s" % path)


def x25(data):
    value = 0xffff
    for byte in data:
        temporary = byte ^ (value & 0xff)
        temporary = (temporary ^ (temporary << 4)) & 0xff
        value = ((value >> 8) ^ (temporary << 8) ^ (temporary << 3) ^ (temporary >> 4)) & 0xffff
    return value


def crc_extra(name, fields):
    encoded = bytearray((name + " ").encode())
    for field_type, field_name, _ in fields:
        base = field_type.split("[")[0]
        encoded.extend((base + " " + field_name + " ").encode())
        if "[" in field_type:
            encoded.append(int(field_type[field_type.index("[") + 1:-1]))
    value = x25(encoded)
    return (value & 0xff) ^ (value >> 8)


def validate_and_scan(production):
    try:
        from lxml import etree
    except ImportError as error:
        fail("lxml is mandatory for schema validation: %s" % error)
    if not all(path.is_file() for path in (XML, SCHEMA, MAVGEN, FIXTURE)):
        fail("required generated-wire source is absent")
    schema = etree.XMLSchema(etree.parse(str(SCHEMA)))
    document = etree.parse(str(XML))
    schema.assertValid(document)
    messages = document.xpath("/mavlink/messages/message")
    includes = document.xpath("/mavlink/include/text()")
    extensions = document.xpath("//extensions")
    if (len(messages) != 3 or includes != ([PRODUCTION_INCLUDE] if production else [])
            or len(extensions) != (1 if production else 0)
            or (production and extensions[0].getparent().get("name") != REQUEST_NAME)):
        fail("dialect must retain three frozen base messages and only its declared request extension")
    if production:
        included = (XML.parent / PRODUCTION_INCLUDE).resolve()
        if included != (MAVLINK / "message_definitions/v1.0/development.xml").resolve() or not included.is_file():
            fail("production include escaped pinned development.xml")
    found = {}
    for message in messages:
        name, identifier = message.get("name"), int(message.get("id"))
        actual = [(field.get("type"), field.get("name")) for field in message.findall("field")]
        expected = [(field_type, field_name) for field_type, field_name, _ in layout_fields(name)]
        if IDS.get(name) != identifier or actual != expected:
            fail("XML layout differs from frozen table: %s" % name)
        if production and name == REQUEST_NAME and message.index(message.find("extensions")) != 20:
            fail("attitude extension must follow all 19 frozen v1 fields and description")
        found[identifier] = name
    if found != {value: key for key, value in IDS.items()}:
        fail("research XML IDs/names do not match frozen allocation")
    for path in MAVLINK.glob("message_definitions/v1.0/**/*.xml"):
        root = etree.parse(str(path))
        for message in root.xpath("/mavlink/messages/message"):
            if int(message.get("id")) in found or message.get("name") in IDS:
                fail("pinned XML collision in %s" % path)


def header_frame(frame, extra):
    if len(frame) < 12 or frame[0] != 0xfd or frame[2:4] != b"\0\0":
        fail("not an unsigned MAVLink 2 frame")
    length = frame[1]
    if len(frame) != length + 12:
        fail("frame length mismatch")
    if x25(frame[1:10 + length] + bytes((extra,))) != int.from_bytes(frame[-2:], "little"):
        fail("frame X25 checksum mismatch")


def generate(temp):
    environment = os.environ.copy()
    environment["PYTHONPATH"] = str(MAVLINK)
    origin = run([sys.executable, "-c", "import pymavlink; print(pymavlink.__file__)"], env=environment).strip()
    if Path(origin).resolve() != (PYMAVLINK / "__init__.py").resolve():
        fail("generator import escaped pinned MAVLink tree")
    c_output, py_output = temp / "c", temp / "python"
    py_output.mkdir()
    run([sys.executable, MAVGEN, "--lang=C", "--wire-protocol=2.0", "--output", c_output, XML], env=environment)
    run([sys.executable, MAVGEN, "--lang=Python3", "--wire-protocol=2.0", "--output", py_output / (XML.stem + ".py"), XML],
        env=environment)
    header = c_output / XML.stem / "mavlink.h"
    if not header.is_file():
        fail("C generator did not create expected dialect header")
    module_files = list(py_output.glob("*.py"))
    if len(module_files) != 1:
        fail("Python generator output is ambiguous")
    return c_output, header, module_files[0]


def check_header(c_output):
    text = "\n".join(path.read_text(encoding="utf-8") for path in c_output.rglob("mavlink_msg_*.h"))
    for name, identifier in IDS.items():
        upper = name
        length = payload_size(name)
        required = (
            ("MAVLINK_MSG_ID_%s %d" % (upper, identifier)),
            ("MAVLINK_MSG_ID_%s_LEN %d" % (upper, length)),
            ("MAVLINK_MSG_ID_%s_MIN_LEN %d" % (upper, GENERATED_MINIMUM[name])),
            ("MAVLINK_MSG_ID_%s_CRC %d" % (upper, crc_extra(upper, FIELDS[upper]))),
        )
        if any(item not in text for item in required):
            fail("generated C metadata mismatch: %s" % name)
    if XML == PRODUCTION_XML:
        for name, (identifier, extra) in INHERITED.items():
            for suffix, value in (("", identifier), ("_CRC", extra)):
                if not re.search(r"^#define MAVLINK_MSG_ID_%s%s %d$" % (name, suffix, value), text, re.MULTILINE):
                    fail("inherited C metadata differs from pinned standard definition: " + name)


def compiler_flags(arguments):
    flags = ["-std=c++17", "-Wall", "-Wextra", "-Wpedantic", "-Werror"]
    flags += ["-O2"] if arguments.optimize else ["-O0", "-g"]
    if arguments.sanitize:
        flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie"]
    return flags


def check_offsets(c_output, temp, arguments):
    compiler = shutil.which("g++")
    if compiler is None:
        fail("g++ is mandatory for generated layout validation")
    assertions = ["#include <%s/mavlink.h>" % XML.stem, "#include <cstddef>", "#include <type_traits>"]
    for name in FIELDS:
        fields = layout_fields(name)
        struct_name = "mavlink_%s_t" % name.lower()
        for field_type, field_name, offset in fields:
            assertions.append("static_assert(offsetof(%s, %s) == %d);" % (struct_name, field_name, offset))
            assertions.append("static_assert(std::is_same<decltype(%s::%s), %s>::value);" %
                              (struct_name, field_name, field_type))
    assertions.append("int main() { return 0; }")
    source = temp / "layout.cpp"
    source.write_text("\n".join(assertions), encoding="utf-8")
    run([compiler, *compiler_flags(arguments), "-isystem", c_output, "-c", source, "-o", temp / "layout.o"])


def compile_fixture(c_output, temp, arguments):
    compiler = shutil.which("g++")
    if compiler is None:
        fail("g++ is mandatory for generated-wire conformance")
    executable = temp / "fixture"
    run([compiler, *compiler_flags(arguments), "-isystem", c_output,
         "-DGENERATED_MAVLINK_HEADER=<%s/mavlink.h>" % XML.stem,
         "-DGENERATED_MAVLINK_REQUEST_ID=%d" % IDS["RESEARCH_ACCELERATION_REQUEST"],
         FIXTURE, "-o", executable])
    return executable


def emitted_frames(executable):
    rows = run([executable, "--emit"]).splitlines()
    if len(rows) != 3 or any(not re.fullmatch("[0-9a-f]+", row) for row in rows):
        fail("fixture emit protocol is malformed")
    return [bytes.fromhex(row) for row in rows]


def load_generated(module_path):
    specification = importlib.util.spec_from_file_location("generated_" + XML.stem, module_path)
    module = importlib.util.module_from_spec(specification)
    specification.loader.exec_module(module)
    if Path(module.__file__).resolve() != module_path.resolve():
        fail("generated import location mismatch")
    return module


def python_frame(module, name, payload):
    fields = layout_fields(name)
    payload = payload.ljust(payload_size(name), b"\0")
    values = []
    for field_type, _, offset in fields:
        width = 4 if field_type.startswith("float") else int(re.search(r"\d+", field_type).group()) // 8
        count = int(field_type[field_type.index("[") + 1:-1]) if "[" in field_type else 1
        raw = payload[offset:offset + width * count]
        if count != 1:
            values.append(list(struct.unpack("<%df" % count, raw)) if field_type.startswith("float") else list(raw))
        elif field_type == "float":
            import struct
            values.append(struct.unpack("<f", raw)[0])
        else:
            values.append(int.from_bytes(raw, "little"))
    stream = io.BytesIO()
    mav = module.MAVLink(stream, srcSystem=42, srcComponent=191)
    mav.seq = 17
    constructor = getattr(module, "MAVLink_%s_message" % name.lower())
    mav.send(constructor(*values))
    return stream.getvalue()


def c_parse(executable, frame, expected_id, expected_payload):
    output = run([executable, "--parse", frame.hex()]).strip()
    name = next(name for name, identifier in IDS.items() if identifier == expected_id)
    expected_payload = expected_payload.ljust(payload_size(name), b"\0")
    expected = "%d %s" % (expected_id, expected_payload.hex())
    if output != expected:
        fail("C parse/reencode mismatch: %r != %r" % (output, expected))


def c_roundtrip(executable, frame, expected):
    output = run([executable, "--roundtrip", frame.hex()]).strip()
    if output != expected.hex():
        fail("C generated roundtrip frame mismatch")


def frame_for(identifier, payload, extra):
    header = bytes((0xfd, len(payload), 0, 0, 17, 42, 191)) + identifier.to_bytes(3, "little")
    checksum = x25(header[1:] + payload + bytes((extra,)))
    return header + payload + checksum.to_bytes(2, "little")


def canonical_frame(name, payload):
    return frame_for(IDS[name], payload.rstrip(b"\0"), crc_extra(name, FIELDS[name]))


def python_parse(module, name, frame, payload):
    parser = module.MAVLink(None)
    message = parser.parse_char(bytes(frame))
    if message is None or message.get_msgId() != IDS[name] or message.get_type() != name:
        fail("Python generated parser did not deliver expected message")
    payload = payload.ljust(payload_size(name), b"\0")
    for field_type, field_name, offset in layout_fields(name):
        width = 4 if field_type.startswith("float") else int(re.search(r"\d+", field_type).group()) // 8
        count = int(field_type[field_type.index("[") + 1:-1]) if "[" in field_type else 1
        raw = payload[offset:offset + width * count]
        actual = getattr(message, field_name)
        if count != 1:
            actual_bytes = struct.pack("<%df" % count, *actual) if field_type.startswith("float") else bytes(actual)
        elif field_type == "float":
            actual_bytes = struct.pack("<f", actual)
        else:
            actual_bytes = actual.to_bytes(width, "little")
        if actual_bytes != raw:
            fail("Python decoded field bits mismatch: " + field_name)


COUNTS = {"roundtrip_vectors": 0, "framing_rejections": 0, "semantic_rejections": 0,
          "v2_extension_vectors": 0, "request_length_rejections": 0}


def roundtrip(executable, module, name, payload, incoming=None):
    expected = canonical_frame(name, payload)
    generated = python_frame(module, name, payload)
    if generated != expected:
        fail("Python frame differs from independent complete frame oracle")
    incoming = generated if incoming is None else incoming
    python_parse(module, name, incoming, payload)
    c_parse(executable, incoming, IDS[name], payload)
    c_roundtrip(executable, incoming, expected)
    COUNTS["roundtrip_vectors"] += 1


def reject(executable, module, frame):
    result = subprocess.run([executable, "--parse", frame.hex()], text=True, capture_output=True, timeout=15)
    if result.returncode != 1 or result.stdout or result.stderr:
        fail("C rejection was not a clean no-delivery result")
    # Python can return UNKNOWN for unknown IDs; never dispatch it as research.
    try:
        decoded = module.MAVLink(None).parse_char(bytes(frame))
    except module.MAVError:
        decoded = None
    if decoded is not None and decoded.get_msgId() in IDS.values():
        fail("Python delivered a known message for malformed framing")
    COUNTS["framing_rejections"] += 1


def exercise_full_commands(executable, module):
    if XML != PRODUCTION_XML:
        return
    from codec import CodecError, decode_request
    base = bytearray(PAYLOADS[REQUEST_NAME])
    base[76:80] = (0x00020000).to_bytes(4, "little")
    base[85] = 1
    extra = crc_extra(REQUEST_NAME, FIELDS[REQUEST_NAME])
    for attitude in ((1., 0., 0., 0.), (-1., 0., 0., 0.),
                     (0., 1., 0., 0.), (0., 0., 0., -1.)):
        payload = bytes(base) + struct.pack("<4f", *attitude)
        roundtrip(executable, module, REQUEST_NAME, payload)
        # Generated getters must zero-fill the omitted extension tail; physical
        # validity is tested by the shared canonicalizer/adapter, not framing.
        for length in range(len(payload.rstrip(b"\0")), len(payload) + 1):
            incoming = frame_for(IDS[REQUEST_NAME], payload[:length], extra)
            roundtrip(executable, module, REQUEST_NAME, payload, incoming)
        try:
            decode_request(payload)
        except CodecError:
            pass
        else:
            fail("v1 codec silently accepted a full v2 command")
        COUNTS["v2_extension_vectors"] += 1
    # A correctly framed extension never changes the v1 semantic codec. The
    # production adapter independently enforces version/flags/reserved/norm.
    for payload in (bytes(base), PAYLOADS[REQUEST_NAME] + struct.pack("<4f", 1., 0., 0., 0.)):
        roundtrip(executable, module, REQUEST_NAME, payload)
        try:
            decode_request(payload)
        except CodecError:
            pass
        else:
            fail("v1 codec accepted a v2 discriminator or nonzero attitude tail")
        COUNTS["semantic_rejections"] += 1
    for payload in (bytes(base[:84]), bytes(base) + bytes(17)):
        frame = frame_for(IDS[REQUEST_NAME], payload, extra)
        result = subprocess.run([executable, "--parse", frame.hex()], text=True, capture_output=True, timeout=15)
        if result.returncode != 1 or result.stdout or result.stderr:
            fail("strict generated fixture accepted an out-of-contract request length")
        COUNTS["request_length_rejections"] += 1


def exercise(executable, module):
    names = list(IDS)
    for name, frame in zip(names, emitted_frames(executable)):
        payload = PAYLOADS[name]
        if frame != canonical_frame(name, payload):
            fail("C generated frame differs from independent complete frame oracle")
        roundtrip(executable, module, name, payload, frame)
        extra = crc_extra(name, FIELDS[name])
        for length in range(MINIMUM[name], len(payload) + 1):
            partial = frame_for(IDS[name], payload[:length], extra)
            roundtrip(executable, module, name, payload, partial)
        corrupt = bytearray(frame)
        corrupt[-1] ^= 1
        reject(executable, module, corrupt)
        reject(executable, module, frame[:-1])
        # Exercise the actual V1 pack path, not only a magic-byte mutation.
        message = module.MAVLink(None).parse_char(frame)
        try:
            message.pack(module.MAVLink(None), force_mavlink1=True)
        except (struct.error, ValueError, OverflowError):
            pass
        else:
            fail("high message ID unexpectedly packed in MAVLink 1")
    # For a missing ID, the generated C CRC-extra lookup is zero. This is a
    # CRC-correct unknown-ID fixture, not a corrupted-known-message false pass.
    unknown_id = max(IDS.values()) + 1
    if unknown_id in module.mavlink_map:
        fail("unknown-ID rejection fixture collides with an included message")
    reject(executable, module, frame_for(unknown_id, PAYLOADS[names[0]][:85], 0))
    header = bytes((0xfe, 9, 17, 42, 191, 0))
    heartbeat = bytes(9)
    checksum = x25(header[1:] + heartbeat + bytes((50,)))
    reject(executable, module, header + heartbeat + checksum.to_bytes(2, "little"))

    request = bytearray(PAYLOADS[names[0]])
    for offset in (0, 8, 16, 40, 48, 56):
        request[offset:offset + 8] = (2**64 - 1).to_bytes(8, "little")
    request[64:76] = struct.pack("<III", 0x80000000, 1, 0x7f7fffff)
    request[80:82] = bytes((255, 255))
    roundtrip(executable, module, names[0], bytes(request))

    from codec import CodecError, decode_request, decode_session, decode_status, encode_status
    mutations = (
        (names[0], 64, struct.pack("<I", 0x7fc00000), decode_request),
        (names[0], 76, bytes(4), decode_request),
        (names[0], 82, b"\0", decode_request),
        (names[0], 85, b"\1", decode_request),
        (names[1], 72, b"\1", decode_session),
        (names[2], 96, b"\x80", decode_status),
    )
    for name, offset, replacement, decoder in mutations:
        payload = bytearray(PAYLOADS[name])
        payload[offset:offset + len(replacement)] = replacement
        # Generic generated framing accepts these; semantic codec must reject.
        roundtrip(executable, module, name, bytes(payload))
        try:
            decoder(bytes(payload))
        except CodecError:
            pass
        else:
            fail("semantic codec accepted malformed but correctly framed payload")
        COUNTS["semantic_rejections"] += 1
    vectors = json.loads((WIRE / "status_vectors.json").read_text())
    if len(vectors) != 9:
        fail("expected all nine frozen status vectors")
    for vector in vectors:
        payload = encode_status(vector["fields"], trim=False)
        if "hex" in vector and payload.hex() != vector["hex"]:
            fail("status golden hex mismatch")
        roundtrip(executable, module, names[2], payload)
    exercise_full_commands(executable, module)


def main():
    global XML, IDS
    parser = argparse.ArgumentParser()
    parser.add_argument("--production", action="store_true",
                        help="check production slaaps.xml and pinned development.xml instead of offline research.xml")
    parser.add_argument("--keep-dir", action="store_true")
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--optimize", action="store_true")
    arguments = parser.parse_args()
    if arguments.production:
        XML, IDS = PRODUCTION_XML, PRODUCTION_IDS
    git_clean(MAVLINK, PIN_MAVLINK)
    git_clean(PYMAVLINK, PIN_PYMAVLINK)
    sources = [XML, FIXTURE, Path(__file__), WIRE / "status_vectors.json", SCHEMA, MAVGEN]
    if arguments.production:
        sources.extend(MAVLINK / "message_definitions/v1.0" / name
                       for name in ("development.xml", "common.xml", "minimal.xml"))
    source_sha256 = {path.name: hashlib.sha256(path.read_bytes()).hexdigest() for path in sources}
    if x25(b"123456789") != 0x6f91:
        fail("independent X25 known-answer check failed")
    validate_and_scan(arguments.production)
    temp = Path(tempfile.mkdtemp(prefix="slaaps_mavlink_" if arguments.production else "c0c_mavlink_"))
    try:
        c_output, header, module_path = generate(temp)
        check_header(c_output)
        check_offsets(c_output, temp, arguments)
        executable = compile_fixture(c_output, temp, arguments)
        module = load_generated(module_path)
        if arguments.production:
            for name, expected in INHERITED.items():
                message = getattr(module, "MAVLink_%s_message" % name.lower())
                if (message.id, message.crc_extra) != expected:
                    fail("inherited Python metadata differs from pinned standard definition: " + name)
        exercise(executable, module)
        if source_sha256 != {path.name: hashlib.sha256(path.read_bytes()).hexdigest() for path in sources}:
            fail("source changed during generated-wire conformance")
        import lxml.etree
        report = {
            "result": "PASS", "pins": [PIN_MAVLINK, PIN_PYMAVLINK],
            "profile": "production" if arguments.production else "offline",
            "dialect": str(XML.relative_to(ROOT)), "message_ids": IDS,
            "includes": [PRODUCTION_INCLUDE] if arguments.production else [],
            "inherited_message_metadata": {name: {"id": values[0], "crc_extra": values[1]}
                                           for name, values in INHERITED.items()} if arguments.production else {},
            "scope": "generated_C_Python_layout_framing_and_payload_conformance_only",
            "runtime_ingress_verified": False, "authentication_verified": False,
            "control_verified": False, "physical_outputs": False,
            "git_text_conversion": "command-local core.autocrlf=true",
            "python": sys.version.split()[0], "lxml": lxml.etree.LXML_VERSION,
            "compiler": run(["g++", "--version"]).splitlines()[0],
            "crc_extras": {name: crc_extra(name, FIELDS[name]) for name in IDS},
            "source_sha256": source_sha256,
            "generated_python_sha256": hashlib.sha256(module_path.read_bytes()).hexdigest(),
            "generated_c_header_sha256": hashlib.sha256(header.read_bytes()).hexdigest(),
            "counts": COUNTS, "flags": compiler_flags(arguments),
        }
        (temp / "result.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
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
        print("C0c FAIL: %s" % error, file=sys.stderr)
        raise SystemExit(1)
