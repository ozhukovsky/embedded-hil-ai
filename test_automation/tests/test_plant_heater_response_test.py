import csv
import io
import math
import struct
import sys
import tempfile
import threading
import unittest
from collections import deque
from contextlib import redirect_stderr
from pathlib import Path
from unittest.mock import patch

TOOLS = Path(__file__).resolve().parents[1] / "tools"
sys.path.insert(0, str(TOOLS))
import hil_smoke_test as protocol  # noqa: E402
import plant_heater_response_test as heater  # noqa: E402
import plant_interface_test as interface  # noqa: E402
import plant_timing_test as timing  # noqa: E402


class Clock:
    def __init__(self):
        self.now = 0.0

    def __call__(self):
        return self.now


class FakePlant:
    """Protocol peer with the firmware's integrate -> sample -> DAC -> telemetry order."""

    def __init__(self, clock):
        self.clock = clock
        self.state, self.test_id = heater.runtime.WAIT_CONFIG, 0
        self.heater_high = False
        self.latched = 0
        self.temperature = 20000.0
        self.responses = deque()
        self.writes = []
        self.parser = protocol.StreamParser()
        self.sequence = 0
        self.next_step = None
        self.index = 0
        self.run_start = None
        self.next_heartbeat = 1.0
        self.uptime_base = 0
        self.drop_index = None
        self.drop_start_ack = self.drop_stop_complete = self.drop_running_status = False
        self.drop_all_telemetry = self.reject_start = False
        self.armed_flags = 0x17
        self.fault_after = None
        self.serial_error_once = False
        self.complete_outcome = None

    def uptime(self, now=None):
        return (self.uptime_base + int((self.clock() if now is None else now) * 1000)) & 0xFFFFFFFF

    def push(self, kind, payload):
        self.responses.append(protocol.encode_frame(kind, self.sequence, payload))
        self.sequence = (self.sequence + 1) & 0xFFFF

    def ack(self, frame):
        self.push(interface.ACK, struct.pack("<HBBH", frame.sequence, frame.message_type, 2, 0))

    def write(self, data):
        frame, = self.parser.feed(data)
        self.writes.append(frame)
        if frame.message_type == protocol.STATUS_REQUEST:
            if self.state == heater.runtime.RUNNING and self.drop_running_status:
                return len(data)
            flags = (0x09 if self.state in (3, 4, 5) else 0x08 if self.state == 7 else 0)
            flags |= 2 if self.heater_high else 0
            self.push(protocol.STATUS, struct.pack("<BBHIIH", 2, self.state, 0, self.test_id,
                                                  self.uptime(), flags))
        elif frame.message_type == heater.plant_bringup.CONFIG_SET:
            self.test_id = int.from_bytes(frame.payload[1:5], "little")
            self.state = 3
            self.ack(frame)
        elif frame.message_type == interface.ARM_TEST:
            self.state = 4
            self.ack(frame)
            self.push(interface.ARMED, struct.pack("<BIH", 2, self.test_id, self.armed_flags))
        elif frame.message_type == timing.START_TEST:
            if self.reject_start:
                self.push(interface.NACK, struct.pack("<HBBH", frame.sequence, timing.START_TEST, 2, 3))
                return len(data)
            self.state, self.run_start = 5, self.clock()
            self.next_step = self.run_start + 5
            self.latched = int(self.heater_high)
            if not self.drop_start_ack:
                self.ack(frame)
        elif frame.message_type == timing.STOP_TEST:
            self.state = 7
            self.ack(frame)
            outcome = int(frame.payload[5] != 0) if self.complete_outcome is None else self.complete_outcome
            if not self.drop_stop_complete:
                self.push(heater.runtime.TEST_COMPLETE,
                          struct.pack("<BIBI", 2, self.test_id, outcome,
                                      int((self.clock() - self.run_start) * 1000)))
        return len(data)

    def read(self, _size):
        self.clock.now += 0.1
        if self.state == 5:
            if self.serial_error_once:
                self.serial_error_once = False
                raise OSError("simulated serial disconnect")
            if self.fault_after is not None and self.clock() - self.run_start >= self.fault_after:
                self.state = 6
                self.push(heater.runtime.FAULT_REPORT,
                          struct.pack("<BIHHI", 2, self.test_id, 2, 1, self.uptime()))
            while self.state == 5 and self.clock() >= self.next_step:
                self.temperature += 5 * ((20000 - self.temperature) / 30 + 200 * self.latched)
                self.latched = int(self.heater_high)
                temp = math.floor(self.temperature + 0.5)
                code = int(100.0 + ((temp / 1000.0 + 20) / 80) * 3900 + 0.5)
                if not self.drop_all_telemetry and self.index != self.drop_index:
                    self.push(timing.TELEMETRY,
                              struct.pack("<BIIIiiBHB", 2, self.test_id, self.index,
                                          self.uptime(self.next_step), temp, 20000, self.latched, code, 1))
                self.index += 1
                self.next_step += 5
        if self.clock() >= self.next_heartbeat:
            self.next_heartbeat += 1
            self.push(protocol.HEARTBEAT,
                      struct.pack("<BBII", 2, self.state, self.test_id, self.uptime()))
        return self.responses.popleft() if self.responses else b""


class ScriptPrompts:
    def __init__(self, plant, clock, delays=None, safe="SAFE"):
        self.plant, self.clock = plant, clock
        self.answers = [safe, "DISCONNECTED", "", "", "", ""]
        self.delays = delays or [0.2] * 6
        self.index = -1
        self.started_at = None

    def begin(self, _text):
        self.index += 1
        self.started_at = self.clock()
        if self.index in (2, 4):
            self.plant.heater_high = True
        elif self.index in (3, 5):
            self.plant.heater_high = False

    def poll(self):
        if self.clock() - self.started_at >= self.delays[self.index]:
            return self.answers[self.index]
        return None


def make_sample(index, temperature, next_input, uptime=None):
    return heater.Telemetry(42, index, (index + 1) * 5000 if uptime is None else uptime,
                            temperature, 20000, next_input, heater.temperature_to_dac(temperature), 1)


class HeaterResponseUtilityTests(unittest.TestCase):
    def runner(self, **plant_changes):
        clock = Clock()
        plant = FakePlant(clock)
        for key, value in plant_changes.items():
            setattr(plant, key, value)
        output = []
        prompts = ScriptPrompts(plant, clock)
        runner = heater.HeaterResponseTest(plant, 42, 3.29, output=output.append,
                                           clock=clock, prompts=prompts)
        return runner, plant, prompts, output

    def test_config_and_arguments(self):
        values = struct.unpack("<BIiiIiHHIB", heater.config_payload(42))
        self.assertEqual(values, (2, 42, 20000, 20000, 30, 200, 5000, 5000, 120000, 0))
        args = heater.build_parser().parse_args(["--port", "COM3", "--test-id", "0x2A", "--vcc", "3.27"])
        self.assertEqual((args.test_id, args.vcc), (42, 3.27))
        for args in (["--port", "COM3"], ["--port", "COM3", "--vcc", "nan"],
                     ["--port", "COM3", "--vcc", "3.29", "--test-id", "0"]):
            with redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                heater.build_parser().parse_args(args)

    def test_wait_config_and_safety_gate(self):
        runner, plant, _, _ = self.runner(state=3)
        self.assertFalse(runner.run())
        self.assertEqual([f.message_type for f in plant.writes], [protocol.STATUS_REQUEST])
        runner, plant, prompts, _ = self.runner()
        prompts.answers[0] = "no"
        self.assertFalse(runner.run())
        self.assertNotIn(heater.plant_bringup.CONFIG_SET, [f.message_type for f in plant.writes])

    def test_telemetry_decode_rejects_wrong_role_id_length_and_fields(self):
        good = struct.pack("<BIIIiiBHB", 2, 42, 0, 5000, 20000, 20000, 0, 2050, 1)
        self.assertEqual(heater.decode_telemetry(good, 42).dac_code, 2050)
        cases = [good[:-1], bytes((1,)) + good[1:], good[:-1] + b"\x02"]
        for bad in cases:
            with self.subTest(payload=bad), self.assertRaises(heater.ValidationError):
                heater.decode_telemetry(bad, 42)
        with self.assertRaises(heater.ValidationError):
            heater.decode_telemetry(good, 43)

    def test_one_step_latching_and_reference_heating_cooling(self):
        validator = heater.ResponseValidator(3.29)
        validator.add(make_sample(0, 20000, 0))
        validator.phase = "heating"
        transition = validator.add(make_sample(1, 20000, 1))
        self.assertEqual((transition["completed_input"], validator.counts["heating"]), (0, 0))
        for index, temp in enumerate((21000, 21833, 22528, 23106), 2):
            validator.add(make_sample(index, temp, 1))
        self.assertEqual(validator.counts["heating"], 4)
        validator.phase = "cooling"
        transition = validator.add(make_sample(6, 23589, 0))
        self.assertEqual((transition["completed_input"], validator.counts["cooling"]), (1, 0))
        for index, temp in enumerate((22991, 22492, 22077, 21731), 7):
            validator.add(make_sample(index, temp, 0))
        self.assertEqual(validator.counts["cooling"], 4)
        self.assertAlmostEqual(validator.records[0]["expected_vout_V"], 3.29 * 2050 / 4096)

    def test_same_sample_is_not_counted_twice_and_gaps_are_incomplete(self):
        validator = heater.ResponseValidator(3.29)
        sample = make_sample(0, 20000, 0)
        validator.add(sample)
        self.assertIsNone(validator.add(sample))
        self.assertEqual(validator.counts["baseline"], 1)
        with self.assertRaises(heater.IncompleteRun):
            validator.add(make_sample(2, 20000, 0))
        with self.assertRaises(heater.IncompleteRun):
            heater.ResponseValidator(3.29).add(make_sample(1, 20000, 0))

    def test_timestamp_order_and_wraparound(self):
        validator = heater.ResponseValidator(3.29)
        validator.add(make_sample(0, 20000, 0, 0xFFFFFF00))
        validator.add(make_sample(1, 20000, 0, (0xFFFFFF00 + 5000) & 0xFFFFFFFF))
        with self.assertRaisesRegex(heater.ValidationError, "interval"):
            validator.add(make_sample(2, 20000, 0, 100))

    def test_dac_mapping_and_temperature_tolerances(self):
        self.assertEqual([heater.temperature_to_dac(t) for t in (-20000, 20000, 60000)],
                         [100, 2050, 4000])
        wrong = make_sample(0, 20000, 0)
        wrong = heater.Telemetry(**dict(heater.asdict(wrong), dac_code=2053))
        with self.assertRaisesRegex(heater.ValidationError, "DAC"):
            heater.ResponseValidator(3.29).add(wrong)
        with self.assertRaisesRegex(heater.ValidationError, "Euler"):
            heater.ResponseValidator(3.29).add(make_sample(0, 20010, 0))

    def test_full_run_starts_once_stops_normally_and_writes_csv(self):
        runner, plant, _, output = self.runner()
        self.assertTrue(runner.run(), "\n".join(output))
        self.assertEqual([f.message_type for f in plant.writes].count(timing.START_TEST), 1)
        stops = [f for f in plant.writes if f.message_type == timing.STOP_TEST]
        self.assertEqual([f.payload for f in stops], [struct.pack("<BIB", 2, 42, 0)])
        self.assertGreaterEqual(runner.validator.counts["heating"], 4)
        self.assertGreaterEqual(runner.validator.counts["cooling"], 4)
        self.assertTrue(any("final state=COMPLETE" in line for line in output))
        self.assertEqual(sum("STATUS confirms PB0 HIGH" in line for line in output), 2)
        self.assertEqual(sum("STATUS confirms PB0 LOW" in line for line in output), 3)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "result.csv"
            runner.write_csv(path)
            with path.open(encoding="utf-8", newline="") as stream:
                rows = list(csv.DictReader(stream))
            self.assertEqual(len(rows), len(runner.validator.records))
            self.assertIn("reference_temp_mC", rows[0])

    def test_serial_reception_continues_during_slow_console_prompt(self):
        runner, plant, prompts, output = self.runner()
        prompts.delays[4] = 12.0  # Multiple model steps arrive while Enter is still pending.
        original_begin, original_poll = prompts.begin, prompts.poll
        snapshot = None
        def begin(text):
            nonlocal snapshot
            original_begin(text)
            snapshot = (len(output), sum(f.message_type == protocol.STATUS_REQUEST for f in plant.writes))
        def poll():
            self.assertEqual(len(output), snapshot[0], "UART output disrupted the pending console prompt")
            self.assertEqual(sum(f.message_type == protocol.STATUS_REQUEST for f in plant.writes), snapshot[1],
                             "an operator prompt triggered unnecessary STATUS polling")
            return original_poll()
        prompts.begin, prompts.poll = begin, poll
        self.assertTrue(runner.run(), "\n".join(output))
        self.assertTrue(all(not row["validation_error"] for row in runner.validator.records))
        self.assertGreaterEqual(len(runner.validator.records), 11)
        self.assertTrue(any("received and validated while waiting" in line for line in output))

    def test_safe_input_is_usable_with_uart_reception_and_no_poll_flood(self):
        runner, plant, _, output = self.runner()
        runner._fresh_status()
        status_count = sum(f.message_type == protocol.STATUS_REQUEST for f in plant.writes)
        console = io.StringIO()
        keys = deque(zip((30, 33, 36, 39, 45), "SAFE\r"))
        input_thread = threading.get_ident()
        injected_status = False
        def read_key():
            nonlocal injected_status
            self.assertEqual(threading.get_ident(), input_thread)
            if plant.clock() >= 15 and not injected_status:
                plant.push(protocol.STATUS, struct.pack("<BBHIIH", 2, 2, 0, 0, plant.uptime(), 0))
                injected_status = True
            if keys and plant.clock() >= keys[0][0]:
                return keys.popleft()[1]
            return None
        runner.prompts = heater.ConsolePrompts(read_key=read_key, output=console)
        output_start = len(output)
        runner._prompt("Confirm safety", "SAFE")
        self.assertEqual(console.getvalue(), "Type SAFE: SAFE\n")
        self.assertEqual(output[output_start:], ["Confirm safety", "Manual setup: no operator timeout before START_TEST."])
        self.assertEqual(sum(f.message_type == protocol.STATUS_REQUEST for f in plant.writes), status_count)
        self.assertGreater(runner.last_rx, 44)
        self.assertTrue(injected_status)
        self.assertNotIn(timing.START_TEST, [f.message_type for f in plant.writes])

    def test_manual_setup_delay_is_separate_from_active_runtime_budget(self):
        runner, plant, prompts, output = self.runner()
        prompts.delays[:4] = (130.0, 45.0, 30.0, 30.0)
        self.assertTrue(runner.run(), "\n".join(output))
        self.assertGreater(plant.run_start, 235)
        self.assertLess(plant.clock() - plant.run_start, heater.HOST_STOP_AFTER_S)
        self.assertFalse(any("operator prompt timed out" in line for line in output))

    def test_fault_is_still_processed_while_waiting_for_running_prompt(self):
        runner, plant, prompts, output = self.runner(fault_after=8.0)
        prompts.delays[4] = 15.0
        self.assertFalse(runner.run())
        self.assertTrue(any("FAIL:" in line and "FAULT_REPORT" in line for line in output))
        self.assertLess(plant.clock() - plant.run_start, 10)
        self.assertNotIn(timing.STOP_TEST, [f.message_type for f in plant.writes])

    def test_missing_armed_readiness_never_starts(self):
        runner, plant, _, _ = self.runner(armed_flags=7)
        self.assertFalse(runner.run())
        self.assertNotIn(timing.START_TEST, [f.message_type for f in plant.writes])

    def test_prompt_timeout_aborts_once_and_never_restarts(self):
        runner, plant, prompts, output = self.runner()
        prompts.delays[4] = 25.0
        self.assertFalse(runner.run())
        self.assertTrue(any("INCOMPLETE" in line for line in output))
        self.assertEqual([f.message_type for f in plant.writes].count(timing.START_TEST), 1)
        self.assertEqual([f.payload[5] for f in plant.writes if f.message_type == timing.STOP_TEST], [2])

    def test_telemetry_gap_aborts_incomplete(self):
        runner, plant, _, output = self.runner(drop_index=2)
        self.assertFalse(runner.run())
        self.assertTrue(any("INCOMPLETE" in line for line in output))
        self.assertEqual([f.payload[5] for f in plant.writes if f.message_type == timing.STOP_TEST], [2])

    def test_unexpected_fault_is_failure_and_does_not_stop_faulted_board(self):
        runner, plant, _, output = self.runner(fault_after=12.0)
        self.assertFalse(runner.run())
        self.assertTrue(any("FAIL:" in line and "FAULT_REPORT" in line for line in output))
        self.assertNotIn(timing.STOP_TEST, [f.message_type for f in plant.writes])

    def test_missing_start_ack_sends_no_second_start_and_attempts_stop(self):
        runner, plant, _, output = self.runner(drop_start_ack=True)
        self.assertFalse(runner.run())
        self.assertEqual([f.message_type for f in plant.writes].count(timing.START_TEST), 1)
        self.assertEqual([f.payload[5] for f in plant.writes if f.message_type == timing.STOP_TEST], [2])
        self.assertFalse(any(line.startswith("PASS") for line in output))

    def test_status_timeout_and_serial_error_attempt_one_stop(self):
        for changes in ({"drop_running_status": True}, {"serial_error_once": True}):
            with self.subTest(changes=changes):
                runner, plant, _, output = self.runner(**changes)
                self.assertFalse(runner.run())
                self.assertEqual([f.message_type for f in plant.writes].count(timing.START_TEST), 1)
                self.assertEqual([f.message_type for f in plant.writes].count(timing.STOP_TEST), 1)
                self.assertFalse(any(line.startswith("PASS") for line in output))

    def test_missing_stop_completion_is_incomplete_without_stop_retry(self):
        runner, plant, _, output = self.runner(drop_stop_complete=True)
        self.assertFalse(runner.run())
        self.assertEqual([f.message_type for f in plant.writes].count(timing.STOP_TEST), 1)
        self.assertFalse(any(line.startswith("PASS") for line in output))

    def test_host_deadline_stops_incomplete_before_board_expiry(self):
        runner, plant, _, output = self.runner()
        with patch.object(heater, "HOST_STOP_AFTER_S", 35.0):
            self.assertFalse(runner.run())
        self.assertTrue(any("host runtime limit" in line for line in output))
        self.assertEqual([f.payload[5] for f in plant.writes if f.message_type == timing.STOP_TEST], [2])
        self.assertLess(plant.clock() - plant.run_start, 120)

    def test_telemetry_silence_is_incomplete_despite_heartbeats_and_status(self):
        runner, plant, _, output = self.runner(drop_all_telemetry=True)
        self.assertFalse(runner.run())
        self.assertTrue(any("TELEMETRY timed out" in line for line in output))
        self.assertEqual([f.payload[5] for f in plant.writes if f.message_type == timing.STOP_TEST], [2])

    def test_start_nack_does_not_retry_or_stop_a_known_unstarted_board(self):
        runner, plant, _, output = self.runner(reject_start=True)
        self.assertFalse(runner.run())
        self.assertEqual([f.message_type for f in plant.writes].count(timing.START_TEST), 1)
        self.assertNotIn(timing.STOP_TEST, [f.message_type for f in plant.writes])
        self.assertTrue(any("NACK" in line for line in output))

    def test_wrong_stop_outcome_is_not_a_pass_or_retried(self):
        runner, plant, _, output = self.runner(complete_outcome=1)
        self.assertFalse(runner.run())
        self.assertEqual([f.message_type for f in plant.writes].count(timing.STOP_TEST), 1)
        self.assertTrue(any("outcome" in line for line in output if line.startswith("FAIL")))

    def test_console_input_polling_and_editing_use_no_background_thread(self):
        keys = deque()
        console = io.StringIO()
        input_thread = threading.get_ident()
        def read_key():
            self.assertEqual(threading.get_ident(), input_thread)
            return keys.popleft() if keys else None
        prompts = heater.ConsolePrompts(read_key=read_key, output=console)
        prompts.begin("Type SAFE: ")
        self.assertIsNone(prompts.poll())
        keys.extend("SAFX\bE\r")
        self.assertEqual(prompts.poll(), "SAFE")
        self.assertEqual(console.getvalue(), "Type SAFE: SAFX\b \bE\n")
        self.assertFalse(prompts.active)
        prompts.begin("Press Enter: ")
        with self.assertRaisesRegex(RuntimeError, "already active"):
            prompts.begin("second stdin consumer")
        keys.append("\x03")
        with self.assertRaises(KeyboardInterrupt):
            prompts.poll()
        prompts.cancel()
        self.assertFalse(prompts.active)


if __name__ == "__main__":
    unittest.main()
