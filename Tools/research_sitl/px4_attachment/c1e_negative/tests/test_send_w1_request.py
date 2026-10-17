"""Offline sender wire checks; no socket, PX4 process, or key deployment."""

import hashlib
import importlib.util
from pathlib import Path
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("c1e_w1_sender", ROOT / "send_w1_request.py")
sender = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(sender)
KEY = bytes(range(1, 33))  # Disposable public fixture key.
BINDINGS = {"native_run_id": "11", "owner_lifetime_id": "12",
            "controller_instance_id": "13", "permitted_source": "14",
            "transport_binding_id": "15", "local_port": 14500, "peer_port": 14600}


def status_frame(fields):
    payload = sender.w1.encode_status(fields, trim=True)
    header = bytes((0xFD, len(payload), 1, 0, 19, 1, 1)) + sender.STATUS_ID.to_bytes(3, "little")
    crc = sender.x25(header[1:] + payload + bytes((sender.STATUS_CRC_EXTRA,))).to_bytes(2, "little")
    packet = header + payload + crc
    signature = bytes((8,)) + (10000105).to_bytes(6, "little")
    return packet + signature + hashlib.sha256(KEY + packet + signature).digest()[:6]


class SenderWireTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.area = tempfile.TemporaryDirectory(prefix="c1e_sender_")
        generated, module_path = sender.w2.generate(Path(cls.area.name))
        cls.module = sender.generated_module(
            module_path, hashlib.sha256(module_path.read_bytes()).hexdigest())
        cls.generated = generated

    @classmethod
    def tearDownClass(cls):
        cls.area.cleanup()

    def test_generated_request_and_independent_signature(self):
        fields = sender.request_fields(BINDINGS, 1000, 2)
        packet = sender.signed_request(self.module, fields, KEY, 10000100)
        self.assertEqual(packet[0], 0xFD)
        self.assertEqual(packet[2:4], b"\x01\x00")
        self.assertEqual(packet[5:7], bytes((42, 191)))
        self.assertEqual(int.from_bytes(packet[7:10], "little"), sender.REQUEST_ID)
        self.assertEqual(packet[10:10 + packet[1]], sender.w1.encode_request(fields, trim=True))
        self.assertEqual(packet[-13:-6], bytes((7,)) + (10000100).to_bytes(6, "little"))
        self.assertEqual(packet[-6:], hashlib.sha256(KEY + packet[:-13] + packet[-13:-6]).digest()[:6])
        self.assertEqual(packet[10 + packet[1]:12 + packet[1]],
                         sender.x25(packet[1:10 + packet[1]] + bytes((187,))).to_bytes(2, "little"))

    def test_status_independent_crc_signature_and_generated_decode(self):
        fields = sender.w1.decode_status(sender.w2.PAYLOADS["RESEARCH_DIAGNOSTIC_STATUS"])
        fields.update(request_sequence=1, status_sequence=1, reason=4, execution_origin=2, outcome=1)
        packet = status_frame(fields)
        result = sender.parse_signed_status(self.module, packet, KEY, BINDINGS)
        self.assertEqual(result["reason"], 4)
        for mutation in (lambda data: data.__setitem__(-1, data[-1] ^ 1),
                         lambda data: data.__setitem__(10, data[10] ^ 1),
                         lambda data: data.__setitem__(-13, 7)):
            damaged = bytearray(packet)
            mutation(damaged)
            with self.assertRaises(sender.SenderError):
                sender.parse_signed_status(self.module, bytes(damaged), KEY, BINDINGS)
        fields["reason"] = 1
        with self.assertRaises(sender.SenderError):
            sender.parse_signed_status(self.module, status_frame(fields), KEY, BINDINGS)

    def test_generated_binding_hash_and_invalid_key(self):
        module_path = Path(self.module.__file__)
        with self.assertRaises(sender.SenderError):
            sender.generated_module(module_path, "0" * 64)
        with self.assertRaises(sender.SenderError):
            sender.signed_request(self.module, sender.request_fields(BINDINGS, 1000, 2), b"bad")
        with self.assertRaises(sender.SenderError):
            sender.request_fields(BINDINGS, 1000, 0)


if __name__ == "__main__":
    unittest.main()
