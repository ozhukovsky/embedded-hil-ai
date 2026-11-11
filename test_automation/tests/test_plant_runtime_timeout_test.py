import struct
import sys
import unittest
from collections import deque
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1] / "tools"
sys.path.insert(0, str(TOOLS))
import hil_smoke_test as protocol  # noqa: E402
import plant_runtime_timeout_test as runtime_test  # noqa: E402
import plant_interface_test as interface  # noqa: E402


class Clock:
    def __init__(self):
        self.now = 0.0

    def __call__(self):
        return self.now

    def sleep(self, duration):
        self.now += duration


def status_frame(sequence, state, test_id=0, flags=0, fault=0):
    payload = struct.pack("<BBHIIH", 2, state, fault, test_id, 1234, flags)
    return protocol.encode_frame(protocol.STATUS, sequence, payload)


def ack_frame(sequence, command_sequence, command_type, is_nack=False, error=0):
    payload = struct.pack("<HBBH", command_sequence, command_type, 2, error)
    msg = runtime_test.NACK if is_nack else runtime_test.ACK
    return protocol.encode_frame(msg, sequence, payload)


class FakePlantPort:
    def __init__(self, clock):
        self.clock = clock
        self.parser = protocol.StreamParser()
        self.responses = deque()
        self.writes = []
        self.state = runtime_test.WAIT_CONFIG
        self.test_id = 0
        self.heater_levels = deque((False, True, True, False))
        self.ready_status_sent = False

    def _response(self, frame):
        if frame.message_type == protocol.STATUS_REQUEST:
            if self.state == runtime_test.WAIT_CONFIG:
                self.responses.append(status_frame(100, runtime_test.WAIT_CONFIG))
            elif self.state == runtime_test.READY:
                if not self.ready_status_sent:
                    self.ready_status_sent = True
                    high = False
                else:
                    high = self.heater_levels.popleft() if self.heater_levels else False
                flags = runtime_test.STATUS_PLANT_VALID | runtime_test.STATUS_DAC_VALID
                if high:
                    flags |= runtime_test.STATUS_HEATER_HIGH
                self.responses.append(status_frame(101 + len(self.writes), runtime_test.READY,
                                                   self.test_id, flags))
            elif self.state == runtime_test.FAULT:
                self.responses.append(status_frame(200, runtime_test.FAULT,
                                                   self.test_id, runtime_test.STATUS_DAC_VALID, 7))
        elif frame.message_type == runtime_test.CONFIG_SET:
            self.test_id = int.from_bytes(frame.payload[1:5], "little")
            self.state = runtime_test.READY
            self.responses.append(ack_frame(102, frame.sequence, runtime_test.CONFIG_SET))
        elif frame.message_type == runtime_test.ARM_TEST:
            self.state = runtime_test.ARMED_STATE
            self.responses.append(ack_frame(103, frame.sequence, runtime_test.ARM_TEST))
            payload = struct.pack("<BIH", 2, self.test_id, 0x17)
            self.responses.append(protocol.encode_frame(interface.ARMED, 104, payload))
        elif frame.message_type == runtime_test.START_TEST:
            self.state = runtime_test.RUNNING
            self.responses.append(ack_frame(105, frame.sequence, runtime_test.START_TEST))
            self.state = runtime_test.FAULT
            fault = struct.pack("<BIHHI", 2, self.test_id, 7, 0, 4567)
            complete = struct.pack("<BIBI", 2, self.test_id, 2, 2001)
            self.responses.append(protocol.encode_frame(runtime_test.FAULT_REPORT, 106, fault))
            self.responses.append(protocol.encode_frame(runtime_test.TEST_COMPLETE, 107, complete))

    def write(self, data):
        frames = self.parser.feed(data)
        self.assert_single_frame = len(frames) == 1
        self.writes.append(frames[0])
        self._response(frames[0])
        return len(data)

    def flush(self):
        pass

    def read(self, _size):
        self.clock.now += 0.01
        return self.responses.popleft() if self.responses else b""


class PlantRuntimeTimeoutUtilityTests(unittest.TestCase):
    def test_cli_arguments_and_fixed_config_payload(self):
        args = runtime_test.build_parser().parse_args(["--port", "COM3", "--test-id", "0x2A"])
        self.assertEqual((args.port, args.baud, args.test_id, args.timeout),
                         ("COM3", 115200, 42, 8.0))
        payload = runtime_test.timeout_test_config_payload(args.test_id)
        self.assertEqual(len(payload), 30)
        role, tid, initial, ambient, tau, gain, step, telemetry, max_run, disturbances = struct.unpack(
            "<BIiiIiHHIB", payload)
        self.assertEqual((role, tid, initial, ambient, tau, gain, step, telemetry,
                          max_run, disturbances), (2, 42, 20000, 20000, 300, 20, 5000, 5000, 2000, 0))
        for bad in ("0", "0x100000000", "-1"):
            with self.subTest(test_id=bad), self.assertRaises(SystemExit):
                runtime_test.build_parser().parse_args(["--port", "COM3", "--test-id", bad])

    def test_state_gate_and_ready_flags(self):
        runtime_test.verify_wait_config(runtime_test.PlantStatus(2, 0, 0, 0))
        with self.assertRaisesRegex(runtime_test.PlantRuntimeTestError, "refusing to reset"):
            runtime_test.verify_wait_config(runtime_test.PlantStatus(3, 0, 42, 9))
        runtime_test.verify_ready(runtime_test.PlantStatus(3, 0, 42, 9), 42)
        with self.assertRaisesRegex(runtime_test.PlantRuntimeTestError, "PLANT_VALID"):
            runtime_test.verify_ready(runtime_test.PlantStatus(3, 0, 42, 8), 42)
        runtime_test.verify_fault_status(runtime_test.PlantStatus(6, 7, 42, 8), 42)
        with self.assertRaisesRegex(runtime_test.PlantRuntimeTestError, "PLANT_VALID"):
            runtime_test.verify_fault_status(runtime_test.PlantStatus(6, 7, 42, 9), 42)

    def test_config_ack_and_nack_processing(self):
        good = protocol.Frame(runtime_test.ACK, 10,
                              struct.pack("<HBBH", 5, runtime_test.CONFIG_SET, 2, 0))
        self.assertTrue(runtime_test.matching_ack(good, 5, runtime_test.CONFIG_SET))
        self.assertIsNone(runtime_test.matching_ack(good, 6, runtime_test.CONFIG_SET))
        nack = protocol.Frame(runtime_test.NACK, 11,
                              struct.pack("<HBBH", 5, runtime_test.CONFIG_SET, 2, 5))
        with self.assertRaisesRegex(runtime_test.PlantRuntimeTestError, "NACK error=5"):
            runtime_test.matching_ack(nack, 5, runtime_test.CONFIG_SET)

    def test_fault_report_and_completion_validation(self):
        report = struct.pack("<BIHHI", 2, 42, 7, 0, 8001)
        parsed_report = runtime_test.decode_fault_report(report, 42)
        self.assertEqual((parsed_report.fault_code, parsed_report.detail, parsed_report.uptime_ms),
                         (7, 0, 8001))
        wrong_fault = struct.pack("<BIHHI", 2, 42, 5, 0, 8001)
        with self.assertRaisesRegex(runtime_test.PlantRuntimeTestError, "expected max-runtime fault 7"):
            runtime_test.decode_fault_report(wrong_fault, 42)

        complete = struct.pack("<BIBI", 2, 42, runtime_test.COMPLETE_FAULT_ABORT, 2000)
        parsed_complete = runtime_test.decode_test_complete(complete, 42)
        self.assertEqual((parsed_complete.outcome, parsed_complete.elapsed_ms), (2, 2000))
        normal = struct.pack("<BIBI", 2, 42, 0, 2000)
        with self.assertRaisesRegex(runtime_test.PlantRuntimeTestError, "expected FAULT_ABORT"):
            runtime_test.decode_test_complete(normal, 42)
        too_late = struct.pack("<BIBI", 2, 42, 2, 5000)
        with self.assertRaisesRegex(runtime_test.PlantRuntimeTestError, "outside"):
            runtime_test.decode_test_complete(too_late, 42)

    def test_full_sequence_confirms_ready_edges_arm_start_and_runtime_fault(self):
        clock = Clock()
        port = FakePlantPort(clock)
        output = []
        prompts = iter(("DISCONNECTED", "", ""))
        runner = runtime_test.PlantRuntimeTimeoutTest(
            port, 42, 8.0, response_timeout=1.0, level_timeout=1.0,
            poll_interval=0.01, output=output.append,
            input_fn=lambda _prompt: next(prompts), clock=clock, sleep_fn=clock.sleep,
        )
        self.assertTrue(runner.run())
        types = [frame.message_type for frame in port.writes]
        self.assertEqual(types.count(runtime_test.CONFIG_SET), 1)
        self.assertEqual(types.count(runtime_test.ARM_TEST), 1)
        self.assertEqual(types.count(runtime_test.START_TEST), 1)
        self.assertNotIn(0x13, types)  # STOP_TEST is never sent.
        self.assertTrue(any("STATUS confirms state=FAULT" in line for line in output))
        self.assertTrue(any("Plant-reported run elapsed: 2001 ms" in line for line in output))
        self.assertTrue(any("significantly before" in line for line in output))

    def test_unexpected_start_state_exits_without_config_or_reset(self):
        clock = Clock()
        port = FakePlantPort(clock)
        port.state = runtime_test.READY
        runner = runtime_test.PlantRuntimeTimeoutTest(
            port, 42, 8.0, response_timeout=0.2, output=lambda _line: None,
            input_fn=lambda _prompt: self.fail("should exit before prompting"),
            clock=clock, sleep_fn=clock.sleep,
        )
        self.assertFalse(runner.run())
        self.assertEqual([frame.message_type for frame in port.writes], [protocol.STATUS_REQUEST])

    def test_status_timeout_fails_without_configuration(self):
        class SilentPort:
            def __init__(self, clock):
                self.clock = clock
                self.writes = []

            def write(self, data):
                self.writes.append(data)
                return len(data)

            def flush(self):
                pass

            def read(self, _size):
                self.clock.now += 0.01
                return b""

        clock = Clock()
        port = SilentPort(clock)
        runner = runtime_test.PlantRuntimeTimeoutTest(
            port, 42, 8.0, response_timeout=0.05, output=lambda _line: None,
            clock=clock, sleep_fn=clock.sleep,
        )
        self.assertFalse(runner.run())
        sent = protocol.StreamParser().feed(port.writes[0])
        self.assertEqual([frame.message_type for frame in sent], [protocol.STATUS_REQUEST])

    def test_runtime_timeout_does_not_send_second_start(self):
        clock = Clock()
        port = FakePlantPort(clock)
        original_response = port._response

        def remain_running(frame):
            original_response(frame)
            if frame.message_type == runtime_test.START_TEST:
                port.responses = deque((port.responses[0],))  # matching ACK only
                port.state = runtime_test.RUNNING
            elif frame.message_type == protocol.STATUS_REQUEST and port.state == runtime_test.FAULT:
                port.state = runtime_test.RUNNING

        port._response = remain_running
        prompts = iter(("DISCONNECTED", "", ""))
        runner = runtime_test.PlantRuntimeTimeoutTest(
            port, 42, 0.1, response_timeout=0.2, level_timeout=0.2,
            poll_interval=0.02, output=lambda _line: None,
            input_fn=lambda _prompt: next(prompts), clock=clock, sleep_fn=clock.sleep,
        )
        self.assertFalse(runner.run())
        types = [frame.message_type for frame in port.writes]
        self.assertEqual(types.count(runtime_test.START_TEST), 1)


if __name__ == "__main__":
    unittest.main()
