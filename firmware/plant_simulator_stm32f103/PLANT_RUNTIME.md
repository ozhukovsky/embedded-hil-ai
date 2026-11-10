# BluePill Plant Runtime

The Plant foreground application uses the shared `hil_protocol_app` for
BOOT_REPORT, STATUS_REQUEST/STATUS, HEARTBEAT, RX sequence monitoring, frame
dispatch, and per-link TX sequence allocation. `plant_runtime.c` handles only
Plant commands and the model lifecycle. Command responses use the shared
transport queues and reliability cache. No I2C or model work runs in an ISR.

For project-wide implementation and verification evidence, see the [milestone
status](../../docs/MILESTONE_STATUS.md). The [shared protocol module guide](../common/hil_protocol/README.md)
describes the portable code used by this runtime.

## Model and validation

The portable model uses `double` internally to make the Euler calculation and
native reference tests straightforward. The STM32F103 has no hardware FPU, so
these few scalar operations use software floating point; their target timing
must be measured on the board. The equation is
`Tnext = T + 5 * ((Tamb - T) / tau + Kh * heater_previous)`.

The accepted temperature/ambient envelope is -20,000 to +60,000 mC. The
derived constraints are `tau_s >= 5`, nonnegative heater gain, and
`Tamb + tau_s * heater_gain_mC_per_s <= 60,000 mC` for the initial ambient and
every disturbance. The product and sum are checked in signed 64-bit
intermediate arithmetic. Disturbances must be ordered and no later than the
configured maximum runtime. An offset of zero is applied when the model is
initialized. Other disturbances are applied after the completed Euler step at
the first 5-second boundary whose elapsed model time reaches their offset.
Thus an offset between boundaries takes effect at the next boundary.

An invalid model temperature is a fault; it is never clipped. Only the agreed
temperature-to-DAC mapping clips the resulting DAC code to [100, 4000].
Telemetry periods are allowed at any nonzero millisecond interval. When several
periods become due between 5-second model samples, one current sample is sent
and `sample_index` advances over all intervening scheduled samples, exposing
unavailable/dropped samples as gaps.

## MCP4725 and safe output

The logical 7-bit address is `0x60`; HAL receives shifted `0xC0`. The driver
uses the DAC-register-only write command (`C2:C1:C0 = 010`) and never writes
EEPROM. It reads the six-byte read-mode sequence and compares the live DAC
register value from bytes 2-3 (one-based); it does not infer analogue output
voltage from register readback. Each blocking HAL operation has a 20 ms HAL
timeout. PLANT_VALID stays low until CONFIG_SET validation and initial write
plus register readback succeed. A successful register comparison does not
verify VOUT electrically.

## Heater-interface startup diagnostic

PB0 (`HEATER_CMD`) remains an input with no internal pull and is configured
for both rising and falling EXTI0 edges. After each valid `CONFIG_SET` has
passed validation and the initial DAC write/readback succeeds, the runtime
masks EXTI0, discards a stale pending edge, samples PB0, and arms the
diagnostic from that baseline. A low baseline requires a rising edge followed
by a falling edge. If PB0 is initially high, the diagnostic first requires a
falling edge to establish low, then a new rising edge and a final falling
edge. ARM_TEST continues to return `HARDWARE_NOT_READY` until the complete
sequence is observed. Applying a new valid configuration resets the previous
result; events before the diagnostic is armed are discarded.

The HAL EXTI callback handles PB0 only. It reads the pin and advances a
single-byte portable state machine; foreground code checks the completed
state when processing ARM_TEST. Foreground reset masks EXTI0 while changing
that byte, then clears pending state before re-enabling the IRQ. No protocol,
I2C, logging, or blocking work runs in the callback.

EXTI is edge-triggered, but it cannot promise capture of arbitrarily short
pulses. The peripheral pending bit may coalesce multiple same-line edges
before service, and a pulse shorter than the input/interrupt response limits
may not be represented as the required sequence. Confirm pulse width and
behavior on the actual board and wiring.

## Scheduling, telemetry, faults

TIM2 retains the CubeMX 5-second configuration. START_TEST clears CNT and UIF
before enabling update interrupts. The callback only marks a pending event and
an overlap flag. Foreground work integrates the previous heater sample, applies
due ambient changes, samples PB0 for the next interval, writes/verifies the
DAC, and then creates telemetry. There is no catch-up integration.

`max_runtime_ms` is enforced from the regular foreground runtime service using
the monotonic 32-bit `HAL_GetTick()` millisecond counter. The check runs before
protocol handling and again immediately before a model step, independently of
the 5-second TIM2 model schedule. It uses unsigned elapsed-time subtraction, so
deadline comparison is wrap-safe for configured durations shorter than one
full tick-counter cycle. At or after the deadline, fault 7 is latched before a
new model step can start: TIM2 is stopped, `PLANT_VALID` is deasserted, and
`FAULT_REPORT` plus fault-aborted `TEST_COMPLETE` are queued when a run was
active.

DWT CYCCNT measures the complete `plant_runtime_service()` foreground call,
including shared protocol service, heater checks, and model work. A pass over
100 ms latches fault 5. This is runtime instrumentation, not evidence that the
physical target has passed the budget; board measurements are still required. The
maximum observed cycle count and over-budget count are exposed through
`plant_runtime_max_loop_cycles()` and
`plant_runtime_over_budget_count()` for debugger inspection.

Fault detail values: fault 2 carries the low 16 bits of the HAL I2C error
mask (a register mismatch may have detail zero); fault 5 detail 1 means a
pending/overlapping timer event and detail 2 means the 100 ms execution budget
was exceeded; fault 6 detail 2 means TIM2 start failed, detail 3 means TIM2
stop failed, and detail 4 means model/DAC mapping validation failed; fault 7
detail 0 means the local maximum runtime expired. Faults latch, stop TIM2,
deassert PLANT_VALID, report FAULT_REPORT, and report fault-aborted
TEST_COMPLETE when a run was active.

The runtime uses fixed storage. Native tests cover the model against an
independent double-precision reference, OFF/ON behavior, ambient steps,
equilibrium, validation and mapping boundaries. The native protocol and
transport tests remain part of the same CTest suite.
