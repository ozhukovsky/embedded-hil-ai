#!/usr/bin/env python3
"""Observe the Plant's physical PB0 input, Euler response, and digital DAC codes."""

from __future__ import annotations

import argparse
import csv
import math
import os
import select
import struct
import sys
import time
from collections import deque
from dataclasses import asdict, dataclass
from pathlib import Path

import hil_smoke_test as protocol
import plant_bringup
import plant_interface_test as interface
import plant_runtime_timeout_test as runtime
import plant_timing_test as timing

serial = protocol.serial
STEP_MS = 5000
MAX_RUNTIME_MS = 120000
HOST_STOP_AFTER_S = 105.0  # Leave 15 s for an orderly stop before local expiry.
TEMP_TOLERANCE_MC = 2
DAC_TOLERANCE_CODES = 1  # Floating-point arithmetic at a half-code boundary.
SCHEDULER_TOLERANCE_MS = 250
TELEMETRY_TIMEOUT_S = 8.0


class ValidationError(RuntimeError):
    """Received evidence contradicts the configured experiment."""


class IncompleteRun(RuntimeError):
    """Missing observations, operator input, or time prevent a complete result."""


def config_payload(test_id: int) -> bytes:
    """Reuse the existing Plant CONFIG_SET serializer and its fixed field offsets."""
    if not 1 <= test_id <= 0xFFFFFFFF:
        raise ValueError("test_id must be a nonzero u32")
    payload = bytearray(plant_bringup.default_config_payload(test_id))
    if len(payload) != 30:
        raise ValidationError("existing CONFIG_SET serializer did not return 30 bytes")
    struct.pack_into("<Ii", payload, 13, 30, 200)  # tau_s, heater_gain_mC_per_s
    struct.pack_into("<I", payload, 25, MAX_RUNTIME_MS)
    return bytes(payload)


@dataclass(frozen=True)
class Telemetry:
    test_id: int
    sample_index: int
    uptime_ms: int
    simulated_temp_mC: int
    ambient_temp_mC: int
    heater_input: int
    dac_code: int
    plant_valid: int


def decode_telemetry(payload: bytes, test_id: int) -> Telemetry:
    # Protocol V1.0 Plant TELEMETRY, identical layout to runtime.describe_frame().
    if len(payload) != 25:
        raise ValidationError("malformed Plant TELEMETRY payload")
    role, *fields = struct.unpack("<BIIIiiBHB", payload)
    sample = Telemetry(*fields)
    if role != interface.PLANT or sample.test_id != test_id:
        raise ValidationError("TELEMETRY role/test_id does not match this Plant run")
    if sample.heater_input not in (0, 1) or sample.plant_valid not in (0, 1):
        raise ValidationError("TELEMETRY contains a non-boolean input/valid field")
    if not -20000 <= sample.simulated_temp_mC <= 60000 or not 0 <= sample.dac_code <= 4095:
        raise ValidationError("TELEMETRY temperature/DAC field is outside its valid range")
    return sample


def temperature_to_dac(temperature_mC: int) -> int:
    """Exact rational form of the documented 100..4000 linear mapping."""
    code = (100 * 80000 + (temperature_mC + 20000) * 3900 + 40000) // 80000
    return max(100, min(4000, code))


class ResponseValidator:
    """Reference integration uses the previous TELEMETRY heater_input, never an offset."""

    def __init__(self, vcc: float):
        self.vcc = vcc
        self.reference_mC = 20000.0
        self.previous: Telemetry | None = None
        self.phase = "baseline"
        self.counts = {"baseline": 0, "heating": 0, "cooling": 0}
        self.records: list[dict] = []

    def add(self, sample: Telemetry) -> dict | None:
        previous = self.previous
        if sample == previous:
            return None  # Identical retransmitted observation adds no interval.
        row = dict(asdict(sample), phase=self.phase, completed_input="", reference_temp_mC="",
                   error_mC="", expected_dac_code="", expected_vout_V=self.vcc * sample.dac_code / 4096,
                   validation_error="")
        self.records.append(row)  # Preserve the failing sample as well as the valid sequence.
        try:
            if previous is None:
                if sample.sample_index != 0:
                    raise IncompleteRun("first sample_index is not 0; the initial interval was missed")
                completed_input = 0  # START_TEST is sent only after a fresh LOW STATUS.
                previous_temp = 20000
            else:
                if sample.sample_index != (previous.sample_index + 1) & 0xFFFFFFFF:
                    raise IncompleteRun("TELEMETRY sample gap/reordering; lost heater latches cannot be inferred")
                dt = (sample.uptime_ms - previous.uptime_ms) & 0xFFFFFFFF
                if abs(dt - STEP_MS) > SCHEDULER_TOLERANCE_MS:
                    raise ValidationError(f"TELEMETRY interval {dt} ms differs from 5000 +/- 250 ms")
                completed_input = previous.heater_input
                previous_temp = previous.simulated_temp_mC
            self.reference_mC += 5.0 * ((20000.0 - self.reference_mC) / 30.0 + 200.0 * completed_input)
            expected_code = temperature_to_dac(sample.simulated_temp_mC)
            error = sample.simulated_temp_mC - self.reference_mC
            row.update(completed_input=completed_input, reference_temp_mC=self.reference_mC,
                       error_mC=error, expected_dac_code=expected_code)
            if abs(error) > TEMP_TOLERANCE_MC:
                raise ValidationError(f"Euler error {error:.3f} mC exceeds +/-{TEMP_TOLERANCE_MC} mC")
            if sample.ambient_temp_mC != 20000 or sample.plant_valid != 1:
                raise ValidationError("TELEMETRY ambient must be 20000 mC and PLANT_VALID must be 1")
            if abs(sample.dac_code - expected_code) > DAC_TOLERANCE_CODES:
                raise ValidationError("DAC code disagrees with the documented temperature mapping")
            if self.phase == "baseline":
                if completed_input or sample.heater_input or abs(sample.simulated_temp_mC - 20000) > 2:
                    raise ValidationError("baseline requires stable 20 C and LOW input")
                self.counts["baseline"] += 1
            elif self.phase == "heating" and completed_input == sample.heater_input == 1:
                if sample.simulated_temp_mC <= previous_temp:
                    raise ValidationError("temperature did not increase over a fully heater-on interval")
                self.counts["heating"] += 1
            elif self.phase == "cooling" and completed_input == sample.heater_input == 0:
                if not 20000 < sample.simulated_temp_mC < previous_temp:
                    raise ValidationError("temperature did not cool toward ambient over a heater-off interval")
                self.counts["cooling"] += 1
            self.previous = sample
            return row
        except (ValidationError, IncompleteRun) as exc:
            row["validation_error"] = str(exc)
            raise


class ConsolePrompts:
    """Poll console input on the same thread that services UART; no stdin worker."""

    def __init__(self, read_key=None, stream=None, output=None):
        self.stream = sys.stdin if stream is None else stream
        self.output = sys.stdout if output is None else output
        self.read_key = read_key
        if read_key is None and os.name == "nt":
            import msvcrt
            self.read_key = lambda: msvcrt.getwch() if msvcrt.kbhit() else None
        self.injected_reader = read_key is not None
        self.active = False
        self.characters = []
        self.skip_key = False

    def _write(self, text: str) -> None:
        self.output.write(text)
        self.output.flush()

    def begin(self, prompt: str) -> None:
        if self.active:
            raise RuntimeError("a console prompt is already active")
        if not self.injected_reader and not self.stream.isatty():
            raise IncompleteRun("interactive console required; run this utility in a terminal")
        self.active = True
        self.characters = []
        self.skip_key = False
        self._write(prompt)

    def poll(self) -> str | None:
        if not self.active:
            return None
        if self.read_key is not None:
            # Bound keyboard work so UART still gets serviced during a large paste.
            for _ in range(64):
                key = self.read_key()
                if key is None:
                    return None
                if self.skip_key:
                    self.skip_key = False
                elif key in ("\x00", "\xe0"):
                    self.skip_key = True  # Windows function/arrow key prefix.
                elif key == "\x03":
                    raise KeyboardInterrupt
                elif key in ("\x04", "\x1a"):
                    raise IncompleteRun("console input was interrupted")
                elif key in ("\r", "\n"):
                    self.active = False
                    self._write("\n")
                    return "".join(self.characters)
                elif key in ("\b", "\x7f"):
                    if self.characters:
                        self.characters.pop()
                        self._write("\b \b")
                elif key.isprintable() and len(self.characters) < 256:
                    self.characters.append(key)
                    self._write(key)
            return None
        # POSIX terminals supply a complete, locally echoed line in canonical mode.
        if not select.select([self.stream], [], [], 0)[0]:
            return None
        result = self.stream.readline()
        if result == "":
            raise IncompleteRun("console input was interrupted")
        self.active = False
        return result.rstrip("\r\n")

    def cancel(self) -> None:
        if self.active:
            self.active = False
            self._write("\n")


class HeaterResponseTest:
    def __init__(self, port, test_id: int, vcc: float, response_timeout: float = 5.0,
                 prompt_timeout: float = 20.0, output=print, clock=time.monotonic,
                 prompts=None):
        if not 0 < response_timeout <= 5 or not 0 < prompt_timeout <= 20:
            raise ValueError("response timeout must be <=5 s; active prompt/level timeout must be <=20 s")
        if not math.isfinite(vcc) or vcc <= 0:
            raise ValueError("VCC must be a finite positive measured voltage")
        if not 1 <= test_id <= 0xFFFFFFFF:
            raise ValueError("test_id must be a nonzero u32")
        self.port, self.test_id, self.output, self.clock = port, test_id, output, clock
        self.response_timeout, self.prompt_timeout = response_timeout, prompt_timeout
        self.prompts = prompts if prompts is not None else ConsolePrompts()
        self.validator = ResponseValidator(vcc)
        self.parser = protocol.StreamParser()
        self.controls = deque()
        self.tx_sequence = 0
        self.status_version = 0
        self.status = None
        self.status_uptime = None
        self.last_state = runtime.WAIT_CONFIG
        self.allowed_states = {runtime.WAIT_CONFIG}
        self.configured = self.start_sent = self.stop_sent = False
        self.start_rejected = False
        self.stopping = self.recovering = False
        self.poll_enabled = True
        self.prompt_active = False
        self.deferred_telemetry = []
        self.hold_level = None
        self.start_tx = None
        self.stop_deadline = None
        self.status_pending_at = None
        self.next_status_at = self.last_rx = self.last_telemetry_at = clock()

    def _message(self, text: str, *, telemetry: bool = False) -> None:
        if self.prompt_active:
            if telemetry:
                self.deferred_telemetry.append(text)
            return
        self.output(text)

    def _send(self, kind: int, payload: bytes = b"") -> int:
        seq = self.tx_sequence
        frame = protocol.encode_frame(kind, seq, payload)
        self.tx_sequence = (seq + 1) & 0xFFFF
        written = self.port.write(frame)
        if written is not None and written != len(frame):
            raise IncompleteRun("incomplete serial write; command outcome is unknown")
        self._message(f"TX type=0x{kind:02X} seq={seq}")
        return seq

    def _issue_status(self) -> None:
        if self.status_pending_at is None:
            self._send(protocol.STATUS_REQUEST)
            self.status_pending_at = self.clock()
            self.next_status_at = self.clock() + 0.5

    def _check_state(self, state: int, test_id: int) -> None:
        if state == runtime.FAULT:
            self.last_state = state
        if self.recovering:
            return
        if state not in self.allowed_states:
            raise ValidationError(f"unexpected board state {state}; expected {sorted(self.allowed_states)}")
        if self.configured and state != runtime.WAIT_CONFIG and test_id != self.test_id:
            raise ValidationError("board test_id changed")

    def _handle(self, frame: protocol.Frame) -> None:
        kind, payload = frame.message_type, frame.payload
        if kind == timing.TELEMETRY:
            sample = decode_telemetry(payload, self.test_id)
            self.last_telemetry_at = self.clock()
            self._message("TELEMETRY " + " ".join(f"{k}={v}" for k, v in asdict(sample).items()) +
                          f" expected_VOUT={self.validator.vcc * sample.dac_code / 4096:.4f} V", telemetry=True)
            if not self.recovering:
                if not self.start_sent:
                    raise ValidationError("TELEMETRY arrived before START_TEST")
                row = self.validator.add(sample)
                if row:
                    self._message(f"  reference={row['reference_temp_mC']:.3f} mC error={row['error_mC']:+.3f} mC "
                                  f"completed_u={row['completed_input']} next_u={sample.heater_input} "
                                  f"phase={self.validator.phase}", telemetry=True)
        elif kind == protocol.STATUS:
            status = interface.decode_plant_status(payload)
            self.status, self.last_state = status, status.state
            self.status_uptime = int.from_bytes(payload[8:12], "little")
            self.status_version += 1
            self.status_pending_at = None
            self._message(runtime.describe_frame(frame, self.test_id))
            self._check_state(status.state, status.test_id)
            if not self.recovering:
                if status.fault_code:
                    raise ValidationError(f"STATUS reports fault {status.fault_code}")
                if status.status_flags & 0x30:
                    raise ValidationError("STATUS reports timing or TX queue failure")
                if status.state in (interface.READY, timing.ARMED, runtime.RUNNING):
                    if status.status_flags & 0x09 != 0x09:
                        raise ValidationError("STATUS lost PLANT_VALID or DAC validity")
                if self.start_sent and not self.stopping and self.hold_level is not None:
                    if status.heater_high != bool(self.hold_level):
                        raise ValidationError("PB0 changed while the operator was required to hold its level")
        elif kind == protocol.HEARTBEAT:
            if len(payload) != 10 or payload[0] != interface.PLANT:
                raise ValidationError("malformed HEARTBEAT or wrong role")
            _, state, test_id, uptime = struct.unpack("<BBII", payload)
            if state > 7:
                raise ValidationError("HEARTBEAT contains an invalid state")
            # Control replies can overtake a queued heartbeat from before the transition.
            older = self.status_uptime is not None and (
                (uptime - self.status_uptime) & 0xFFFFFFFF) >= 0x80000000
            same_tick = uptime == self.status_uptime
            if state == runtime.FAULT or not (older or same_tick):
                self._check_state(state, test_id)
            self._message(runtime.describe_frame(frame, self.test_id))
        elif kind == runtime.FAULT_REPORT:
            if len(payload) != 13 or payload[0] != interface.PLANT:
                raise ValidationError("malformed FAULT_REPORT")
            if self.configured and int.from_bytes(payload[1:5], "little") != self.test_id:
                raise ValidationError("FAULT_REPORT has the wrong test_id")
            code, detail = struct.unpack_from("<HH", payload, 5)
            if not 1 <= code <= 7:
                raise ValidationError("FAULT_REPORT contains an invalid fault code")
            self.last_state = runtime.FAULT
            self._message(f"FAULT_REPORT code={code} detail={detail}")
            if not self.recovering:
                raise ValidationError(f"unexpected FAULT_REPORT code={code} detail={detail}")
        elif kind in (interface.ACK, interface.NACK, interface.ARMED, runtime.TEST_COMPLETE):
            if kind == runtime.TEST_COMPLETE and not self.stopping and not self.recovering:
                raise ValidationError("unexpected TEST_COMPLETE before host STOP_TEST")
            self.controls.append(frame)
            self._message(protocol.describe(frame))
        elif kind == protocol.BOOT_REPORT:
            self._message(protocol.describe(frame))
            if self.configured and not self.recovering:
                raise ValidationError("BOOT_REPORT indicates an unexpected board reset")
        else:
            raise ValidationError(f"unexpected message type 0x{kind:02X}")

    def _pump(self) -> None:
        now = self.clock()
        if self.stopping and self.stop_deadline is not None and now >= self.stop_deadline:
            raise IncompleteRun("overall 10 s STOP_TEST confirmation deadline exhausted")
        if self.start_tx is not None and not self.stopping:
            if now - self.start_tx >= HOST_STOP_AFTER_S:
                raise IncompleteRun("105 s host runtime limit reached; stopping before the 120 s board deadline")
            if now - self.last_telemetry_at > TELEMETRY_TIMEOUT_S:
                raise IncompleteRun("scheduled TELEMETRY timed out")
        if not self.recovering and self.status_pending_at is not None:
            if now - self.status_pending_at > self.response_timeout:
                raise IncompleteRun("serial STATUS response timeout")
        if self.poll_enabled and now >= self.next_status_at:
            self._issue_status()
        data = self.port.read(256)
        for frame in self.parser.feed(data):
            self.last_rx = self.clock()
            self._handle(frame)
        if self.clock() - self.last_rx > self.response_timeout + 1:
            raise IncompleteRun("no valid serial frames before timeout")

    def _wait(self, predicate, timeout: float) -> None:
        deadline = self.clock() + timeout
        while not predicate():
            if self.clock() >= deadline:
                raise IncompleteRun("protocol/observation timeout")
            self._pump()

    def _fresh_status(self):
        version = self.status_version
        self._issue_status()
        self._wait(lambda: self.status_version > version, self.response_timeout)
        return self.status

    def _prompt(self, text: str, token: str | None = None) -> None:
        self.output(text)
        deadline = None if self.start_tx is None else self.clock() + self.prompt_timeout
        if deadline is None:
            self.output("Manual setup: no operator timeout before START_TEST.")
        else:
            remaining = max(0.0, HOST_STOP_AFTER_S - (self.clock() - self.start_tx))
            self.output(f"Confirm within {self.prompt_timeout:g} s; {remaining:.1f} s of active host budget remains.")
        was_polling = self.poll_enabled
        self.prompt_active = True
        self.poll_enabled = False
        try:
            self.prompts.begin("Type " + token + ": " if token else "Press Enter after changing the level; keep holding it: ")
            while True:
                answer = self.prompts.poll()
                if answer is not None:
                    if token and answer.strip() != token:
                        raise IncompleteRun(f"explicit {token} confirmation was not provided")
                    return
                if deadline is not None and self.clock() >= deadline:
                    raise IncompleteRun("operator prompt timed out; remove injected 3.3 V and restore PB0 LOW")
                self._pump()
        finally:
            cancel = getattr(self.prompts, "cancel", None)
            if cancel is not None:
                cancel()
            self.prompt_active = False
            self.poll_enabled = was_polling
            self.next_status_at = self.clock()
            if self.deferred_telemetry:
                self.output("Telemetry received and validated while waiting for input:")
                for line in self.deferred_telemetry:
                    self.output(line)
                self.deferred_telemetry.clear()

    def _level(self, high: bool) -> None:
        self._fresh_status()
        self._wait(lambda: self.status.heater_high == high, self.prompt_timeout)
        self.output(f"STATUS confirms PB0 {'HIGH' if high else 'LOW'}.")

    def _ack(self, sequence: int, kind: int) -> None:
        def consume():
            while self.controls:
                frame = self.controls.popleft()
                if (kind == timing.START_TEST and frame.message_type == interface.NACK and
                        len(frame.payload) == 6 and
                        struct.unpack("<HBBH", frame.payload)[:3] == (sequence, kind, interface.PLANT) and
                        1 <= int.from_bytes(frame.payload[4:6], "little") <= 9):
                    self.start_rejected = True
                if runtime.matching_ack(frame, sequence, kind):
                    return True
            return False
        self._wait(consume, self.response_timeout)

    def _arm(self) -> None:
        self.allowed_states = {interface.READY, timing.ARMED}
        self.poll_enabled = False
        seq = self._send(interface.ARM_TEST, bytes((interface.PLANT,)) + self.test_id.to_bytes(4, "little"))
        tracker = interface.ArmResponseTracker(seq, self.test_id)
        def consume():
            while self.controls:
                if tracker.consume(self.controls.popleft()):
                    return True
            return False
        self._wait(consume, self.response_timeout)
        self.output(f"ARMED readiness_flags=0x{tracker.readiness_flags:04X}; heater-interface bit 4=1.")
        self.poll_enabled = True
        self._fresh_status()
        self.allowed_states = {timing.ARMED}
        if self.status.state != timing.ARMED or self.status.heater_high:
            raise ValidationError("START_TEST requires ARMED and PB0 LOW")

    def _start(self) -> None:
        if self.start_sent:
            raise ValidationError("refusing a second START_TEST")
        self.allowed_states = {timing.ARMED, runtime.RUNNING}
        self.poll_enabled = False
        self.start_sent = True
        self.start_tx = self.last_telemetry_at = self.clock()
        seq = self._send(timing.START_TEST, bytes((interface.PLANT,)) + self.test_id.to_bytes(4, "little"))
        self._ack(seq, timing.START_TEST)
        self.poll_enabled = True
        if self._fresh_status().state != runtime.RUNNING:
            raise ValidationError("START_TEST ACK was not followed by RUNNING STATUS")
        self.allowed_states = {runtime.RUNNING}
        self.hold_level = 0
        self.output("START_TEST accepted once; hold PB0 LOW for the baseline.")

    def _stop(self, reason: int) -> None:
        if self.stop_sent:
            raise ValidationError("refusing a second STOP_TEST")
        self.stopping = True
        self.stop_deadline = self.clock() + 10.0
        self.allowed_states = {runtime.RUNNING, timing.COMPLETE}
        self.poll_enabled = False
        self.stop_sent = True  # Set before write: partial writes must never trigger a retry.
        payload = bytes((interface.PLANT,)) + self.test_id.to_bytes(4, "little") + bytes((reason,))
        seq = self._send(timing.STOP_TEST, payload)
        self._ack(seq, timing.STOP_TEST)
        complete = None
        def consume():
            nonlocal complete
            while self.controls:
                frame = self.controls.popleft()
                if frame.message_type == runtime.TEST_COMPLETE:
                    complete = frame.payload
                    return True
            return False
        self._wait(consume, self.response_timeout)
        if len(complete) != 10:
            raise ValidationError("malformed TEST_COMPLETE")
        role, tid, outcome, elapsed = struct.unpack("<BIBI", complete)
        if (role, tid, outcome) != (interface.PLANT, self.test_id, int(reason != 0)):
            raise ValidationError("TEST_COMPLETE role/test_id/outcome does not match STOP_TEST")
        if reason == 0:
            last = self.validator.previous
            if last is None or not (last.sample_index + 1) * STEP_MS <= elapsed < MAX_RUNTIME_MS:
                raise ValidationError("normal TEST_COMPLETE elapsed_ms is inconsistent with the observed run")
        self.poll_enabled = True
        self.status_pending_at = None
        self._fresh_status()
        self._wait(lambda: self.status.state == timing.COMPLETE, self.response_timeout)
        status = self.status
        if status.state != timing.COMPLETE or status.fault_code or status.test_id != self.test_id or status.status_flags & 1:
            raise ValidationError("STOP_TEST did not produce COMPLETE with PLANT_VALID LOW")
        self.output(f"STOP_TEST ACK + TEST_COMPLETE outcome={outcome} elapsed_ms={elapsed}; final state=COMPLETE.")

    def _intervals(self, phase: str, count: int) -> None:
        self._wait(lambda: self.validator.counts[phase] >= count, HOST_STOP_AFTER_S)
        self.output(f"Confirmed {self.validator.counts[phase]} complete {phase} interval(s).")

    def run(self) -> bool:
        try:
            runtime.verify_wait_config(self._fresh_status())
            self._prompt("No heater, switching stage or actuator connected. Keep BlackPill PB0 disconnected, "
                         "the external 6.2 kOhm pull-down and common GND in place. Use 3.3 V ONLY through "
                         "about 1 kOhm series resistance; never 5 V. Preserve DAC wiring. Confirm these conditions.", "SAFE")
            self.allowed_states = {runtime.WAIT_CONFIG, interface.READY}
            self.poll_enabled = False
            self.configured = True
            seq = self._send(plant_bringup.CONFIG_SET, config_payload(self.test_id))
            self._ack(seq, plant_bringup.CONFIG_SET)
            self.poll_enabled = True
            runtime.verify_ready(self._fresh_status(), self.test_id)
            self.allowed_states = {interface.READY}
            self._prompt("Confirm BlackPill PB0 is physically disconnected from BluePill PB0.", "DISCONNECTED")
            self._level(False)
            self._prompt("Startup diagnostic: apply 3.3 V through about 1 kOhm to PB0 and HOLD HIGH.")
            self._level(True)
            self._prompt("Startup diagnostic: remove 3.3 V and HOLD PB0 LOW via the external pull-down.")
            self._level(False)
            self._arm()
            self._start()
            self._intervals("baseline", 1)

            self.validator.phase = "heating"
            self.hold_level = None
            self._prompt("HEATING: apply 3.3 V through about 1 kOhm to PB0 and HOLD HIGH. "
                         "Observe VOUT with the multimeter; voltage predictions are not measurements.")
            self._level(True)
            self.hold_level = 1
            self._intervals("heating", 4)

            self.validator.phase = "cooling"
            self.hold_level = None
            self._prompt("COOLING: remove 3.3 V and HOLD PB0 LOW. A final heating result can still arrive "
                         "because the completed interval used the previous latch.")
            self._level(False)
            self.hold_level = 0
            self._intervals("cooling", 4)
            self._stop(0)
            self.output("PASS: digital telemetry follows the heater response, Euler model and DAC mapping. "
                        "Dynamic analogue VOUT verification remains an independent multimeter observation.")
            return True
        except (ValidationError, IncompleteRun, interface.PlantInterfaceError,
                runtime.PlantRuntimeTestError, OSError, EOFError, KeyboardInterrupt) as exc:
            category = "INCOMPLETE" if isinstance(exc, (IncompleteRun, OSError, EOFError, KeyboardInterrupt)) else "FAIL"
            self.output(f"{category}: {exc}. Remove injected 3.3 V and restore PB0 LOW.")
            if (self.start_sent and not self.start_rejected and not self.stop_sent and
                    self.last_state not in (runtime.FAULT, timing.COMPLETE)):
                self.output("Attempting one STOP_TEST with HOST_SAFETY_ABORT (2); no second run will start.")
                self.recovering = True
                try:
                    self._stop(2)
                except (RuntimeError, OSError, KeyboardInterrupt) as stop_error:
                    self.output(f"Stop could not be confirmed: {stop_error}. Check Plant state before another run.")
            return False

    def write_csv(self, path: Path) -> None:
        if not self.validator.records:
            self.output("No telemetry records to write.")
            return
        with path.open("w", encoding="utf-8", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=list(self.validator.records[0]))
            writer.writeheader()
            writer.writerows(self.validator.records)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="FT232RL port, e.g. COM3")
    parser.add_argument("--baud", type=timing.positive_u32, default=115200)
    parser.add_argument("--test-id", type=timing.positive_u32, default=1)
    parser.add_argument("--vcc", type=timing.positive_float, required=True,
                        help="currently measured DAC VCC in volts; no assumed supply voltage")
    parser.add_argument("--response-timeout", type=timing.positive_float, default=5.0,
                        help="protocol response timeout, at most 5 s")
    parser.add_argument("--prompt-timeout", type=timing.positive_float, default=20.0,
                        help="operator timeout after START_TEST, at most 20 s; setup prompts have no timeout")
    parser.add_argument("--csv", type=Path, help="optional telemetry/reference CSV, including partial runs")
    return parser


def main(argv=None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    if args.response_timeout > 5 or args.prompt_timeout > 20:
        parser.error("response timeout must be <=5 s and active prompt/level timeout <=20 s")
    port = runner = None
    exit_code = 1
    try:
        port = serial.Serial(port=None, baudrate=args.baud, timeout=0.1,
                             write_timeout=args.response_timeout, bytesize=8, parity="N", stopbits=1,
                             xonxoff=False, rtscts=False, dsrdtr=False)
        port.port, port.dtr, port.rts = args.port, False, False
        port.open()
        print(f"Opened {args.port}; measured DAC VCC={args.vcc:g} V. Plant must already be freshly reset.")
        runner = HeaterResponseTest(port, args.test_id, args.vcc, args.response_timeout, args.prompt_timeout)
        exit_code = 0 if runner.run() else 1
    except (OSError, RuntimeError, ValueError) as exc:
        print(f"INCOMPLETE: {exc}", file=sys.stderr)
    finally:
        if port is not None and port.is_open:
            port.close()
        if runner is not None and args.csv:
            try:
                runner.write_csv(args.csv)
            except OSError as exc:
                print(f"Could not save telemetry CSV: {exc}", file=sys.stderr)
                exit_code = 1
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
