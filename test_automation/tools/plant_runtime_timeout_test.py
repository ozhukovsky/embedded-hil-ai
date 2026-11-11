#!/usr/bin/env python3
"""Validate Plant max_runtime_ms expiration before the first 5 s model step."""

from __future__ import annotations

import argparse
import math
import struct
import sys
import time
from collections import deque
from dataclasses import dataclass
from typing import Callable, Optional

try:
    import serial
except ImportError as exc:  # pragma: no cover - depends on host environment
    raise SystemExit("pyserial is required: python -m pip install pyserial") from exc

import hil_smoke_test as protocol
import plant_bringup
from plant_interface_test import (
    ACK,
    ARMED_HEATER_CHECK_PASSED,
    ARM_TEST,
    NACK,
    PLANT,
    READY,
    STATUS_DAC_VALID,
    STATUS_HEATER_HIGH,
    STATUS_PLANT_VALID,
    ArmResponseTracker,
    PlantInterfaceError,
    PlantStatus,
    decode_plant_status,
)

CONFIG_SET = plant_bringup.CONFIG_SET
START_TEST = 0x12
FAULT_REPORT = 0x31
TEST_COMPLETE = 0x32
WAIT_CONFIG = 2
ARMED_STATE = 4
RUNNING = 5
FAULT = 6
NACK_HARDWARE_NOT_READY = 7
COMPLETE_FAULT_ABORT = 2
MAX_RUNTIME_MS = 2000
MODEL_STEP_MS = 5000
HOST_SIGNIFICANT_LIMIT_MS = 4000
RESPONSE_TIMEOUT_S = 5.0
LEVEL_TIMEOUT_S = 30.0


class PlantRuntimeTestError(RuntimeError):
    """A protocol, state, safety-confirmation, or timing validation failure."""


@dataclass(frozen=True)
class FaultReport:
    fault_code: int
    detail: int
    uptime_ms: int


@dataclass(frozen=True)
class TestComplete:
    outcome: int
    elapsed_ms: int


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
                        help="nonzero test ID to configure (default: 1)")
    parser.add_argument("--timeout", type=positive_float, default=8.0,
                        help="overall timeout from START_TEST transmission through fault confirmation (default: 8 s)")
    parser.add_argument("--response-timeout", type=positive_float, default=RESPONSE_TIMEOUT_S,
                        help="timeout for each setup/command response (default: 5 s)")
    parser.add_argument("--level-timeout", type=positive_float, default=LEVEL_TIMEOUT_S,
                        help="timeout for each manual PB0 level observation (default: 30 s)")
    parser.add_argument("--poll-interval", type=positive_float, default=0.25,
                        help="STATUS_REQUEST interval during manual and runtime observation (default: 0.25 s)")
    return parser


def timeout_test_config_payload(test_id: int) -> bytes:
    """Reuse the bring-up serializer and set its existing max_runtime field to 2 s."""
    if not 1 <= test_id <= 0xFFFFFFFF:
        raise ValueError("test_id must be a nonzero u32")
    payload = bytearray(plant_bringup.default_config_payload(test_id))
    if len(payload) != 30:
        raise PlantRuntimeTestError("existing Plant CONFIG_SET serializer returned an unexpected size")
    struct.pack_into("<I", payload, 25, MAX_RUNTIME_MS)
    return bytes(payload)


def verify_wait_config(status: PlantStatus) -> None:
    if status.state != WAIT_CONFIG:
        raise PlantRuntimeTestError(
            f"Plant is state {status.state}, expected WAIT_CONFIG (2); refusing to reset or reconfigure it"
        )
    if status.fault_code != 0:
        raise PlantRuntimeTestError(f"Plant reports fault_code={status.fault_code} before configuration")


def verify_ready(status: PlantStatus, test_id: int) -> None:
    if status.state != READY:
        raise PlantRuntimeTestError(f"Plant state is {status.state}, expected READY (3) after CONFIG_SET")
    if status.fault_code != 0 or status.test_id != test_id:
        raise PlantRuntimeTestError("READY STATUS has an unexpected fault code or test_id")
    flags = STATUS_PLANT_VALID | STATUS_DAC_VALID
    if status.status_flags & flags != flags:
        raise PlantRuntimeTestError("READY STATUS lacks PLANT_VALID or I2C/DAC-valid flag")


def verify_fault_status(status: PlantStatus, test_id: int) -> None:
    if status.state != FAULT or status.fault_code != 7 or status.test_id != test_id:
        raise PlantRuntimeTestError("STATUS does not confirm Plant FAULT state/code 7 for this test")
    if status.status_flags & STATUS_PLANT_VALID:
        raise PlantRuntimeTestError("Plant reports FAULT while PLANT_VALID is still HIGH")


def matching_ack(frame: protocol.Frame, command_sequence: int,
                 command_type: int) -> Optional[bool]:
    """Return ACK/NACK result for the expected command; None means unrelated frame."""
    if frame.message_type not in (ACK, NACK):
        return None
    if len(frame.payload) != 6:
        raise PlantRuntimeTestError("malformed ACK/NACK payload")
    sequence = int.from_bytes(frame.payload[0:2], "little")
    original_type = frame.payload[2]
    role = frame.payload[3]
    error = int.from_bytes(frame.payload[4:6], "little")
    if sequence != command_sequence:
        return None
    if original_type != command_type or role != PLANT:
        raise PlantRuntimeTestError("ACK/NACK does not match the expected Plant command")
    if frame.message_type == NACK:
        if not 1 <= error <= 9:
            raise PlantRuntimeTestError("NACK contains an invalid Protocol V1.0 error code")
        if error == NACK_HARDWARE_NOT_READY:
            raise PlantRuntimeTestError("Plant returned NACK HARDWARE_NOT_READY (7)")
        raise PlantRuntimeTestError(f"Plant rejected command with NACK error={error}")
    if error != 0:
        raise PlantRuntimeTestError("ACK has a nonzero error_code")
    return True


def decode_fault_report(payload: bytes, test_id: int) -> FaultReport:
    if len(payload) != 13:
        raise PlantRuntimeTestError("malformed FAULT_REPORT payload")
    role = payload[0]
    received_test_id = int.from_bytes(payload[1:5], "little")
    code = int.from_bytes(payload[5:7], "little")
    detail = int.from_bytes(payload[7:9], "little")
    uptime = int.from_bytes(payload[9:13], "little")
    if role != PLANT or received_test_id != test_id:
        raise PlantRuntimeTestError("FAULT_REPORT role or test_id does not match this Plant test")
    if not 1 <= code <= 7:
        raise PlantRuntimeTestError("FAULT_REPORT contains an invalid fault code")
    if code != 7:
        raise PlantRuntimeTestError(f"unexpected Plant fault code {code}; expected max-runtime fault 7")
    if detail != 0:
        raise PlantRuntimeTestError(f"fault 7 detail was {detail}; expected 0")
    return FaultReport(code, detail, uptime)


def decode_test_complete(payload: bytes, test_id: int) -> TestComplete:
    if len(payload) != 10:
        raise PlantRuntimeTestError("malformed TEST_COMPLETE payload")
    role = payload[0]
    received_test_id = int.from_bytes(payload[1:5], "little")
    outcome = payload[5]
    elapsed = int.from_bytes(payload[6:10], "little")
    if role != PLANT or received_test_id != test_id:
        raise PlantRuntimeTestError("TEST_COMPLETE role or test_id does not match this Plant test")
    if outcome != COMPLETE_FAULT_ABORT:
        raise PlantRuntimeTestError(f"unexpected TEST_COMPLETE outcome {outcome}; expected FAULT_ABORT (2)")
    if not MAX_RUNTIME_MS <= elapsed < MODEL_STEP_MS:
        raise PlantRuntimeTestError(
            f"Plant-reported run elapsed {elapsed} ms is outside [{MAX_RUNTIME_MS}, {MODEL_STEP_MS}) ms"
        )
    return TestComplete(outcome, elapsed)


def describe_frame(frame: protocol.Frame, test_id: int) -> str:
    if frame.message_type == protocol.HEARTBEAT:
        if len(frame.payload) != 10:
            raise PlantRuntimeTestError("malformed HEARTBEAT payload")
        role, state, current_test, uptime = struct.unpack("<BBII", frame.payload)
        if role != PLANT or state > 7:
            raise PlantRuntimeTestError("HEARTBEAT has invalid Plant role or state")
        return f"HEARTBEAT role={role} state={state} test_id={current_test} uptime_ms={uptime} seq={frame.sequence}"
    if frame.message_type == protocol.STATUS:
        status = decode_plant_status(frame.payload)
        return (f"STATUS role={PLANT} state={status.state} test_id={status.test_id} "
                f"fault={status.fault_code} flags=0x{status.status_flags:04X} seq={frame.sequence}")
    if frame.message_type == protocol.BOOT_REPORT:
        return protocol.describe(frame)
    if frame.message_type == 0x30:  # Protocol V1.0 TELEMETRY
        if len(frame.payload) != 25:
            raise PlantRuntimeTestError("malformed Plant TELEMETRY payload")
        role, tid, sample, uptime, temp, ambient, heater, dac, valid = struct.unpack(
            "<BIIIiiBHB", frame.payload
        )
        if role != PLANT or heater > 1 or dac > 4095 or valid > 1:
            raise PlantRuntimeTestError("Plant TELEMETRY contains an invalid field")
        return (f"TELEMETRY role={role} test_id={tid} sample_index={sample} uptime_ms={uptime} "
                f"simulated_temp_mC={temp} ambient_temp_mC={ambient} heater_input={heater} "
                f"dac_code={dac} plant_valid={valid} seq={frame.sequence}")
    if frame.message_type == FAULT_REPORT:
        report = decode_fault_report(frame.payload, test_id)
        return (f"FAULT_REPORT role={PLANT} test_id={test_id} fault_code={report.fault_code} "
                f"detail={report.detail} uptime_ms={report.uptime_ms} seq={frame.sequence}")
    if frame.message_type == TEST_COMPLETE:
        complete = decode_test_complete(frame.payload, test_id)
        return (f"TEST_COMPLETE role={PLANT} test_id={test_id} outcome={complete.outcome} "
                f"elapsed_ms={complete.elapsed_ms} seq={frame.sequence}")
    return protocol.describe(frame)


class PlantRuntimeTimeoutTest:
    def __init__(self, port, test_id: int, run_timeout: float,
                 response_timeout: float = RESPONSE_TIMEOUT_S,
                 level_timeout: float = LEVEL_TIMEOUT_S,
                 poll_interval: float = 0.25,
                 output: Callable[[str], None] = print,
                 input_fn: Callable[[str], str] = input,
                 clock: Callable[[], float] = time.monotonic,
                 sleep_fn: Callable[[float], None] = time.sleep):
        self.port = port
        self.test_id = test_id
        self.run_timeout = run_timeout
        self.response_timeout = response_timeout
        self.level_timeout = level_timeout
        self.poll_interval = poll_interval
        self.output = output
        self.input_fn = input_fn
        self.clock = clock
        self.sleep = sleep_fn
        self.parser = protocol.StreamParser()
        self.pending: deque[protocol.Frame] = deque()
        self.seen: set[tuple[int, int, bytes]] = set()
        self.tx_sequence = 0

    def _send(self, message_type: int, payload: bytes = b"") -> int:
        sequence = self.tx_sequence
        encoded = protocol.encode_frame(message_type, sequence, payload)
        self._write_encoded(encoded)
        self.tx_sequence = (self.tx_sequence + 1) & 0xFFFF
        return sequence

    def _write_encoded(self, encoded: bytes) -> None:
        written = self.port.write(encoded)
        if written is not None and written != len(encoded):
            raise PlantRuntimeTestError("serial write was incomplete")
        self.port.flush()

    def _next_frame(self, deadline: float) -> Optional[protocol.Frame]:
        while self.clock() < deadline:
            if self.pending:
                frame = self.pending.popleft()
                signature = (frame.message_type, frame.sequence, frame.payload)
                if signature in self.seen:
                    self.output(f"Duplicate frame ignored: {protocol.describe(frame)}")
                    continue
                self.seen.add(signature)
                self.output(describe_frame(frame, self.test_id))
                return frame
            self.pending.extend(self.parser.feed(self.port.read(256)))
        return None

    def _request_status(self, timeout: float) -> PlantStatus:
        sequence = self._send(protocol.STATUS_REQUEST)
        self.output(f"Sent STATUS_REQUEST seq={sequence}")
        deadline = self.clock() + timeout
        while self.clock() < deadline:
            frame = self._next_frame(deadline)
            if frame is None:
                break
            if frame.message_type == protocol.STATUS:
                return decode_plant_status(frame.payload)
        raise PlantRuntimeTestError("no valid Plant STATUS response before timeout")

    def _await_ack(self, command_sequence: int, command_type: int, timeout: float) -> None:
        deadline = self.clock() + timeout
        while self.clock() < deadline:
            frame = self._next_frame(deadline)
            if frame is None:
                break
            accepted = matching_ack(frame, command_sequence, command_type)
            if accepted:
                self.output(f"ACK received for message 0x{command_type:02X} seq={command_sequence}.")
                return
        raise PlantRuntimeTestError(f"no matching ACK for message 0x{command_type:02X} before timeout")

    def _wait_for_level(self, want_high: bool) -> bool:
        deadline = self.clock() + self.level_timeout
        while self.clock() < deadline:
            status = self._request_status(min(self.response_timeout, deadline - self.clock()))
            if status.state != READY or status.fault_code != 0 or status.test_id != self.test_id:
                raise PlantRuntimeTestError("Plant left READY or changed test_id during the PB0 diagnostic")
            required = STATUS_PLANT_VALID | STATUS_DAC_VALID
            if status.status_flags & required != required:
                raise PlantRuntimeTestError("Plant PLANT_VALID or I2C/DAC-valid flag dropped during diagnostic")
            level_high = bool(status.status_flags & STATUS_HEATER_HIGH)
            if level_high == want_high:
                return True
            self.sleep(min(self.poll_interval, max(0.0, deadline - self.clock())))
        return False

    def _arm(self) -> None:
        payload = bytes((PLANT,)) + self.test_id.to_bytes(4, "little")
        sequence = self._send(ARM_TEST, payload)
        self.output(f"Sent ARM_TEST seq={sequence} test_id={self.test_id}")
        tracker = ArmResponseTracker(sequence, self.test_id)
        deadline = self.clock() + self.response_timeout
        while self.clock() < deadline:
            frame = self._next_frame(deadline)
            if frame is None:
                break
            if tracker.consume(frame):
                flags = tracker.readiness_flags
                if flags is None or not flags & ARMED_HEATER_CHECK_PASSED:
                    raise PlantRuntimeTestError("ARMED readiness bit 4 is not set")
                self.output(f"ARMED readiness_flags=0x{flags:04X}; heater-interface bit 4=1.")
                return
        raise PlantRuntimeTestError("ARM_TEST did not produce matching ACK followed by ARMED")

    def _start(self) -> tuple[float, float]:
        payload = bytes((PLANT,)) + self.test_id.to_bytes(4, "little")
        sequence = self.tx_sequence
        encoded = protocol.encode_frame(START_TEST, sequence, payload)
        start_tx = self.clock()
        self._write_encoded(encoded)
        self.tx_sequence = (self.tx_sequence + 1) & 0xFFFF
        self.output(f"Sent START_TEST seq={sequence} test_id={self.test_id}; host timer started.")
        deadline = start_tx + self.run_timeout
        ack_deadline = min(deadline, self.clock() + self.response_timeout)
        while self.clock() < ack_deadline:
            frame = self._next_frame(ack_deadline)
            if frame is None:
                break
            accepted = matching_ack(frame, sequence, START_TEST)
            if accepted:
                ack_received = self.clock()
                self.output(f"START_TEST ACK observed after {(ack_received - start_tx) * 1000.0:.1f} ms.")
                return start_tx, deadline
        raise PlantRuntimeTestError("no matching START_TEST ACK; no retry was sent")

    def _wait_for_runtime_fault(self, start_tx: float, deadline: float) -> bool:
        report: Optional[FaultReport] = None
        report_host_time: Optional[float] = None
        completion: Optional[TestComplete] = None
        status_fault_seen = False
        next_status_request = self.clock()

        while self.clock() < deadline:
            if not status_fault_seen and self.clock() >= next_status_request:
                sequence = self._send(protocol.STATUS_REQUEST)
                self.output(f"Sent STATUS_REQUEST seq={sequence} while observing runtime.")
                next_status_request = self.clock() + self.poll_interval

            frame = self._next_frame(min(deadline, next_status_request if not status_fault_seen else deadline))
            if frame is None:
                continue

            if frame.message_type == FAULT_REPORT:
                current = decode_fault_report(frame.payload, self.test_id)
                if report is not None and current != report:
                    raise PlantRuntimeTestError("conflicting FAULT_REPORT frames received")
                if report is None:
                    report = current
                    report_host_time = self.clock()
                    self.output(f"Observed expected FAULT_REPORT code 7 at Plant uptime {report.uptime_ms} ms.")
            elif frame.message_type == TEST_COMPLETE:
                current_complete = decode_test_complete(frame.payload, self.test_id)
                if completion is not None and current_complete != completion:
                    raise PlantRuntimeTestError("conflicting TEST_COMPLETE frames received")
                completion = current_complete
                self.output(f"Observed fault-aborted TEST_COMPLETE elapsed={completion.elapsed_ms} ms.")
            elif frame.message_type == protocol.STATUS:
                status = decode_plant_status(frame.payload)
                if status.test_id != self.test_id:
                    raise PlantRuntimeTestError("runtime STATUS has an unexpected test_id")
                if status.state == RUNNING:
                    if status.fault_code != 0:
                        raise PlantRuntimeTestError("RUNNING STATUS unexpectedly carries a fault")
                elif status.state == FAULT:
                    verify_fault_status(status, self.test_id)
                    status_fault_seen = True
                    self.output("STATUS confirms state=FAULT, fault_code=7, PLANT_VALID=0.")
                else:
                    raise PlantRuntimeTestError(
                        f"unexpected Plant state {status.state} after START_TEST; expected RUNNING or FAULT"
                    )

            if report is not None and completion is not None and status_fault_seen:
                if report_host_time is None:
                    raise PlantRuntimeTestError("missing host timestamp for FAULT_REPORT")
                host_elapsed_ms = (report_host_time - start_tx) * 1000.0
                plant_run_ms = completion.elapsed_ms
                self.output(f"Host-observed START_TEST transmit to FAULT_REPORT: {host_elapsed_ms:.1f} ms.")
                self.output(f"Plant-reported uptime at fault: {report.uptime_ms} ms since reset.")
                self.output(f"Plant-reported run elapsed: {plant_run_ms} ms.")
                self.output(f"Thermal model update period: {MODEL_STEP_MS} ms.")
                if host_elapsed_ms >= HOST_SIGNIFICANT_LIMIT_MS:
                    raise PlantRuntimeTestError(
                        f"host-observed fault at {host_elapsed_ms:.1f} ms was not at least 1 s ahead of the 5 s model step"
                    )
                if not MAX_RUNTIME_MS <= plant_run_ms < MODEL_STEP_MS:
                    raise PlantRuntimeTestError("Plant-reported run elapsed does not confirm pre-model-step faulting")
                if plant_run_ms >= HOST_SIGNIFICANT_LIMIT_MS:
                    raise PlantRuntimeTestError(
                        f"Plant-reported fault at {plant_run_ms} ms was not at least 1 s ahead of the 5 s model step"
                    )
                self.output("PASS: max_runtime_ms fault occurred significantly before the first 5 s model update.")
                return True

        raise PlantRuntimeTestError(
            f"overall {self.run_timeout:g} s runtime timeout expired before FAULT_REPORT, "
            "fault-aborted TEST_COMPLETE, and FAULT/PLANT_VALID-low STATUS were all confirmed; "
            "do not run another test without safely recovering the Plant"
        )

    def run(self) -> bool:
        try:
            status = self._request_status(self.response_timeout)
            verify_wait_config(status)
            self.output("Plant is in WAIT_CONFIG after reset; test may proceed.")

            config = timeout_test_config_payload(self.test_id)
            sequence = self._send(CONFIG_SET, config)
            self.output(f"Sent CONFIG_SET seq={sequence} test_id={self.test_id} max_runtime_ms={MAX_RUNTIME_MS}.")
            self._await_ack(sequence, CONFIG_SET, self.response_timeout)
            ready = self._request_status(self.response_timeout)
            verify_ready(ready, self.test_id)
            self.output("Plant READY; PLANT_VALID=1; I2C/DAC_valid=1.")

            if ready.status_flags & STATUS_HEATER_HIGH:
                raise PlantRuntimeTestError("PB0 is not initially LOW; restore the 6.2 kΩ pull-down and retry")
            self.output("PB0 initial LOW confirmed by STATUS.")
            confirmation = self.input_fn(
                "Confirm BlackPill PB0 remains disconnected from BluePill PB0; type DISCONNECTED: "
            )
            if confirmation.strip() != "DISCONNECTED":
                raise PlantRuntimeTestError("BlackPill PB0 disconnect was not explicitly confirmed")

            self.input_fn("Apply 3.3 V to BluePill PB0; press Enter and hold HIGH until detected: ")
            if not self._wait_for_level(True):
                raise PlantRuntimeTestError("PB0 HIGH not observed; remove 3.3 V. ARM_TEST/START_TEST were not sent")
            self.output("PB0 HIGH observed by STATUS.")
            self.input_fn("Remove 3.3 V from PB0; press Enter and hold LOW until detected: ")
            if not self._wait_for_level(False):
                raise PlantRuntimeTestError("PB0 LOW not observed; ensure 3.3 V is removed. ARM_TEST/START_TEST were not sent")
            self.output("PB0 LOW observed by STATUS; both manual levels confirmed.")

            self._arm()
            start_tx, deadline = self._start()
            return self._wait_for_runtime_fault(start_tx, deadline)
        except (PlantRuntimeTestError, PlantInterfaceError) as exc:
            self.output(f"ERROR: {exc}")
            return False
        except (KeyboardInterrupt, EOFError):
            self.output("Interrupted. No automatic reset or STOP_TEST was sent; do not start another run until Plant state is checked.")
            return False


def print_safety_conditions(output: Callable[[str], None] = print) -> None:
    output("SAFETY REQUIREMENTS BEFORE STARTING:")
    output("- No physical heater, power switching stage, or other actuator may be connected.")
    output("- Keep BlackPill PB0 disconnected from BluePill PB0 throughout this test.")
    output("- Preserve the external 6.2 kΩ pull-down from BluePill PB0 to GND.")
    output("- Use only 3.3 V for the temporary PB0 diagnostic level; never use 5 V.")
    output("- Do not alter DAC wiring or power arrangement. The utility will not reset the Plant.")


def main(argv: Optional[list[str]] = None) -> int:
    args = build_parser().parse_args(argv)
    print_safety_conditions()
    try:
        confirmation = input("Type SAFE after confirming all conditions above: ")
    except (EOFError, KeyboardInterrupt):
        print("ERROR: safety confirmation was not provided.", file=sys.stderr)
        return 1
    if confirmation.strip() != "SAFE":
        print("ERROR: safety conditions were not explicitly confirmed.", file=sys.stderr)
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
        runner = PlantRuntimeTimeoutTest(
            port, args.test_id, args.timeout, args.response_timeout,
            args.level_timeout, args.poll_interval,
        )
        return 0 if runner.run() else 1
    except (serial.SerialException, TimeoutError, ValueError, PlantInterfaceError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    finally:
        if port is not None and port.is_open:
            port.close()


if __name__ == "__main__":
    raise SystemExit(main())
