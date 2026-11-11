import sys
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1] / "tools"
sys.path.insert(0, str(TOOLS))
import hil_smoke_test as protocol  # noqa: E402
import plant_interface_test as interface  # noqa: E402


class Clock:
    def __init__(self):
        self.now = 0.0

    def __call__(self):
        return self.now

    def sleep(self, duration):
        self.now += duration


class FakePort:
    def __init__(self, responses, clock):
        self.responses = list(responses)
        self.clock = clock
        self.writes = []

    def write(self, data):
        self.writes.append(data)
        return len(data)

    def flush(self):
        pass

    def read(self, _size):
        self.clock.now += 0.01
        return self.responses.pop(0) if self.responses else b""


def status_frame(sequence, state=interface.READY, flags=0x09, test_id=42, role=interface.PLANT):
    payload = (bytes((role, state)) + bytes(2) + test_id.to_bytes(4, "little") +
               bytes(4) + flags.to_bytes(2, "little"))
    return protocol.encode_frame(protocol.STATUS, sequence, payload)


class PlantInterfaceUtilityTests(unittest.TestCase):
    def test_argument_defaults_and_custom_values(self):
        args = interface.build_parser().parse_args(["--port", "COM3"])
        self.assertEqual((args.port, args.baud, args.timeout), ("COM3", 115200, 5.0))
        args = interface.build_parser().parse_args(
            ["--port", "COM9", "--baud", "57600", "--timeout", "8", "--poll-interval", "0.5"])
        self.assertEqual((args.port, args.baud, args.timeout, args.poll_interval),
                         ("COM9", 57600, 8.0, 0.5))

    def test_argument_rejects_nonpositive_timeout_and_baud(self):
        for argv in (["--port", "COM3", "--timeout", "0"],
                     ["--port", "COM3", "--baud", "-1"],
                     ["--port", "COM3", "--timeout", "inf"]):
            with self.subTest(argv=argv), self.assertRaises(SystemExit):
                interface.build_parser().parse_args(argv)

    def test_status_decoding_reports_live_heater_bit(self):
        high = interface.decode_plant_status(status_frame(1, flags=0x0B)[8:-2])
        low = interface.decode_plant_status(status_frame(2, flags=0x09)[8:-2])
        self.assertEqual(high.test_id, 42)
        self.assertTrue(high.heater_high)
        self.assertFalse(low.heater_high)

    def test_status_rejects_wrong_role_or_malformed_payload(self):
        with self.assertRaises(interface.PlantInterfaceError):
            interface.decode_plant_status(status_frame(1, role=1)[8:-2])
        with self.assertRaises(interface.PlantInterfaceError):
            interface.decode_plant_status(bytes(13))

    def test_arm_response_requires_matching_ack_then_armed_bit_four(self):
        tracker = interface.ArmResponseTracker(7, 42)
        ack = protocol.Frame(interface.ACK, 100,
                             (7).to_bytes(2, "little") + bytes((interface.ARM_TEST, 2, 0, 0)))
        armed = protocol.Frame(interface.ARMED, 101,
                               bytes((2,)) + (42).to_bytes(4, "little") + (0x17).to_bytes(2, "little"))
        self.assertFalse(tracker.consume(ack))
        self.assertTrue(tracker.consume(armed))
        self.assertEqual(tracker.readiness_flags, 0x17)

    def test_arm_rejects_nack_order_and_missing_interface_flag(self):
        nack = protocol.Frame(interface.NACK, 3,
                              (8).to_bytes(2, "little") + bytes((interface.ARM_TEST, 2, 7, 0)))
        with self.assertRaisesRegex(interface.PlantInterfaceError, "HARDWARE_NOT_READY"):
            interface.ArmResponseTracker(8, 42).consume(nack)

        tracker = interface.ArmResponseTracker(8, 42)
        armed = protocol.Frame(interface.ARMED, 9,
                               bytes((2,)) + (42).to_bytes(4, "little") + (0x07).to_bytes(2, "little"))
        with self.assertRaisesRegex(interface.PlantInterfaceError, "before its required ACK"):
            tracker.consume(armed)

        tracker.consume(protocol.Frame(interface.ACK, 10,
                                       (8).to_bytes(2, "little") + bytes((interface.ARM_TEST, 2, 0, 0))))
        with self.assertRaisesRegex(interface.PlantInterfaceError, "bit 4"):
            tracker.consume(armed)

    def test_bad_crc_is_discarded_and_next_valid_status_is_parsed(self):
        bad = bytearray(status_frame(1))
        bad[-1] ^= 0xFF
        parser = protocol.StreamParser()
        frames = parser.feed(bytes(bad) + status_frame(2, flags=0x0B))
        self.assertEqual(len(frames), 1)
        self.assertTrue(interface.decode_plant_status(frames[0].payload).heater_high)

    def test_end_to_end_waits_for_both_levels_before_arm_and_never_starts(self):
        clock = Clock()
        ack = protocol.encode_frame(interface.ACK, 30,
                                    (5).to_bytes(2, "little") + bytes((interface.ARM_TEST, 2, 0, 0)))
        armed = protocol.encode_frame(interface.ARMED, 31,
                                      bytes((2,)) + (42).to_bytes(4, "little") + (0x17).to_bytes(2, "little"))
        port = FakePort([
            status_frame(1, flags=0x09),
            status_frame(2, flags=0x09),
            status_frame(3, flags=0x0B),
            status_frame(4, flags=0x0B),
            status_frame(5, flags=0x09),
            ack + armed,
        ], clock)
        output = []
        prompts = iter(("DISCONNECTED", "", ""))
        runner = interface.PlantInterfaceTest(port, 1.0, 0.01, output.append,
                                              lambda _prompt: next(prompts), clock, clock.sleep)
        self.assertTrue(runner.run())
        decoded = [protocol.StreamParser().feed(frame)[0] for frame in port.writes]
        types = [frame.message_type for frame in decoded]
        self.assertEqual(types.count(protocol.STATUS_REQUEST), 5)
        self.assertEqual(types[-1], interface.ARM_TEST)
        self.assertNotIn(0x10, types)  # CONFIG_SET
        self.assertNotIn(0x12, types)  # START_TEST
        self.assertTrue(any("bit 4=1" in line for line in output))

    def test_high_initial_status_does_not_arm(self):
        clock = Clock()
        port = FakePort([status_frame(1, flags=0x0B)], clock)
        runner = interface.PlantInterfaceTest(port, 0.1, 0.01, lambda _line: None,
                                              lambda _prompt: self.fail("must not prompt for injection"),
                                              clock, clock.sleep)
        self.assertFalse(runner.run())
        sent = [protocol.StreamParser().feed(data)[0].message_type for data in port.writes]
        self.assertEqual(sent, [protocol.STATUS_REQUEST])

    def test_missing_high_or_return_to_low_never_sends_arm(self):
        cases = (
            [status_frame(1, flags=0x09)] + [status_frame(i, flags=0x09) for i in range(2, 20)],
            [status_frame(1, flags=0x09), status_frame(2, flags=0x0B)] +
            [status_frame(i, flags=0x0B) for i in range(3, 20)],
        )
        for responses in cases:
            with self.subTest(expected_transition=len(responses)):
                clock = Clock()
                port = FakePort(responses, clock)
                prompts = iter(("DISCONNECTED", "", ""))
                runner = interface.PlantInterfaceTest(port, 0.05, 0.01, lambda _line: None,
                                                      lambda _prompt: next(prompts),
                                                      clock, clock.sleep)
                self.assertFalse(runner.run())
                sent = [protocol.StreamParser().feed(data)[0].message_type for data in port.writes]
                self.assertNotIn(interface.ARM_TEST, sent)


if __name__ == "__main__":
    unittest.main()
