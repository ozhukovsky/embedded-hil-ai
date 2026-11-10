import sys
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1] / "tools"
sys.path.insert(0, str(TOOLS))
import hil_smoke_test as smoke  # noqa: E402


class FakeClock:
    def __init__(self):
        self.now = 0.0

    def __call__(self):
        return self.now

    def advance(self, amount=0.1):
        self.now += amount


class FakeSerial:
    def __init__(self, chunks, clock):
        self.chunks = list(chunks)
        self.clock = clock
        self.writes = []

    def write(self, data):
        self.writes.append(data)
        return len(data)

    def flush(self):
        pass

    def read(self, _size):
        self.clock.advance()
        return self.chunks.pop(0) if self.chunks else b""


def make_frame(message_type, sequence, payload=b""):
    return smoke.encode_frame(message_type, sequence, payload)


def make_status(role, state=1, fault=0, flags=0):
    return (bytes((role, state)) + fault.to_bytes(2, "little") +
            bytes(8) + flags.to_bytes(2, "little"))


def make_heartbeat(role, uptime):
    return bytes((role, 1)) + bytes(4) + uptime.to_bytes(4, "little")


class SmokeTestLateConnectionTests(unittest.TestCase):
    def run_fake(self, role, chunks, listen_seconds=0.3, timeout=0.5):
        clock = FakeClock()
        port = FakeSerial(chunks, clock)
        output = []
        result = smoke.run_smoke_test(port, role, timeout, listen_seconds,
                                      output.append, clock)
        return result, port, output

    def test_late_connection_succeeds_without_boot_and_displays_heartbeats(self):
        expected_role = smoke.ROLE["plant"]
        data = (make_frame(smoke.HEARTBEAT, 8, make_heartbeat(expected_role, 1000)) +
                make_frame(smoke.STATUS, 9, make_status(expected_role)) +
                make_frame(smoke.HEARTBEAT, 10, make_heartbeat(expected_role, 2000)))
        result, port, output = self.run_fake("plant", [data])

        self.assertTrue(result)
        sent = smoke.StreamParser().feed(port.writes[0])
        self.assertEqual(len(sent), 1)
        self.assertEqual(sent[0].message_type, smoke.STATUS_REQUEST)
        self.assertEqual(sent[0].payload, b"")
        self.assertIn("BOOT_REPORT observed: no", output)
        self.assertTrue(any(line.startswith("HEARTBEAT role=2") for line in output))

    def test_bad_crc_and_wrong_role_status_do_not_satisfy_request(self):
        wrong = make_frame(smoke.STATUS, 1, make_status(smoke.ROLE["dut"]))
        bad_crc = wrong[:-1] + bytes((wrong[-1] ^ 0x80,))
        valid = make_frame(smoke.STATUS, 2, make_status(smoke.ROLE["plant"]))
        result, _port, output = self.run_fake("plant", [bad_crc + wrong, valid])

        self.assertTrue(result)
        self.assertIn("STATUS role=2", "\n".join(output))

    def test_invalid_status_payload_does_not_satisfy_request(self):
        invalid = make_status(smoke.ROLE["dut"], state=8)
        result, _port, output = self.run_fake("dut", [make_frame(smoke.STATUS, 1, invalid)])

        self.assertFalse(result)
        self.assertIn("no valid STATUS response", "\n".join(output))

    def test_timeout_fails_when_no_valid_status_arrives(self):
        heartbeat = make_frame(smoke.HEARTBEAT, 1,
                               make_heartbeat(smoke.ROLE["dut"], 1000))
        result, _port, output = self.run_fake("dut", [heartbeat], timeout=0.3)

        self.assertFalse(result)
        self.assertTrue(any(line.startswith("HEARTBEAT role=1") for line in output))
        self.assertIn("BOOT_REPORT observed: no", output)

    def test_status_payload_validates_role_state_fault_and_reserved_flags(self):
        role = smoke.ROLE["dut"]
        self.assertTrue(smoke.valid_status_payload(make_status(role), role))
        self.assertFalse(smoke.valid_status_payload(make_status(2), role))
        self.assertFalse(smoke.valid_status_payload(make_status(role, state=8), role))
        self.assertFalse(smoke.valid_status_payload(make_status(role, fault=8), role))
        self.assertFalse(smoke.valid_status_payload(make_status(role, flags=0x0040), role))
        self.assertFalse(smoke.valid_status_payload(bytes(13), role))


if __name__ == "__main__":
    unittest.main()
