#!/usr/bin/env python3
"""Configure the Plant simulator and verify its READY response (never starts a test)."""

from __future__ import annotations

import argparse
import struct
import sys
import time

try:
    import serial
except ImportError as exc:  # pragma: no cover
    raise SystemExit("pyserial is required: python -m pip install pyserial") from exc

import hil_smoke_test as protocol

CONFIG_SET = 0x10
ACK = 0x20
NACK = 0x21
READY = 3
PLANT = 2


def default_config_payload(test_id: int = 1) -> bytes:
    """20 C initial/ambient, tau=300 s, gain=20 mC/s, 5 s steps."""
    return struct.pack("<BIiiIiHHIB", PLANT, test_id, 20000, 20000, 300, 20,
                       5000, 5000, 600000, 0)


def _valid_plant_status(payload: bytes, ready: bool = False) -> bool:
    if not protocol.valid_status_payload(payload, PLANT):
        return False
    flags = int.from_bytes(payload[12:14], "little")
    if int.from_bytes(payload[2:4], "little") != 0:
        return False
    if ready:
        return payload[1] == READY and (flags & 0x0009) == 0x0009
    return True


def run_bringup(port, timeout: float = 5.0, listen_seconds: float = 10.0,
                output=print, clock=time.monotonic) -> bool:
    parser = protocol.StreamParser()
    boot_seen = False
    tx_sequence = 0

    def request_status() -> int:
        nonlocal tx_sequence
        seq = tx_sequence
        port.write(protocol.encode_frame(protocol.STATUS_REQUEST, seq))
        port.flush()
        tx_sequence = (tx_sequence + 1) & 0xFFFF
        output(f"Sent STATUS_REQUEST seq={seq}")
        return seq

    request_status()
    deadline = clock() + timeout
    initial_status = False
    while clock() < deadline and not initial_status:
        for frame in parser.feed(port.read(256)):
            output(protocol.describe(frame))
            if frame.message_type == protocol.BOOT_REPORT:
                boot_seen = True
            elif frame.message_type == protocol.HEARTBEAT and len(frame.payload) == 10:
                pass
            elif frame.message_type == protocol.STATUS and _valid_plant_status(frame.payload):
                initial_status = True
    if not initial_status:
        output(f"BOOT_REPORT observed: {'yes' if boot_seen else 'no'}")
        output("ERROR: Plant did not answer STATUS_REQUEST")
        return False

    config_seq = tx_sequence
    port.write(protocol.encode_frame(CONFIG_SET, config_seq, default_config_payload()))
    port.flush()
    tx_sequence = (tx_sequence + 1) & 0xFFFF
    output(f"Sent CONFIG_SET seq={config_seq} test_id=1")
    ack_deadline = clock() + timeout
    accepted = False
    while clock() < ack_deadline and not accepted:
        for frame in parser.feed(port.read(256)):
            output(protocol.describe(frame))
            if frame.message_type == protocol.BOOT_REPORT:
                boot_seen = True
            elif frame.message_type in (ACK, NACK) and len(frame.payload) == 6:
                seq = int.from_bytes(frame.payload[:2], "little")
                msg_type, role = frame.payload[2], frame.payload[3]
                error = int.from_bytes(frame.payload[4:6], "little")
                if seq == config_seq and msg_type == CONFIG_SET and role == PLANT:
                    if frame.message_type == ACK and error == 0:
                        accepted = True
                    else:
                        output(f"ERROR: CONFIG_SET rejected with NACK error={error}")
                        output(f"BOOT_REPORT observed: {'yes' if boot_seen else 'no'}")
                        return False
    if not accepted:
        output(f"BOOT_REPORT observed: {'yes' if boot_seen else 'no'}")
        output("ERROR: no valid CONFIG_SET ACK received before timeout")
        return False

    request_status()
    ready_deadline = clock() + timeout
    while clock() < ready_deadline:
        for frame in parser.feed(port.read(256)):
            output(protocol.describe(frame))
            if frame.message_type == protocol.BOOT_REPORT:
                boot_seen = True
            elif frame.message_type == protocol.STATUS and _valid_plant_status(frame.payload, ready=True):
                flags = int.from_bytes(frame.payload[12:14], "little")
                output("Plant READY; PLANT_VALID and I2C/DAC status flags are set.")
                output(f"PLANT_VALID={int(bool(flags & 0x0001))} DAC_valid={int(bool(flags & 0x0008))}")
                output("Expected initial DAC code for 20 C: 2050 (digital readback is firmware-verified).")
                output("No ARM_TEST or START_TEST was sent.")
                listen_until = clock() + listen_seconds
                while clock() < listen_until:
                    for later in parser.feed(port.read(256)):
                        output(protocol.describe(later))
                        if later.message_type == protocol.BOOT_REPORT:
                            boot_seen = True
                output(f"BOOT_REPORT observed: {'yes' if boot_seen else 'no'}")
                return True
    output(f"BOOT_REPORT observed: {'yes' if boot_seen else 'no'}")
    output("ERROR: CONFIG_SET was ACKed but Plant did not report valid READY status")
    return False


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="Plant FT232RL COM port, e.g. COM3")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--listen-seconds", type=float, default=10.0,
                        help="continue displaying received frames after READY (default: 10)")
    args = parser.parse_args()
    try:
        with serial.Serial(args.port, baudrate=args.baud, timeout=0.1) as port:
            print(f"Opened {args.port} for Plant.")
            return 0 if run_bringup(port, args.timeout, args.listen_seconds) else 1
    except (serial.SerialException, TimeoutError, RuntimeError, ValueError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
