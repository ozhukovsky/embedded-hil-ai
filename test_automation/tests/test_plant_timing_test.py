import struct
import sys
import unittest
from collections import deque
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1] / "tools"
sys.path.insert(0, str(TOOLS))
import hil_smoke_test as protocol  # noqa: E402
import plant_interface_test as interface  # noqa: E402
import plant_timing_test as timing  # noqa: E402


class Clock:
    def __init__(self):
        self.now = 0.0

    def __call__(self):
        return self.now

    def sleep(self, duration):
        self.now += duration


def status_frame(sequence, state, test_id=0, flags=0x09, fault=0):
    payload = struct.pack("<BBHIIH", 2, state, fault, test_id, 1234, flags)
    return protocol.encode_frame(protocol.STATUS, sequence, payload)


def ack_frame(sequence, command_sequence, command_type):
    payload = struct.pack("<HBBH", command_sequence, command_type, 2, 0)
    return protocol.encode_frame(timing.ACK, sequence, payload)


class FakePlantPort:
    def __init__(self, clock):
        self.clock = clock
        self.parser = protocol.StreamParser()
        self.responses = deque()
        self.writes = []
        self.state = timing.WAIT_CONFIG
        self.test_id = 0
        self.heater_high = False
        self.response_sequence = 10
        self.last_heartbeat_second = -1
        self.last_telemetry_second = -1

    def _push(self, message_type, payload):
        self.responses.append(protocol.encode_frame(message_type, self.response_sequence, payload))
        self.response_sequence += 1

    def _respond(self, frame):
        if frame.message_type == protocol.STATUS_REQUEST:
            flags = 0
            if self.state == timing.READY:
                flags = 0x09 | (0x02 if self.heater_high else 0)
            self.responses.append(status_frame(self.response_sequence, self.state, self.test_id,
                                                flags, 0))
            self.response_sequence += 1
        elif frame.message_type == timing.CONFIG_SET:
            self.test_id = int.from_bytes(frame.payload[1:5], "little")
            self.state = timing.READY
            self.responses.append(ack_frame(self.response_sequence, frame.sequence, timing.CONFIG_SET))
            self.response_sequence += 1
        elif frame.message_type == timing.ARM_TEST:
            self.state = timing.ARMED
            self.responses.append(ack_frame(self.response_sequence, frame.sequence, timing.ARM_TEST))
            self.response_sequence += 1
            self._push(interface.ARMED, struct.pack("<BIH", 2, self.test_id, 0x17))
        elif frame.message_type == timing.START_TEST:
            self.state = timing.RUNNING
            self.responses.append(ack_frame(self.response_sequence, frame.sequence, timing.START_TEST))
            self.response_sequence += 1
        elif frame.message_type == timing.STOP_TEST:
            self.state = timing.COMPLETE
            self.responses.append(ack_frame(self.response_sequence, frame.sequence, timing.STOP_TEST))
            self.response_sequence += 1
            elapsed = int(self.clock() * 1000)
            self._push(timing.TEST_COMPLETE, struct.pack("<BIBI", 2, self.test_id, 0, elapsed))

    def write(self, data):
        frames = self.parser.feed(data)
        self.writes.extend(frames)
        for frame in frames:
            self._respond(frame)
        return len(data)

    def flush(self):
        pass

    def read(self, _size):
        self.clock.now += 0.1
        if self.state == timing.RUNNING:
            second = int(self.clock.now)
            if second > self.last_heartbeat_second:
                self.last_heartbeat_second = second
                self._push(protocol.HEARTBEAT, struct.pack("<BBII", 2, timing.RUNNING,
                                                           self.test_id, second * 1000))
            if second >= 5 and second // 5 > self.last_telemetry_second:
                self.last_telemetry_second = second // 5
                self._push(timing.TELEMETRY,
                           struct.pack("<BIIIiiBHB", 2, self.test_id, second // 5,
                                       second * 1000, 20000, 20000, 0, 2050, 1))
        return self.responses.popleft() if self.responses else b""


class PlantTimingUtilityTests(unittest.TestCase):
    def test_arguments_and_observation_minimum(self):
        args = timing.build_parser().parse_args(["--port", "COM3", "--test-id", "0x2A"])
        self.assertEqual((args.port, args.baud, args.test_id, args.observation_seconds),
                         ("COM3", 115200, 42, 18.0))
        for bad in ("0", "0x100000000", "-1"):
            with self.subTest(test_id=bad), self.assertRaises(SystemExit):
                timing.build_parser().parse_args(["--port", "COM3", "--test-id", bad])
        with self.assertRaises(SystemExit):
            timing.build_parser().parse_args(["--port", "COM3", "--observation-seconds", "nan"])
        with self.assertRaisesRegex(ValueError, "18 through 50"):
            timing.PlantTimingTest(None, 1, observation_seconds=17.9)
        with self.assertRaisesRegex(ValueError, "18 through 50"):
            timing.PlantTimingTest(None, 1, observation_seconds=51)

    def test_config_uses_existing_serializer_with_requested_values(self):
        payload = timing.timing_config_payload(42)
        values = struct.unpack("<BIiiIiHHIB", payload)
        self.assertEqual(values, (2, 42, 20000, 20000, 300, 20, 5000, 5000, 60000, 0))

    def test_test_complete_requires_normal_outcome_and_minimum_duration(self):
        payload = struct.pack("<BIBI", 2, 42, 0, 18000)
        self.assertEqual(timing.decode_normal_test_complete(payload, 42), 18000)
        with self.assertRaisesRegex(timing.PlantTimingTestError, "shorter"):
            timing.decode_normal_test_complete(struct.pack("<BIBI", 2, 42, 0, 17999), 42)
        with self.assertRaisesRegex(timing.PlantTimingTestError, "NORMAL"):
            timing.decode_normal_test_complete(struct.pack("<BIBI", 2, 42, 2, 18000), 42)

    def test_end_to_end_configure_arm_run_and_single_stop(self):
        clock = Clock()
        port = FakePlantPort(clock)
        output = []
        responses = iter(("DISCONNECTED", "", ""))

        def answer(prompt):
            if "Apply 3.3 V" in prompt:
                port.heater_high = True
            elif "Remove 3.3 V" in prompt:
                port.heater_high = False
            return next(responses)

        runner = timing.PlantTimingTest(port, 42, response_timeout=1.0,
                                        level_timeout=1.0, poll_interval=0.5,
                                        output=output.append, input_fn=answer,
                                        clock=clock, sleep_fn=clock.sleep)
        self.assertTrue(runner.run())
        types = [frame.message_type for frame in port.writes]
        self.assertEqual(types.count(timing.START_TEST), 1)
        self.assertEqual(types.count(timing.STOP_TEST), 1)
        self.assertEqual(types.count(timing.CONFIG_SET), 1)
        self.assertGreaterEqual(runner.counts[protocol.HEARTBEAT], 18)
        self.assertGreaterEqual(runner.counts[timing.TELEMETRY], 3)
        self.assertTrue(any("state=COMPLETE" in line for line in output))
        self.assertTrue(any("Observation complete" in line for line in output))

    def test_non_wait_config_state_refuses_to_reconfigure(self):
        clock = Clock()
        port = FakePlantPort(clock)
        port.state = timing.READY
        runner = timing.PlantTimingTest(port, 1, output=lambda _line: None,
                                        input_fn=lambda _prompt: self.fail("must not prompt"),
                                        clock=clock, sleep_fn=clock.sleep)
        self.assertFalse(runner.run())
        self.assertEqual([f.message_type for f in port.writes], [protocol.STATUS_REQUEST])


if __name__ == "__main__":
    unittest.main()
