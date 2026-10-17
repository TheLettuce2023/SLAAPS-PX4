"""Private C1H loopback transport; no PX4, simulator or serial endpoint support."""

import copy
import os
import socket
import time

from codec import CodecError, decode_status


class TransportError(RuntimeError):
    pass


class Guidance:
    """Cooperative sender capability: deliberately no native/console handle."""

    def __init__(self, module, key, seed):
        if "MAV_IGNORE_CRC" in os.environ or module.MAVLINK_IGNORE_CRC != 0:
            raise TransportError("CRC bypass is forbidden")
        self.module = module
        self.key = key
        self.mav = module.MAVLink(None, srcSystem=42, srcComponent=191)
        self.mav.signing.secret_key = key
        self.mav.signing.link_id = 7
        self.mav.signing.timestamp = seed
        self.mav.signing.sign_outgoing = True
        self.replay = {}
        self.status_timestamp = seed
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.socket.bind(("127.0.0.1", 0))
        self.socket.settimeout(0.2)

    def close(self):
        self.socket.close()
        self.key = None
        self.mav.signing.secret_key = None
        self.replay.clear()

    def frame(self, kind, fields):
        timestamp = max(self.mav.signing.timestamp, time.monotonic_ns() // 10000)
        if not 0 < timestamp < (1 << 48) - 1:
            raise TransportError("signing clock exhausted")
        self.mav.signing.timestamp = timestamp
        constructor = getattr(self.module, "MAVLink_research_acceleration_%s_message" % kind)
        values = dict(fields)
        values["reserved"] = list(values["reserved"])
        packet = constructor(**values).pack(self.mav)
        self.mav.seq = (self.mav.seq + 1) % 256
        return bytes(packet)

    def send(self, frame, peer):
        if peer[0] != "127.0.0.1" or peer[1] <= 0:
            raise TransportError("non-private destination")
        pre = time.monotonic_ns()
        if self.socket.sendto(frame, peer) != len(frame):
            raise TransportError("incomplete datagram send")
        return pre, time.monotonic_ns()

    def status(self, expected_peer, controller):
        frame, peer = self.socket.recvfrom(2048)
        returned = time.monotonic_ns()
        if peer != expected_peer:
            raise TransportError("status peer mismatch")
        return self.verify_status(frame, controller), returned

    def verify_status(self, frame, controller):
        # No candidate replay state survives any failed envelope/auth/binding check.
        if (len(frame) != 131 or frame[:4] != bytes((253, 106, 1, 0))
                or frame[5:7] != bytes((1, 1))
                or int.from_bytes(frame[7:10], "little") != 200002 or frame[-13] != 8):
            raise TransportError("status envelope/binding")
        candidate = self.module.MAVLink(None)
        candidate.robust_parsing = False
        candidate.signing.secret_key = self.key
        candidate.signing.allow_unsigned_callback = None
        candidate.signing.timestamp = self.status_timestamp
        candidate.signing.stream_timestamps = copy.deepcopy(self.replay)
        try:
            message = candidate.parse_char(frame)
        except self.module.MAVError as error:
            raise TransportError("status authentication rejected") from error
        if (message is None or message.get_msgId() != 200002
                or message.get_signed() is not True or message.get_link_id() != 8
                or bytes(message.get_msgbuf()) != frame or candidate.buf_len() != 0):
            raise TransportError("status parser did not authenticate an atomic frame")
        try:
            status = decode_status(frame[10:116])
        except CodecError as error:
            raise TransportError("status semantics rejected") from error
        if (status["controller_instance_id"] != controller
                or (status["target_system"], status["target_component"]) != (42, 191)):
            raise TransportError("status payload binding")
        self.replay = copy.deepcopy(candidate.signing.stream_timestamps)
        self.status_timestamp = candidate.signing.timestamp
        return status


class LoopbackReceiver:
    def __init__(self):
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.socket.bind(("127.0.0.1", 0))
        self.socket.settimeout(0.2)

    def close(self):
        self.socket.close()

    @property
    def address(self):
        return self.socket.getsockname()

    def receive(self, expected_peer):
        data, peer = self.socket.recvfrom(2048)
        return data, time.monotonic_ns(), peer == expected_peer

    def status(self, frame, peer):
        if peer[0] != "127.0.0.1":
            raise TransportError("non-private status destination")
        if self.socket.sendto(frame, peer) != len(frame):
            raise TransportError("incomplete status send")
