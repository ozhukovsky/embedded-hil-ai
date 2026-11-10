#!/usr/bin/env python3
"""Minimal Protocol V1.0 serial bring-up check for either HIL board."""

from __future__ import annotations

import argparse
import sys
import time
from dataclasses import dataclass

try:
    import serial
except ImportError as exc:  # pragma: no cover - depends on host environment
    raise SystemExit("pyserial is required: python -m pip install pyserial") from exc

SOF = b"\xA5\x5A"
VERSION = 1
BOOT_REPORT = 0x01
STATUS = 0x02
HEARTBEAT = 0x03
STATUS_REQUEST = 0x04
MAX_PAYLOAD = 64
ROLE = {"dut": 1, "plant": 2}


def crc16_ccitt_false(data: bytes) -> int:
    crc = 0xFFFF
    for value in data:
        crc ^= value << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


@dataclass(frozen=True)
class Frame:
    message_type: int
    sequence: int
    payload: bytes


class StreamParser:
    def __init__(self) -> None:
        self.buffer = bytearray()

    def feed(self, data: bytes) -> list[Frame]:
        self.buffer.extend(data)
        frames: list[Frame] = []
        while True:
            start = self.buffer.find(SOF)
            if start < 0:
                keep = 1 if self.buffer.endswith(SOF[:1]) else 0
                if keep:
                    self.buffer[:] = self.buffer[-1:]
                else:
                    self.buffer.clear()
                break
            if start:
                del self.buffer[:start]
            if len(self.buffer) < 8:
                break
            if self.buffer[2] != VERSION:
                del self.buffer[0]
                continue
            payload_len = int.from_bytes(self.buffer[6:8], "little")
            if payload_len > MAX_PAYLOAD:
                del self.buffer[0]
                continue
            frame_len = 10 + payload_len
            if len(self.buffer) < frame_len:
                break
            expected = int.from_bytes(self.buffer[8 + payload_len : frame_len], "little")
            actual = crc16_ccitt_false(self.buffer[2 : 8 + payload_len])
            if expected != actual:
                del self.buffer[0]
                continue
            frames.append(
                Frame(
                    self.buffer[3],
                    int.from_bytes(self.buffer[4:6], "little"),
                    bytes(self.buffer[8 : 8 + payload_len]),
                )
            )
            del self.buffer[:frame_len]
        return frames


def encode_frame(message_type: int, sequence: int, payload: bytes = b"") -> bytes:
    if len(payload) > MAX_PAYLOAD:
        raise ValueError("payload exceeds Protocol V1.0 maximum")
    body = bytes((VERSION, message_type)) + sequence.to_bytes(2, "little")
    body += len(payload).to_bytes(2, "little") + payload
    return SOF + body + crc16_ccitt_false(body).to_bytes(2, "little")


def describe(frame: Frame) -> str:
    if frame.message_type == BOOT_REPORT and len(frame.payload) == 12:
        return f"BOOT_REPORT role={frame.payload[0]} seq={frame.sequence}"
    if frame.message_type == STATUS and len(frame.payload) == 14:
        role, state = frame.payload[:2]
        uptime = int.from_bytes(frame.payload[8:12], "little")
        return f"STATUS role={role} state={state} uptime_ms={uptime} seq={frame.sequence}"
    if frame.message_type == HEARTBEAT and len(frame.payload) == 10:
        role, state = frame.payload[:2]
        uptime = int.from_bytes(frame.payload[6:10], "little")
        return f"HEARTBEAT role={role} state={state} uptime_ms={uptime} seq={frame.sequence}"
    return f"message=0x{frame.message_type:02X} seq={frame.sequence} len={len(frame.payload)}"


def valid_status_payload(payload: bytes, expected_role: int) -> bool:
    if len(payload) != 14 or payload[0] != expected_role:
        return False
    state = payload[1]
    fault_code = int.from_bytes(payload[2:4], "little")
    status_flags = int.from_bytes(payload[12:14], "little")
    allowed_flags = 0x0037 if expected_role == ROLE["dut"] else 0x003B
    return state <= 7 and fault_code <= 7 and (status_flags & ~allowed_flags) == 0


def run_smoke_test(
    port: serial.Serial,
    role: str,
    status_timeout: float = 20.0,
    listen_seconds: float = 10.0,
    output=print,
    clock=time.monotonic,
) -> bool:
    expected_role = ROLE[role]
    stream = StreamParser()
    boot_seen = False

    port.write(encode_frame(STATUS_REQUEST, 0))
    port.flush()
    output("Sent STATUS_REQUEST seq=0")

    deadline = clock() + status_timeout
    status_seen = False
    while not status_seen and clock() < deadline:
        for frame in stream.feed(port.read(256)):
            output(describe(frame))
            if frame.message_type == BOOT_REPORT:
                boot_seen = True
            elif (frame.message_type == STATUS and not status_seen and
                  valid_status_payload(frame.payload, expected_role)):
                status_seen = True

    if not status_seen:
        output(f"BOOT_REPORT observed: {'yes' if boot_seen else 'no'}")
        output("ERROR: no valid STATUS response received before timeout")
        return False

    listen_until = clock() + listen_seconds
    while clock() < listen_until:
        for frame in stream.feed(port.read(256)):
            output(describe(frame))
            if frame.message_type == BOOT_REPORT:
                boot_seen = True
    # Include a BOOT_REPORT that arrived while listening in the final result.
    output(f"BOOT_REPORT observed: {'yes' if boot_seen else 'no'}")
    output("Protocol smoke test passed (host-side framing and role checks).")
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--role", choices=ROLE, required=True, help="DUT (USB CDC) or Plant (USART1)")
    parser.add_argument("--port", required=True, help="COM port, for example COM6")
    parser.add_argument("--baud", type=int, help="UART baud (Plant defaults to 115200; ignored by USB CDC)")
    parser.add_argument("--status-timeout", "--boot-timeout", dest="status_timeout",
                        type=float, default=20.0,
                        help="seconds to wait for a valid STATUS response (default: 20)")
    parser.add_argument("--listen-seconds", type=float, default=10.0)
    args = parser.parse_args()
    baud = args.baud or 115200

    try:
        with serial.Serial(args.port, baudrate=baud, timeout=0.2) as port:
            print(f"Opened {args.port} for {args.role}.")
            return 0 if run_smoke_test(port, args.role, args.status_timeout,
                                       args.listen_seconds) else 1
    except (serial.SerialException, TimeoutError, RuntimeError, ValueError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
