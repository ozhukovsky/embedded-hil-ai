#!/usr/bin/env python3
"""Interactively verify the Plant PB0 heater-interface startup diagnostic."""

from __future__ import annotations

import argparse
import math
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

PLANT = 2
READY = 3
ARM_TEST = 0x11
ACK = 0x20
NACK = 0x21
ARMED = 0x22
STATUS_PLANT_VALID = 1 << 0
STATUS_HEATER_HIGH = 1 << 1
STATUS_DAC_VALID = 1 << 3
ARMED_HEATER_CHECK_PASSED = 1 << 4
PLANT_ARMED_ALLOWED_FLAGS = 0x0017
NACK_HARDWARE_NOT_READY = 7


class PlantInterfaceError(RuntimeError):
    """A protocol, state, timeout, or operator-confirmation failure."""


@dataclass(frozen=True)
class PlantStatus:
    state: int
    fault_code: int
    test_id: int
    status_flags: int

    @property
    def heater_high(self) -> bool:
        return bool(self.status_flags & STATUS_HEATER_HIGH)


def positive_float(value: str) -> float:
    try:
        parsed = float(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("must be a number greater than zero") from exc
    if not math.isfinite(parsed) or parsed <= 0:
        raise argparse.ArgumentTypeError("must be a finite number greater than zero")
    return parsed


def positive_int(value: str) -> int:
    try:
        parsed = int(value, 10)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("must be an integer greater than zero") from exc
    if parsed <= 0:
        raise argparse.ArgumentTypeError("must be greater than zero")
    return parsed


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="Plant FT232RL COM port, e.g. COM3")
    parser.add_argument("--baud", type=positive_int, default=115200,
                        help="USART1 baud rate (default: 115200)")
    parser.add_argument("--timeout", type=positive_float, default=5.0,
                        help="maximum seconds for each status/transition/ARM response (default: 5)")
    parser.add_argument("--poll-interval", type=positive_float, default=0.25,
                        help="seconds between STATUS_REQUEST polls (default: 0.25)")
    return parser


def decode_plant_status(payload: bytes) -> PlantStatus:
    if not protocol.valid_status_payload(payload, PLANT):
        raise PlantInterfaceError("malformed STATUS payload or response role is not Plant (2)")
    return PlantStatus(
        state=payload[1],
        fault_code=int.from_bytes(payload[2:4], "little"),
        test_id=int.from_bytes(payload[4:8], "little"),
        status_flags=int.from_bytes(payload[12:14], "little"),
    )


class ArmResponseTracker:
    """Validate the ordered ACK then ARMED response to one ARM_TEST."""

    def __init__(self, command_sequence: int, test_id: int):
        self.command_sequence = command_sequence
        self.test_id = test_id
        self.acknowledged = False
        self.readiness_flags: Optional[int] = None

    def consume(self, frame: protocol.Frame) -> bool:
        if frame.message_type in (ACK, NACK):
            if len(frame.payload) != 6:
                raise PlantInterfaceError("malformed ACK/NACK payload")
            ack_sequence = int.from_bytes(frame.payload[0:2], "little")
            ack_type = frame.payload[2]
            role = frame.payload[3]
            error_code = int.from_bytes(frame.payload[4:6], "little")
            if ack_sequence != self.command_sequence:
                return False
            if ack_type != ARM_TEST or role != PLANT:
                raise PlantInterfaceError("ACK/NACK does not identify this Plant ARM_TEST")
            if frame.message_type == NACK:
                reason = ("HARDWARE_NOT_READY" if error_code == NACK_HARDWARE_NOT_READY
                          else f"error {error_code}")
                raise PlantInterfaceError(f"Plant rejected ARM_TEST with NACK {reason}")
            if error_code != 0:
                raise PlantInterfaceError(f"invalid ARM_TEST ACK error_code={error_code}")
            if self.acknowledged:
                raise PlantInterfaceError("duplicate ACK received before ARMED")
            self.acknowledged = True
            return False

        if frame.message_type == ARMED:
            if not self.acknowledged:
                raise PlantInterfaceError("ARMED arrived before its required ACK")
            if len(frame.payload) != 7:
                raise PlantInterfaceError("malformed ARMED payload")
            role = frame.payload[0]
            test_id = int.from_bytes(frame.payload[1:5], "little")
            flags = int.from_bytes(frame.payload[5:7], "little")
            if role != PLANT or test_id != self.test_id:
                raise PlantInterfaceError("ARMED role or test_id does not match the active Plant")
            if flags & ~PLANT_ARMED_ALLOWED_FLAGS:
                raise PlantInterfaceError("ARMED contains reserved Plant readiness flags")
            if not flags & ARMED_HEATER_CHECK_PASSED:
                raise PlantInterfaceError("ARMED did not report heater-interface readiness flag bit 4")
            self.readiness_flags = flags
            return True

        return False


class PlantInterfaceTest:
    def __init__(self, port, timeout: float, poll_interval: float,
                 output: Callable[[str], None] = print,
                 input_fn: Callable[[str], str] = input,
                 clock: Callable[[], float] = time.monotonic,
                 sleep_fn: Callable[[float], None] = time.sleep):
        self.port = port
        self.timeout = timeout
        self.poll_interval = poll_interval
        self.output = output
        self.input_fn = input_fn
        self.clock = clock
        self.sleep = sleep_fn
        self.parser = protocol.StreamParser()
        self.pending: deque[protocol.Frame] = deque()
        self.tx_sequence = 0

    def _send(self, message_type: int, payload: bytes = b"") -> int:
        sequence = self.tx_sequence
        encoded = protocol.encode_frame(message_type, sequence, payload)
        written = self.port.write(encoded)
        if written is not None and written != len(encoded):
            raise PlantInterfaceError("serial write was incomplete")
        self.port.flush()
        self.tx_sequence = (self.tx_sequence + 1) & 0xFFFF
        return sequence

    def _next_frame(self, deadline: float) -> Optional[protocol.Frame]:
        while self.clock() < deadline:
            if self.pending:
                return self.pending.popleft()
            frames = self.parser.feed(self.port.read(256))
            for frame in frames:
                self.output(protocol.describe(frame))
                self.pending.append(frame)
        return self.pending.popleft() if self.pending else None

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
            if frame.message_type == NACK:
                raise PlantInterfaceError("unexpected NACK while waiting for STATUS")
        raise PlantInterfaceError("no valid STATUS response before timeout")

    def _verify_ready(self, status: PlantStatus, expected_test_id: Optional[int] = None) -> None:
        if status.state != READY:
            raise PlantInterfaceError(f"Plant state changed from READY (3) to {status.state}")
        if status.fault_code != 0:
            raise PlantInterfaceError(f"Plant reports fault_code={status.fault_code}")
        required = STATUS_PLANT_VALID | STATUS_DAC_VALID
        if status.status_flags & required != required:
            raise PlantInterfaceError("Plant PLANT_VALID or I2C/DAC-valid STATUS flag is low")
        if expected_test_id is not None and status.test_id != expected_test_id:
            raise PlantInterfaceError("Plant test_id changed during the interface check")

    def _wait_for_level(self, high: bool, test_id: int) -> bool:
        deadline = self.clock() + self.timeout
        while self.clock() < deadline:
            status = self._request_status(deadline - self.clock())
            self._verify_ready(status, test_id)
            if status.heater_high == high:
                return True
            self.sleep(min(self.poll_interval, max(0.0, deadline - self.clock())))
        return False

    def _arm(self, test_id: int) -> int:
        payload = bytes((PLANT,)) + test_id.to_bytes(4, "little")
        sequence = self._send(ARM_TEST, payload)
        self.output(f"Sent ARM_TEST seq={sequence} test_id={test_id}")
        tracker = ArmResponseTracker(sequence, test_id)
        deadline = self.clock() + self.timeout
        while self.clock() < deadline:
            frame = self._next_frame(deadline)
            if frame is None:
                break
            if tracker.consume(frame):
                if tracker.readiness_flags is None:
                    raise PlantInterfaceError("ARMED readiness flags were not available")
                return tracker.readiness_flags
        if not tracker.acknowledged:
            raise PlantInterfaceError("no valid ARM_TEST ACK received before timeout")
        raise PlantInterfaceError("ACK received, but ARMED did not arrive before timeout")

    def run(self) -> bool:
        try:
            initial = self._request_status(self.timeout)
            self._verify_ready(initial)
            if initial.heater_high:
                raise PlantInterfaceError("PB0 is not initially LOW; restore the pull-down baseline and retry")
            test_id = initial.test_id
            self.output(f"Plant READY; test_id={test_id}; PLANT_VALID=1; I2C/DAC_valid=1; PB0=LOW")

            self.output("Safety check: disconnect BlackPill PB0 from BluePill PB0. Keep the external 6.2 kΩ pull-down connected. Use 3.3 V only, never 5 V.")
            confirmation = self.input_fn("Type DISCONNECTED to confirm before applying the signal: ")
            if confirmation.strip() != "DISCONNECTED":
                raise PlantInterfaceError("operator did not confirm BlackPill PB0 is disconnected")

            self.input_fn("Apply 3.3 V to BluePill PB0 now; press Enter and HOLD the level until detection is confirmed: ")
            if not self._wait_for_level(True, test_id):
                raise PlantInterfaceError("PB0 HIGH was not observed before timeout; remove the 3.3 V signal and restore LOW. ARM_TEST was not sent")
            self.output("PB0 HIGH observed in STATUS.")

            self.input_fn("Remove 3.3 V from PB0 now; press Enter and HOLD LOW until detection is confirmed: ")
            if not self._wait_for_level(False, test_id):
                raise PlantInterfaceError("PB0 LOW was not observed before timeout; ensure the 3.3 V signal is removed. ARM_TEST was not sent")
            self.output("PB0 LOW observed in STATUS; both requested transitions were observed.")

            flags = self._arm(test_id)
            self.output("ARM_TEST accepted: ACK followed by ARMED.")
            self.output(f"ARMED readiness_flags=0x{flags:04X}; heater-interface check bit 4={int(bool(flags & ARMED_HEATER_CHECK_PASSED))}.")
            self.output("Plant interface test passed. START_TEST was not sent.")
            return True
        except PlantInterfaceError as exc:
            self.output(f"ERROR: {exc}")
            return False
        except KeyboardInterrupt:
            self.output("Interrupted; remove any injected 3.3 V signal and restore PB0 LOW. ARM_TEST was not sent unless already reported.")
            return False


def main(argv: Optional[list[str]] = None) -> int:
    args = build_parser().parse_args(argv)
    port = None
    try:
        # Keep modem-control lines inactive while opening; they are not used
        # by the Plant protocol and must not be used to reset the board.
        port = serial.Serial(port=None, baudrate=args.baud, timeout=0.1,
                             write_timeout=args.timeout)
        port.port = args.port
        port.dtr = False
        port.rts = False
        port.open()
        print(f"Opened {args.port} for Plant at {args.baud} baud.")
        return 0 if PlantInterfaceTest(port, args.timeout, args.poll_interval).run() else 1
    except (serial.SerialException, TimeoutError, RuntimeError, ValueError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    finally:
        if port is not None and port.is_open:
            port.close()


if __name__ == "__main__":
    raise SystemExit(main())
