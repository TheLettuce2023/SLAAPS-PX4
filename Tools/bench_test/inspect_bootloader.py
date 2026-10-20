#!/usr/bin/env python3
"""Read a V6X ArduPilot bootloader's stored vectors and application-slot CRC.

Run after a complete power cycle, with QGC closed and the failed image still
installed. Only GET_SYNC, GET_DEVICE, READ_MULTI, GET_CRC and GET_VERSION are
implemented. This tool does not enter the bootloader, finalize an upload or boot
the application. Identification may hold the bootloader until another power cycle.
"""

import argparse
import base64
import datetime
import hashlib
import json
from pathlib import Path
import struct
import sys
import time
import zlib

ROOT = Path(__file__).resolve().parents[2]
STAGE = ROOT / "build/bench_commissioning/stock-e880f1cd79-diagnostic"
EXPECTED_BOARD = 53
EXPECTED_SLOT = 1966080
BOOTLOADER_USB_ID = (0x1209, 0x5741)
GET_SYNC, GET_DEVICE, READ_MULTI, GET_CRC, GET_VERSION = 0x21, 0x22, 0x28, 0x29, 0x2F
OK = b"\x12\x10"
INVALID = b"\x12\x13"
VECTOR_BYTES = 32  # Length byte is EOC, harmless even if READ_MULTI is unsupported.


class InspectionError(Exception):
    pass


class UnsupportedCommand(InspectionError):
    pass


def slot_crc(image, size):
    """Match the bootloader/uploader CRC: initial zero, no final complement."""
    if len(image) > size:
        raise InspectionError("Image exceeds application slot")
    state = zlib.crc32(image, 0xFFFFFFFF)
    state = zlib.crc32(b"\xff" * (size - len(image)), state)
    return (state ^ 0xFFFFFFFF) & 0xFFFFFFFF


def load_candidate(path=None):
    expected_hash = None
    if path is None:
        receipt = json.loads((STAGE / "comparison-receipt.json").read_text())
        artifact = receipt["artifacts"]["px4"]
        path = STAGE / artifact["path"]
        expected_hash = artifact["sha256"]
    path = Path(path).resolve()
    data = path.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if expected_hash and digest != expected_hash:
        raise InspectionError("Stock package differs from its retained receipt")
    package = json.loads(data)
    if package["board_id"] != EXPECTED_BOARD or package["image_maxsize"] != EXPECTED_SLOT:
        raise InspectionError("Package is not the expected V6X board/flash layout")
    image = zlib.decompress(base64.b64decode(package["image"], validate=True))
    if len(image) != package["image_size"] or not VECTOR_BYTES <= len(image) <= EXPECTED_SLOT:
        raise InspectionError("Invalid decoded image size")
    return image, {"path": str(path), "sha256": digest, "image_bytes": len(image)}


class Reader:
    def __init__(self, port):
        self.port = port
        self.commands = []

    def send(self, opcode, arguments=b""):
        allowed = (
            opcode in (GET_SYNC, GET_CRC, GET_VERSION) and not arguments
            or opcode == GET_DEVICE and arguments in (b"\x01", b"\x02", b"\x03", b"\x04")
            or opcode == READ_MULTI and arguments == bytes([VECTOR_BYTES])
        )
        if not allowed:
            raise InspectionError("Command is outside the diagnostic query allowlist")
        frame = bytes([opcode]) + arguments + b"\x20"
        if self.port.write(frame) != len(frame):
            raise InspectionError("Incomplete serial write")
        self.port.flush()
        self.commands.append(frame.hex())

    def read_exact(self, count):
        data = bytearray()
        while len(data) < count:
            chunk = self.port.read(count - len(data))
            if not chunk:
                if data == INVALID:
                    raise UnsupportedCommand("Bootloader rejected the query")
                raise InspectionError(f"Truncated reply: expected {count} bytes, received {len(data)}")
            data.extend(chunk)
        return bytes(data)

    def query(self, opcode, arguments=b"", response_size=0):
        self.send(opcode, arguments)
        reply = self.read_exact(response_size + 2)
        if reply[-2:] != OK:
            raise InspectionError("Invalid bootloader response status")
        return reply[:-2]

    def device_info(self, key):
        return struct.unpack("<I", self.query(GET_DEVICE, bytes([key]), 4))[0]

    def version(self):
        self.send(GET_VERSION)
        length = struct.unpack("<I", self.read_exact(4))[0]
        if not 0 < length <= 256:
            raise InspectionError("Invalid bootloader version length")
        reply = self.read_exact(length + 2)
        if reply[-2:] != OK:
            raise InspectionError("Invalid bootloader version status")
        return reply[:-2].decode("utf-8", errors="replace").rstrip("\0")


def inspect(reader, image):
    reader.query(GET_SYNC)
    protocol = reader.device_info(1)
    board = reader.device_info(2)
    if protocol != 5 or board != EXPECTED_BOARD:
        raise InspectionError(f"Unexpected bootloader protocol/board: {protocol}/{board}")
    revision = reader.device_info(3)
    size = reader.device_info(4)  # ArduPilot also resets its read pointer to zero here.
    if size != EXPECTED_SLOT:
        raise InspectionError(f"Unexpected application-slot size: {size}")
    vectors_error = None
    try:
        vectors = reader.query(READ_MULTI, bytes([VECTOR_BYTES]), VECTOR_BYTES)
    except UnsupportedCommand as error:
        vectors = None
        vectors_error = str(error)
        reader.query(GET_SYNC)
    actual_crc = struct.unpack("<I", reader.query(GET_CRC, response_size=4))[0]
    expected_crc = slot_crc(image, size)
    erased_lead_crc = slot_crc(b"\xff" * 32 + image[32:], size)
    prefix_matches = vectors == image[:VECTOR_BYTES] if vectors is not None else None
    lead_erased = vectors[:32] == b"\xff" * 32 if vectors is not None else None
    if prefix_matches and actual_crc == expected_crc:
        outcome = "candidate_vectors_and_slot_crc_match"
    elif lead_erased and actual_crc == erased_lead_crc:
        outcome = "candidate_body_matches_but_first_32_bytes_are_erased"
    elif lead_erased:
        outcome = "first_32_bytes_are_erased_flash_contents_not_fully_matched"
    elif vectors is None and actual_crc == expected_crc:
        outcome = "slot_crc_matches_vectors_unavailable"
    else:
        outcome = "flash_readback_does_not_match_candidate"
    try:
        version = reader.version()
        version_error = None
    except InspectionError as error:
        version, version_error = None, str(error)
    return {
        "status": "inspection_completed", "outcome": outcome,
        "bootloader_protocol": protocol, "bootloader_version_text": version,
        "bootloader_version_query_error": version_error,
        "board_id": board, "board_revision": revision, "flash_size": size,
        "expected_slot_crc": f"0x{expected_crc:08x}", "reported_slot_crc": f"0x{actual_crc:08x}",
        "slot_crc_matches": actual_crc == expected_crc,
        "expected_crc_if_first_32_erased": f"0x{erased_lead_crc:08x}",
        "vectors_match": prefix_matches, "first_32_bytes_erased": lead_erased,
        "vector_read_error": vectors_error,
        "expected_vectors": [f"0x{word:08x}" for word in struct.unpack("<8I", image[:VECTOR_BYTES])],
        "reported_vectors": [f"0x{word:08x}" for word in struct.unpack("<8I", vectors)] if vectors else None,
    }


def select_bootloader(ports, name):
    matches = [port for port in ports if port.device.lower() == name.lower()]
    if len(matches) != 1 or (matches[0].vid, matches[0].pid) != BOOTLOADER_USB_ID:
        raise InspectionError("Requested port is not the enumerated ArduPilot USB bootloader (1209:5741)")
    return matches[0]


def wait_for_bootloader(scan, name, seconds, clock=time.monotonic, sleep=time.sleep):
    deadline = clock() + seconds
    while True:
        try:
            return select_bootloader(scan(), name)
        except InspectionError:
            if clock() >= deadline:
                raise InspectionError(f"ArduPilot USB bootloader {name} was not present within {seconds:g} seconds")
            sleep(min(0.2, max(0, deadline - clock())))


def open_bootloader(scan, create, name, seconds, clock=time.monotonic, sleep=time.sleep):
    deadline = clock() + seconds
    errors = []
    while True:
        device = wait_for_bootloader(scan, name, max(0, deadline - clock()), clock, sleep)
        connection = create(port=None, baudrate=115200, timeout=3, write_timeout=3)
        connection.dtr = False
        connection.rts = False
        connection.port = device.device
        try:
            connection.open()
            return device, connection, errors
        except OSError as error:
            connection.close()
            errors.append(str(error))
            if clock() >= deadline:
                raise InspectionError(f"Bootloader port did not become available: {errors[-1]}")
            sleep(min(0.05, max(0, deadline - clock())))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="ArduPilot bootloader port, for example COM14")
    parser.add_argument("--firmware", type=Path, help="V6X .px4 package; defaults to the retained stock comparison")
    parser.add_argument("--report", type=Path, help="New JSON report path; existing files are never overwritten")
    parser.add_argument("--wait-seconds", type=float, default=0, help="Wait up to 60 seconds for a brief bootloader appearance")
    args = parser.parse_args(argv)
    if not 0 <= args.wait_seconds <= 60:
        parser.error("--wait-seconds must be between 0 and 60")
    now = datetime.datetime.now(datetime.timezone.utc)
    report_path = args.report or STAGE / ("cold-readback-" + now.strftime("%Y%m%dT%H%M%S%fZ") + ".json")
    if report_path.exists():
        parser.error("Report already exists; choose a new path")
    report = {"schema_version": 1, "observed_utc": now.isoformat(), "port": args.port,
              "power_cycle_observed_by_tool": False, "application_boot_verified": False,
              "physical_acceptance_granted": False}
    reader = None
    try:
        import serial
        from serial.tools import list_ports
        image, report["candidate"] = load_candidate(args.firmware)
        if args.wait_seconds:
            print(f"Waiting up to {args.wait_seconds:g} seconds for {args.port}; reconnect USB now.", flush=True)
        device, connection, open_errors = open_bootloader(list_ports.comports, serial.Serial,
                                                        args.port, args.wait_seconds)
        report["usb_id"] = f"{device.vid:04x}:{device.pid:04x}"
        report["transient_port_open_errors"] = open_errors
        with connection:
            connection.reset_input_buffer()
            reader = Reader(connection)
            report.update(inspect(reader, image))
            report["observed_utc"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
        result = 0
    except (InspectionError, ImportError, OSError, ValueError, KeyError, zlib.error) as error:
        report.update(status="inspection_failed", error=str(error))
        result = 1
    if reader:
        report["commands_sent_hex"] = reader.commands
    report_path.parent.mkdir(parents=True, exist_ok=True)
    with report_path.open("x", encoding="utf-8") as output:
        json.dump(report, output, indent=2)
        output.write("\n")
    print(json.dumps(report, indent=2))
    print(f"Report saved: {report_path}", file=sys.stderr)
    return result


if __name__ == "__main__":
    sys.exit(main())
