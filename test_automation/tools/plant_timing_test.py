#!/usr/bin/env python3
"""Run an 18-to-50-second Plant timing observation and stop normally."""

from __future__ import annotations

import argparse
import math
import sys
import time
from typing import Callable, Optional

try:
    import serial
except ImportError as exc:  # pragma: no cover - depends on host environment
    raise SystemExit("pyserial is required: python -m pip install pyserial") from exc

import hil_smoke_test as protocol
import plant_bringup
import plant_interface_test as interface
import plant_runtime_timeout_test as runtime_test

PLANT = interface.PLANT
READY = interface.READY
WAIT_CONFIG = runtime_test.WAIT_CONFIG
ARMED = runtime_test.ARMED_STATE
RUNNING = runtime_test.RUNNING
COMPLETE = 7
CONFIG_SET = plant_bringup.CONFIG_SET
ARM_TEST = interface.ARM_TEST
START_TEST = runtime_test.START_TEST
STOP_TEST = 0x13
ACK = interface.ACK
NACK = interface.NACK
TELEMETRY = 0x30
TEST_COMPLETE = runtime_test.TEST_COMPLETE
COMPLETE_NORMAL = 0
STOP_REASON_NORMAL = 0
MAX_RUNTIME_MS = 60_000
MIN_OBSERVATION_S = 18.0
MAX_OBSERVATION_S = 50.0
RESPONSE_TIMEOUT_S = 5.0
LEVEL_TIMEOUT_S = 30.0
POLL_INTERVAL_S = 1.0


class PlantTimingTestError(RuntimeError):
    """A protocol, state, safety confirmation, or timing-test failure."""


def positive_float(value: str) -> float:
    try:
        parsed = float(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("must be a number greater than zero") from exc
    if not math.isfinite(parsed) or parsed <= 0:
        raise argparse.ArgumentTypeError("must be a finite number greater than zero")
    return parsed


def positive_u32(value: str) -> int:
    try:
        parsed = int(value, 0)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("must be a nonzero 32-bit integer") from exc
    if not 1 <= parsed <= 0xFFFFFFFF:
        raise argparse.ArgumentTypeError("must be from 1 through 4294967295")
    return parsed


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="Plant FT232RL COM port, e.g. COM3")
    parser.add_argument("--baud", type=positive_u32, default=115200,
                        help="USART1 baud rate (default: 115200)")
    parser.add_argument("--test-id", type=positive_u32, default=1,
                        help="nonzero test ID (default: 1)")
    parser.add_argument("--response-timeout", type=positive_float, default=RESPONSE_TIMEOUT_S,
                        help="timeout for each protocol response (default: 5 s)")
    parser.add_argument("--level-timeout", type=positive_float, default=LEVEL_TIMEOUT_S,
                        help="timeout for each PB0 level observation (default: 30 s)")
    parser.add_argument("--poll-interval", type=positive_float, default=POLL_INTERVAL_S,
                        help="STATUS polling interval while running (default: 1 s)")
    parser.add_argument("--observation-seconds", type=positive_float, default=MIN_OBSERVATION_S,
                        help="run duration from 18 through 50 seconds (default: 18)")
    return parser


def timing_config_payload(test_id: int) -> bytes:
    """Reuse the established CONFIG_SET serializer, overriding only max_runtime_ms."""
    if not 1 <= test_id <= 0xFFFFFFFF:
        raise ValueError("test_id must be a nonzero u32")
    payload = bytearray(plant_bringup.default_config_payload(test_id))
    if len(payload) != 30:
        raise PlantTimingTestError("existing Plant CONFIG_SET serializer returned an unexpected size")
    payload[25:29] = MAX_RUNTIME_MS.to_bytes(4, "little")
    return bytes(payload)


def decode_normal_test_complete(payload: bytes, test_id: int) -> int:
    """Validate the existing Protocol V1.0 TEST_COMPLETE layout."""
    if len(payload) != 10:
        raise PlantTimingTestError("malformed TEST_COMPLETE payload")
    role = payload[0]
    received_test_id = int.from_bytes(payload[1:5], "little")
    outcome = payload[5]
    elapsed_ms = int.from_bytes(payload[6:10], "little")
    if role != PLANT or received_test_id != test_id:
        raise PlantTimingTestError("TEST_COMPLETE role or test_id does not match this Plant test")
    if outcome != COMPLETE_NORMAL:
        raise PlantTimingTestError(f"unexpected TEST_COMPLETE outcome {outcome}; expected NORMAL (0)")
    if elapsed_ms < MIN_OBSERVATION_S * 1000:
        raise PlantTimingTestError(
            f"Plant-reported runtime {elapsed_ms} ms is shorter than the required 18000 ms"
        )
    return elapsed_ms


class PlantTimingTest:
    """Serial test runner using the existing framing, status, and ARM validators."""

    def __init__(self, port, test_id: int, response_timeout: float = RESPONSE_TIMEOUT_S,
                 level_timeout: float = LEVEL_TIMEOUT_S, poll_interval: float = POLL_INTERVAL_S,
                 observation_seconds: float = MIN_OBSERVATION_S,
                 output: Callable[[str], None] = print,
                 input_fn: Callable[[str], str] = input,
                 clock: Callable[[], float] = time.monotonic,
                 sleep_fn: Callable[[float], None] = time.sleep):
        if not MIN_OBSERVATION_S <= observation_seconds <= MAX_OBSERVATION_S:
            raise ValueError("observation_seconds must be from 18 through 50 seconds")
        self.port = port
        self.test_id = test_id
        self.response_timeout = response_timeout
        self.level_timeout = level_timeout
        self.poll_interval = poll_interval
        self.observation_seconds = observation_seconds
        self.output = output
        self.input_fn = input_fn
        self.clock = clock
        self.sleep = sleep_fn
        self.parser = protocol.StreamParser()
        self.pending = []
        self.tx_sequence = 0
        self.start_sent = False
        self.stop_sent = False
        self.counts = {protocol.HEARTBEAT: 0, protocol.STATUS: 0, TELEMETRY: 0}

    def _send(self, message_type: int, payload: bytes = b"") -> int:
        sequence = self.tx_sequence
        encoded = protocol.encode_frame(message_type, sequence, payload)
        written = self.port.write(encoded)
        if written is not None and written != len(encoded):
            raise PlantTimingTestError("serial write was incomplete")
        self.port.flush()
        self.tx_sequence = (self.tx_sequence + 1) & 0xFFFF
        return sequence

    def _report_frame(self, frame: protocol.Frame) -> None:
        if frame.message_type == TEST_COMPLETE:
            elapsed = decode_normal_test_complete(frame.payload, self.test_id)
            self.output(f"TEST_COMPLETE role=2 test_id={self.test_id} outcome=NORMAL elapsed_ms={elapsed}")
            return
        self.output(runtime_test.describe_frame(frame, self.test_id))

    def _next_frame(self, deadline: float) -> Optional[protocol.Frame]:
        while self.clock() < deadline:
            if self.pending:
                frame = self.pending.pop(0)
                self._report_frame(frame)
                return frame
            self.pending.extend(self.parser.feed(self.port.read(256)))
        return None

    def _status(self, timeout: float) -> interface.PlantStatus:
        sequence = self._send(protocol.STATUS_REQUEST)
        self.output(f"Sent STATUS_REQUEST seq={sequence}")
        deadline = self.clock() + timeout
        while self.clock() < deadline:
            frame = self._next_frame(deadline)
            if frame is None:
                break
            if frame.message_type == protocol.STATUS:
                return interface.decode_plant_status(frame.payload)
            if frame.message_type == NACK:
                raise PlantTimingTestError("unexpected NACK while waiting for STATUS")
        raise PlantTimingTestError("no valid Plant STATUS response before timeout")

    def _await_ack(self, sequence: int, message_type: int) -> None:
        deadline = self.clock() + self.response_timeout
        while self.clock() < deadline:
            frame = self._next_frame(deadline)
            if frame is None:
                break
            accepted = runtime_test.matching_ack(frame, sequence, message_type)
            if accepted:
                self.output(f"ACK received for message 0x{message_type:02X} seq={sequence}.")
                return
        raise PlantTimingTestError(f"no matching ACK for message 0x{message_type:02X}")

    def _wait_for_level(self, high: bool) -> bool:
        deadline = self.clock() + self.level_timeout
        while self.clock() < deadline:
            status = self._status(min(self.response_timeout, deadline - self.clock()))
            runtime_test.verify_ready(status, self.test_id)
            if status.heater_high == high:
                return True
            self.sleep(min(self.poll_interval, max(0.0, deadline - self.clock())))
        return False

    def _arm(self) -> None:
        payload = bytes((PLANT,)) + self.test_id.to_bytes(4, "little")
        sequence = self._send(ARM_TEST, payload)
        self.output(f"Sent ARM_TEST seq={sequence} test_id={self.test_id}")
        tracker = interface.ArmResponseTracker(sequence, self.test_id)
        deadline = self.clock() + self.response_timeout
        while self.clock() < deadline:
            frame = self._next_frame(deadline)
            if frame is None:
                break
            if tracker.consume(frame):
                flags = tracker.readiness_flags
                if flags is None or not flags & interface.ARMED_HEATER_CHECK_PASSED:
                    raise PlantTimingTestError("ARMED readiness bit 4 is not set")
                self.output(f"ARMED readiness_flags=0x{flags:04X}; heater-interface bit 4=1.")
                return
        raise PlantTimingTestError("ARM_TEST did not produce matching ACK followed by ARMED")

    def _start_once(self) -> float:
        if self.start_sent:
            raise PlantTimingTestError("START_TEST has already been transmitted; refusing a retry")
        payload = bytes((PLANT,)) + self.test_id.to_bytes(4, "little")
        self.start_sent = True
        sequence = self._send(START_TEST, payload)
        self.output(f"Sent START_TEST seq={sequence} test_id={self.test_id} (single transmission).")
        deadline = self.clock() + self.response_timeout
        while self.clock() < deadline:
            frame = self._next_frame(deadline)
            if frame is None:
                break
            if runtime_test.matching_ack(frame, sequence, START_TEST):
                accepted_at = self.clock()
                self.output("START_TEST ACK received; observation timer starts at acceptance.")
                return accepted_at
        raise PlantTimingTestError("no matching START_TEST ACK; START_TEST was not retried")

    def _observe(self, accepted_at: float) -> None:
        end = accepted_at + self.observation_seconds
        next_status = self.clock()
        while self.clock() < end:
            now = self.clock()
            if now >= next_status:
                sequence = self._send(protocol.STATUS_REQUEST)
                self.output(f"Sent STATUS_REQUEST seq={sequence} while observing.")
                next_status = now + self.poll_interval
            frame = self._next_frame(min(end, next_status))
            if frame is None:
                continue
            if frame.message_type in self.counts:
                self.counts[frame.message_type] += 1
            if frame.message_type == protocol.STATUS:
                status = interface.decode_plant_status(frame.payload)
                if status.test_id != self.test_id:
                    raise PlantTimingTestError("runtime STATUS has an unexpected test_id")
                if status.state != RUNNING or status.fault_code:
                    raise PlantTimingTestError(
                        f"Plant left RUNNING during observation (state={status.state}, fault={status.fault_code})"
                    )
        observed = self.clock() - accepted_at
        if observed < MIN_OBSERVATION_S:
            raise PlantTimingTestError("observation ended before 18 seconds after START_TEST acceptance")
        self.output(f"Observation complete: {observed:.1f} s after START_TEST acceptance.")
        self.output("Observed during window: " + ", ".join(
            f"{name}={self.counts[msg]}" for msg, name in (
                (protocol.HEARTBEAT, "HEARTBEAT"), (protocol.STATUS, "STATUS"),
                (TELEMETRY, "TELEMETRY"))))

    def _stop_once(self) -> None:
        if self.stop_sent:
            raise PlantTimingTestError("STOP_TEST has already been transmitted; refusing a retry")
        # hil_encode_stop_test(): target_role, test_id, stop_reason (NORMAL).
        payload = bytes((PLANT,)) + self.test_id.to_bytes(4, "little") + bytes((STOP_REASON_NORMAL,))
        self.stop_sent = True
        sequence = self._send(STOP_TEST, payload)
        self.output(f"Sent STOP_TEST seq={sequence} once; reason=NORMAL.")
        deadline = self.clock() + self.response_timeout
        acknowledged = False
        completion_ms: Optional[int] = None
        while self.clock() < deadline and completion_ms is None:
            frame = self._next_frame(deadline)
            if frame is None:
                break
            if frame.message_type in (ACK, NACK):
                if runtime_test.matching_ack(frame, sequence, STOP_TEST):
                    acknowledged = True
            elif frame.message_type == TEST_COMPLETE:
                if not acknowledged:
                    raise PlantTimingTestError("TEST_COMPLETE arrived before the matching STOP_TEST ACK")
                completion_ms = decode_normal_test_complete(frame.payload, self.test_id)
        if not acknowledged:
            raise PlantTimingTestError("STOP_TEST ACK was not received; STOP_TEST was not retried")
        if completion_ms is None:
            raise PlantTimingTestError("normal TEST_COMPLETE was not received after STOP_TEST ACK")
        self.output(f"Normal TEST_COMPLETE confirmed; Plant elapsed={completion_ms} ms.")

        status = self._status(self.response_timeout)
        if status.state != COMPLETE or status.fault_code or status.test_id != self.test_id:
            raise PlantTimingTestError(
                f"final STATUS does not confirm COMPLETE (state={status.state}, fault={status.fault_code})"
            )
        self.output("Final STATUS confirms Plant state=COMPLETE.")

    def run(self) -> bool:
        try:
            initial = self._status(self.response_timeout)
            runtime_test.verify_wait_config(initial)
            self.output("Plant is freshly reset and in WAIT_CONFIG; proceeding without reset.")

            config = timing_config_payload(self.test_id)
            sequence = self._send(CONFIG_SET, config)
            self.output(f"Sent CONFIG_SET seq={sequence} test_id={self.test_id} max_runtime_ms={MAX_RUNTIME_MS}.")
            self._await_ack(sequence, CONFIG_SET)
            ready = self._status(self.response_timeout)
            runtime_test.verify_ready(ready, self.test_id)
            self.output("Plant READY; PLANT_VALID=1; I2C/DAC_valid=1.")
            if ready.status_flags & interface.STATUS_HEATER_HIGH:
                raise PlantTimingTestError("PB0 must be LOW before the interface diagnostic")

            confirmation = self.input_fn(
                "Confirm BlackPill PB0 is disconnected from BluePill PB0; type DISCONNECTED: "
            )
            if confirmation.strip() != "DISCONNECTED":
                raise PlantTimingTestError("BlackPill PB0 disconnect was not explicitly confirmed")

            self.input_fn("Apply 3.3 V to BluePill PB0; press Enter and HOLD HIGH until detected: ")
            if not self._wait_for_level(True):
                raise PlantTimingTestError("PB0 HIGH was not observed; remove 3.3 V; ARM_TEST was not sent")
            self.output("PB0 HIGH observed by STATUS.")
            self.input_fn("Remove 3.3 V; press Enter and HOLD LOW until detected: ")
            if not self._wait_for_level(False):
                raise PlantTimingTestError("PB0 LOW was not observed; ARM_TEST was not sent")
            self.output("PB0 LOW observed by STATUS; both diagnostic levels confirmed.")

            self._arm()
            accepted_at = self._start_once()
            self._observe(accepted_at)
            self._stop_once()
            self.output("Plant timing observation passed. No firmware reset or START_TEST retry was performed.")
            self.output("DWT watch expressions after completion: p.max_cycles, p.over_budget, SystemCoreClock.")
            self.output("Do not reset/reflash/reinitialize before reading these RAM-resident values.")
            return True
        except (PlantTimingTestError, runtime_test.PlantRuntimeTestError,
                interface.PlantInterfaceError, EOFError, KeyboardInterrupt) as exc:
            self.output(f"ERROR: {exc}")
            if isinstance(exc, (EOFError, KeyboardInterrupt)):
                self.output("Interrupted; no automatic reset or START_TEST retry was performed.")
            return False


def print_safety_conditions(output: Callable[[str], None] = print) -> None:
    output("SAFETY REQUIREMENTS BEFORE STARTING:")
    output("- No physical heater, switching stage, or actuator may be connected.")
    output("- BlackPill PB0 must remain disconnected from BluePill PB0.")
    output("- Preserve the external 6.2 kOhm pull-down from BluePill PB0 to GND.")
    output("- Use only 3.3 V for the temporary PB0 diagnostic level; never use 5 V.")
    output("- Keep PB0 LOW during the timed run. Do not change DAC wiring or power arrangement.")
    output("- The utility will not reset the Plant and sends START_TEST only once.")


def main(argv: Optional[list[str]] = None) -> int:
    args = build_parser().parse_args(argv)
    if not MIN_OBSERVATION_S <= args.observation_seconds <= MAX_OBSERVATION_S:
        print("ERROR: --observation-seconds must be from 18 through 50.", file=sys.stderr)
        return 2
    print_safety_conditions()
    try:
        if input("Type SAFE after confirming every condition above: ").strip() != "SAFE":
            print("ERROR: safety confirmation was not provided.", file=sys.stderr)
            return 1
    except (EOFError, KeyboardInterrupt):
        print("ERROR: safety confirmation was not provided.", file=sys.stderr)
        return 1

    port = None
    try:
        port = serial.Serial(port=None, baudrate=args.baud, timeout=0.1,
                             write_timeout=args.response_timeout)
        port.port = args.port
        port.dtr = False
        port.rts = False
        port.open()
        print(f"Opened {args.port} for Plant at {args.baud} baud.")
        runner = PlantTimingTest(port, args.test_id, args.response_timeout,
                                 args.level_timeout, args.poll_interval,
                                 args.observation_seconds)
        return 0 if runner.run() else 1
    except (serial.SerialException, TimeoutError, ValueError,
            PlantTimingTestError, runtime_test.PlantRuntimeTestError,
            interface.PlantInterfaceError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    finally:
        if port is not None and port.is_open:
            port.close()


if __name__ == "__main__":
    raise SystemExit(main())
