import sys
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1] / "tools"
sys.path.insert(0, str(TOOLS))
import hil_smoke_test as protocol  # noqa: E402
import plant_bringup as bringup  # noqa: E402


class Clock:
    def __init__(self): self.now = 0.0
    def __call__(self): return self.now


class FakePort:
    def __init__(self, chunks, clock): self.chunks, self.clock, self.writes = list(chunks), clock, []
    def write(self, data): self.writes.append(data); return len(data)
    def flush(self): pass
    def read(self, _size):
        self.clock.now += 0.01
        return self.chunks.pop(0) if self.chunks else b""


def status(state, flags):
    return bytes((2, state)) + bytes(2) + (1).to_bytes(4, "little") + bytes(4) + flags.to_bytes(2, "little")


class PlantBringupTests(unittest.TestCase):
    def test_config_payload_has_expected_default_fields(self):
        payload = bringup.default_config_payload()
        self.assertEqual(len(payload), 30)
        self.assertEqual(payload[0], bringup.PLANT)
        self.assertEqual(int.from_bytes(payload[5:9], "little", signed=True), 20000)
        self.assertEqual(100 + round(((20 + 20) / 80) * 3900), 2050)

    def test_requires_ack_then_ready_and_emits_heartbeat(self):
        clock = Clock()
        first = (protocol.encode_frame(protocol.HEARTBEAT, 3, bytes((2, 2)) + bytes(8)) +
                 protocol.encode_frame(protocol.STATUS, 4, status(2, 0)))
        ack = protocol.encode_frame(bringup.ACK, 5, (1).to_bytes(2, "little") + bytes((bringup.CONFIG_SET, 2, 0, 0)))
        heart = protocol.encode_frame(protocol.HEARTBEAT, 6, bytes((2, 3)) + bytes(8))
        ready = protocol.encode_frame(protocol.STATUS, 7, status(bringup.READY, 9))
        # Ready is returned only after the second status request.
        later_heartbeat = protocol.encode_frame(protocol.HEARTBEAT, 8, bytes((2, 3)) + bytes(8))
        parts = [first, ack + heart, ready, later_heartbeat]
        port = FakePort(parts, clock)
        output = []
        self.assertTrue(bringup.run_bringup(port, 1.0, 0.1, output.append, clock))
        sent = [protocol.StreamParser().feed(w) for w in port.writes]
        self.assertEqual([f[0].message_type for f in sent], [protocol.STATUS_REQUEST, bringup.CONFIG_SET, protocol.STATUS_REQUEST])
        self.assertTrue(any(line.startswith("HEARTBEAT role=2") for line in output))
        self.assertIn("HEARTBEAT role=2 state=3 uptime_ms=0 seq=8", output)
        self.assertIn("No ARM_TEST or START_TEST was sent.", output)
        self.assertIn("PLANT_VALID=1 DAC_valid=1", output)

    def test_nack_and_missing_ready_fail(self):
        for terminal in (
            protocol.encode_frame(bringup.NACK, 5, (1).to_bytes(2, "little") + bytes((bringup.CONFIG_SET, 2, 0, 7))),
            protocol.encode_frame(protocol.STATUS, 6, status(2, 9)),
        ):
            clock = Clock()
            port = FakePort([protocol.encode_frame(protocol.STATUS, 1, status(2, 0)) + terminal], clock)
            self.assertFalse(bringup.run_bringup(port, 0.1, 0.0, lambda _line: None, clock))


if __name__ == "__main__":
    unittest.main()
