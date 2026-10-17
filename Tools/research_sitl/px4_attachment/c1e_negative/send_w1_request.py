"""One signed W1 diagnostic request over the reviewed loopback endpoint.

The caller owns the ephemeral 32-byte key in memory. This module never reads a
key from argv, logs it, or retries a request after an unknown delivery outcome.
Generated pinned W2 Python encoders supply the request frame; CRC/signature and
returned status are checked independently before the W1 codec accepts status.
"""

from __future__ import annotations

import hashlib
import importlib.util
from pathlib import Path
import socket
import sys
import time


WIRE_DIR = Path(__file__).resolve().parents[2] / "diagnostic_wire"
sys.path.insert(0, str(WIRE_DIR))
import codec as w1  # noqa: E402
import test_generated_mavlink as w2  # noqa: E402


REQUEST_ID = 200010
STATUS_ID = 200011
REQUEST_CRC_EXTRA = 187
STATUS_CRC_EXTRA = 159


class SenderError(ValueError):
    pass


def x25(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        value = byte ^ (crc & 0xFF)
        value ^= (value << 4) & 0xFF
        crc = ((crc >> 8) ^ (value << 8) ^ (value << 3) ^ (value >> 4)) & 0xFFFF
    return crc


def generated_module(path: Path, expected_sha256: str):
    if not path.is_file() or path.is_symlink():
        raise SenderError("Generated W2 binding missing or nonregular")
    if hashlib.sha256(path.read_bytes()).hexdigest() != expected_sha256:
        raise SenderError("Generated W2 binding hash changed")
    specification = importlib.util.spec_from_file_location("reviewed_c1e_diagnostic", path)
    module = importlib.util.module_from_spec(specification)
    specification.loader.exec_module(module)
    if Path(module.__file__).resolve() != path.resolve():
        raise SenderError("Generated W2 binding import escaped reviewed path")
    return module


def request_fields(bindings, producer_now_us, expected_epoch):
    if type(producer_now_us) is not int or producer_now_us <= 0 or producer_now_us > (1 << 64) - 80001:
        raise SenderError("Invalid unqualified producer timestamp")
    if type(expected_epoch) is not int or not 0 < expected_epoch < (1 << 64):
        raise SenderError("Missing source-bound transition epoch")
    return {
        "native_run_id": int(bindings["native_run_id"]),
        "owner_lifetime_id": int(bindings["owner_lifetime_id"]),
        "controller_instance_id": int(bindings["controller_instance_id"]),
        "source_id": int(bindings["permitted_source"]),
        "transport_session_id": int(bindings["transport_binding_id"]),
        "request_sequence": 1, "expected_transition_epoch": expected_epoch,
        "producer_monotonic_us": producer_now_us,
        "producer_expiry_monotonic_us": producer_now_us + 80000,
        "clock_mapping_epoch": 0,
        "acceleration_x_frd_m_s2": 1.5,
        "acceleration_y_frd_m_s2": -2.25,
        "acceleration_z_frd_m_s2": 0.5,
        "protocol_version": 65536, "domain": 1, "profile": 1,
        "target_system": 1, "target_component": 1,
        "frame": 1, "quantity": 1, "reference_point": 1,
        "flags": 0, "reserved": b"\0\0\0\0",
    }


def signing_time_10us():
    """Use the same Linux monotonic epoch as PX4 hrt, after bootstrap."""
    timestamp = time.monotonic_ns() // 10_000
    if not 0 < timestamp < (1 << 48):
        raise SenderError("Native monotonic signing timestamp outside 48 bits")
    return timestamp


def signed_request(module, fields, key, signing_timestamp=None):
    if signing_timestamp is None:
        signing_timestamp = signing_time_10us()
    if len(key) != 32 or not 0 < signing_timestamp < (1 << 48):
        raise SenderError("Invalid disposable signing material or timestamp")
    payload = w1.encode_request(fields, trim=True)
    unsigned = w2.python_frame(module, "RESEARCH_DIAGNOSTIC_REQUEST", w1.encode_request(fields))
    if unsigned[0] != 0xFD or unsigned[2:4] != b"\0\0" or unsigned[5:7] != bytes((42, 191)) \
            or int.from_bytes(unsigned[7:10], "little") != REQUEST_ID \
            or unsigned[1] != len(payload) or unsigned[10:-2] != payload:
        raise SenderError("Generated request disagrees with strict W1 payload")
    header = bytes((0xFD, len(payload), 1, 0, unsigned[4], 42, 191))
    header += REQUEST_ID.to_bytes(3, "little")
    crc = x25(header[1:] + payload + bytes((REQUEST_CRC_EXTRA,))).to_bytes(2, "little")
    packet = header + payload + crc
    signature_header = bytes((7,)) + signing_timestamp.to_bytes(6, "little")
    packet += signature_header + hashlib.sha256(key + packet + signature_header).digest()[:6]
    return packet


def parse_signed_status(module, packet, key, bindings):
    if len(key) != 32 or len(packet) < 157 or len(packet) > 161 or packet[0] != 0xFD \
            or packet[2:4] != b"\x01\x00" or len(packet) != packet[1] + 25 \
            or not 132 <= packet[1] <= 136 or packet[5:7] != b"\x01\x01" \
            or int.from_bytes(packet[7:10], "little") != STATUS_ID:
        raise SenderError("Returned status envelope differs from C1E W1")
    payload = packet[10:10 + packet[1]]
    crc = x25(packet[1:10] + payload + bytes((STATUS_CRC_EXTRA,))).to_bytes(2, "little")
    if packet[10 + packet[1]:12 + packet[1]] != crc:
        raise SenderError("Returned status CRC failed independent check")
    signature = packet[-13:]
    if signature[0] != 8 or signature[7:] != hashlib.sha256(key + packet[:-13] + signature[:7]).digest()[:6]:
        raise SenderError("Returned status signature/link failed independent check")
    unsigned = bytes((0xFD, len(payload), 0, 0)) + packet[4:10]
    unsigned_crc = x25(unsigned[1:] + payload + bytes((STATUS_CRC_EXTRA,))).to_bytes(2, "little")
    try:
        w2.python_parse(module, "RESEARCH_DIAGNOSTIC_STATUS", unsigned + payload + unsigned_crc,
                        payload + bytes(136 - len(payload)))
        status = w1.decode_status(payload)
    except (ValueError, RuntimeError) as error:
        raise SenderError("Generated/W1 status decode failed") from error
    expected = {"native_run_id": int(bindings["native_run_id"]),
                "owner_lifetime_id": int(bindings["owner_lifetime_id"]),
                "controller_instance_id": int(bindings["controller_instance_id"]),
                "source_id": int(bindings["permitted_source"]),
                "transport_session_id": int(bindings["transport_binding_id"]),
                "request_sequence": 1, "status_sequence": 1,
                "reason": 4, "domain": 1, "profile": 1,
                "execution_origin": 2, "outcome": 1, "arm_state": 1,
                "target_system": 42, "target_component": 191,
                "eligible_for_control": 0, "clock_mapping_epoch": 0}
    if any(status[name] != value for name, value in expected.items()):
        raise SenderError("Returned status is not the correlated native rejection")
    if status["arrival_native_us"] > status["evaluation_native_us"]:
        raise SenderError("Returned native time correlation regressed")
    return status


def send_one(module, key, bindings, endpoint, expected_epoch, timeout_seconds=8.0,
             on_request_sent=None, on_status_received=None):
    """Send once through a peer socket already bound before PX4 starts."""
    local_port = bindings["local_port"]
    peer_port = bindings["peer_port"]
    if type(local_port) is not int or type(peer_port) is not int \
            or not 1 <= local_port <= 65535 or not 1 <= peer_port <= 65535 \
            or local_port == peer_port:
        raise SenderError("Invalid loopback endpoint ports")
    request = signed_request(module,
                             request_fields(bindings, time.monotonic_ns() // 1000, expected_epoch), key)
    if endpoint.family != socket.AF_INET or endpoint.type & socket.SOCK_DGRAM != socket.SOCK_DGRAM \
            or endpoint.getsockname() != ("127.0.0.1", peer_port):
        raise SenderError("Peer receiver was not prebound to reviewed loopback endpoint")
    endpoint.settimeout(timeout_seconds)
    sent = endpoint.sendto(request, ("127.0.0.1", local_port))
    if sent != len(request):
        raise SenderError("Local request send was incomplete")
    if on_request_sent is not None:
        on_request_sent(request)
    status_bytes, address = endpoint.recvfrom(512)
    if on_status_received is not None:
        on_status_received(status_bytes)
    if address != ("127.0.0.1", local_port):
        raise SenderError("Status came from an unexpected endpoint")
    status = parse_signed_status(module, status_bytes, key, bindings)
    return request, status_bytes, status
