# PC bring-up utilities

These scripts are small serial-port bring-up tools, not a full test automation framework. They require Python 3.9+ and `pyserial`:

```powershell
python -m pip install pyserial
```

## Protocol smoke test

`hil_smoke_test.py` checks a selected DUT or Plant serial endpoint. It opens a COM port, sends an empty STATUS_REQUEST, validates the received frame CRC and STATUS payload/role, reports whether BOOT_REPORT was observed (BOOT_REPORT is optional for a late connection), then displays incoming frames such as HEARTBEAT.

BlackPill/DUT over USB CDC:

```powershell
python test_automation/tools/hil_smoke_test.py --role dut --port COM6
```

BluePill/Plant over FT232RL USART1 (115200 baud, 8-N-1):

```powershell
python test_automation/tools/hil_smoke_test.py --role plant --port COM7
```

Use `--status-timeout` and `--listen-seconds` to adjust the response and observation windows. A STATUS response must arrive for the selected role; a missed BOOT_REPORT alone is not a failure. This utility does not reset or flash a board and does not exercise a complete test run.

## Plant bring-up

`plant_bringup.py` sends a Plant configuration, waits for the Plant's acknowledgement/readiness status, and can observe subsequent protocol traffic. It depends on the framing and CRC helpers in `hil_smoke_test.py` and the same serial prerequisites.

```powershell
python test_automation/tools/plant_bringup.py --help
python test_automation/tools/plant_bringup.py --port COM7 --timeout 5 --listen-seconds 10
```

Supported options are `--port` (required), `--baud` (default 115200), `--timeout` (default 5 seconds), and `--listen-seconds` (default 10 seconds). The utility configures and checks the Plant; **it does not send ARM_TEST or START_TEST**. It does not verify DAC analogue voltage or the complete heater interface electrically.

## Plant PB0 interface test

`plant_interface_test.py` verifies the configured Plant's live PB0 input and
then asks the Plant to report the Milestone 6A startup diagnostic result. The
Plant must already be configured and in READY state. This tool intentionally
does not send CONFIG_SET, START_TEST, or a reset/reconfiguration command.

```powershell
python test_automation/tools/plant_interface_test.py --port COM3 --baud 115200 --timeout 5
```

Leave the external 6.2 kΩ pull-down between BluePill PB0 and GND in place and
ensure the BlackPill PB0 is disconnected from BluePill PB0. Use a common GND
for the 3.3 V source and BluePill. The utility requires typing `DISCONNECTED`
before it asks for signal injection. Apply **3.3 V only, never 5 V**, to
BluePill PB0 when prompted, and hold that level until HIGH is confirmed. Then
remove 3.3 V and hold LOW until LOW is confirmed. The default poll interval is
250 ms; `--poll-interval` can change it. The timeout applies independently to
the initial STATUS, each transition phase, and the ACK/ARMED response.

STATUS bit 1 reports the sampled GPIO level at the time firmware constructs
that STATUS response. It proves that firmware observed LOW and HIGH levels,
not that EXTI captured every physical edge. ARM_TEST is sent only after both
STATUS transitions are observed; its NACK or ordered ACK+ARMED response then
reports the firmware's diagnostic result, with ARMED readiness bit 4 indicating
that the startup functional check passed. Malformed frames and bad CRC
candidates are discarded by the shared host stream parser; if no valid
response follows, the operation times out. This is a manual bring-up utility,
not electrical or safety certification.

Expected successful result includes `PB0 HIGH observed in STATUS.`,
`PB0 LOW observed in STATUS`, `ARM_TEST accepted: ACK followed by ARMED.`,
and `ARMED readiness_flags=0x0017; heater-interface check bit 4=1.` The Plant
will be left in ARMED state; START_TEST is never transmitted.

## Plant `max_runtime_ms` timeout test

`plant_runtime_timeout_test.py` configures the Plant with a 2,000 ms local
maximum runtime and 5,000 ms model/telemetry periods, performs the PB0
LOW→HIGH→LOW diagnostic, arms once, starts once, and verifies fault 7 before
the first thermal-model step. The Plant must be freshly reset and in
WAIT_CONFIG. If it reports any other state, the utility exits without
reconfiguring it. It does not reset the board, send STOP_TEST, or retry
START_TEST.

```powershell
python test_automation/tools/plant_runtime_timeout_test.py --port COM3 --baud 115200 --test-id 1 --timeout 8
```

Before starting, disconnect BlackPill PB0 from BluePill PB0 and keep it
disconnected throughout. No physical heater, switching stage, or other
actuator may be connected. Preserve the external 6.2 kΩ pull-down from PB0 to
GND, use only 3.3 V for the manual interface check (never 5 V), and do not
change the existing DAC wiring or power arrangement. The utility prints these
conditions and requires typing `SAFE`, then separately requires typing
`DISCONNECTED` before the manual PB0 transition prompts. Hold each level until
STATUS confirms it. The Plant is left faulted after a successful timeout test;
the utility will not reset it for another run.

Fixed CONFIG_SET values are test ID from `--test-id` (default 1), initial and
ambient temperatures 20,000 mC, `tau_s=300`, heater gain 20 mC/s,
`model_step_ms=5000`, `telemetry_period_ms=5000`, `max_runtime_ms=2000`, and
zero disturbances. `--timeout` (default 8 seconds) bounds the run from
START_TEST transmission through fault/status confirmation; `--response-timeout`,
`--level-timeout`, and `--poll-interval` control the earlier interactions.

A pass requires matching START_TEST ACK, FAULT_REPORT code 7/detail 0,
fault-aborted TEST_COMPLETE with Plant-reported run elapsed from 2,000 ms up to
but excluding 4,000 ms, and STATUS state FAULT with PLANT_VALID low. It also
requires the host to observe FAULT_REPORT less than 4,000 ms after START_TEST
transmission, leaving at least a 1-second margin before the 5-second model
step. The report keeps host-observed time, FAULT_REPORT uptime since reset,
TEST_COMPLETE run elapsed, and the model period distinct. These timings include
foreground, serial, and host polling latency and do not imply an exact 2,000 ms
physical cutoff.

If the overall timeout expires or the Plant enters an unexpected state, do not
run another test until the Plant's state is safely recovered. The utility does
not send STOP_TEST or perform that recovery. This verifies protocol-visible
firmware behavior; it does not measure analogue DAC voltage or prove physical
timing more precisely than the reported host and MCU clocks.

## Plant DWT timing observation

`plant_timing_test.py` requires a freshly reset Plant in `WAIT_CONFIG`. It
configures the specified 20,000 mC initial/ambient temperatures, `tau_s=300`,
20 mC/s heater gain, 5,000 ms model and telemetry periods, 60,000 ms maximum
runtime, and zero disturbances. It checks READY, performs the PB0
LOW→HIGH→LOW diagnostic, verifies ARM_TEST/ARMED readiness bit 4, sends
START_TEST exactly once, observes for 18 to 50 seconds after its ACK, then
sends one normal STOP_TEST and verifies ACK, normal TEST_COMPLETE, and
COMPLETE STATUS. It never resets the Plant or retries START_TEST. If interrupted
or a response is lost, inspect the Plant state before attempting another run.

```powershell
python test_automation/tools/plant_timing_test.py --port COM3 --baud 115200 --test-id 1
```

Before running, disconnect BlackPill PB0 from BluePill PB0 and leave it
disconnected. Do not connect a heater, switching stage, or actuator. Preserve
the external 6.2 kΩ PB0 pull-down and the existing DAC wiring/power setup. Use
a common ground for the temporary signal source and Plant. Apply **3.3 V only,
never 5 V**, to PB0 for the requested HIGH transition, and remove it for LOW.
Hold each level until the utility confirms it by STATUS. PB0 must remain LOW
throughout the timed run. The utility requires `SAFE` and `DISCONNECTED`
confirmations before the diagnostic and run.

The STATUS level checks confirm sampled PB0 levels; they do not prove that
EXTI captured the edges. The ARM_TEST/ARMED response checks the firmware's
diagnostic result. The tool's 18-second host observation covers multiple
5-second model periods and displays received HEARTBEAT, STATUS, and TELEMETRY
frames. This is not a physical heater or actuator test.

### Inspecting DWT values without resetting the target

Before the run, ensure the BluePill Debug configuration is set up for an
**Attach to Running Target** session, with program download and reset-on-launch
disabled. The repository does not provide a CubeIDE `.launch` configuration,
so verify these settings in the local IDE before testing. This matters because
the counter values are RAM-only. After the
utility reports COMPLETE, leave the board powered and do not reset, reflash, or
reinitialize the Plant. Attach without download/reset, halt it only after the
test has completed, and evaluate these expressions in the Expressions view
while the debugger has the `plant_runtime.c` translation unit selected:

```text
p.max_cycles
p.over_budget
SystemCoreClock
```

Equivalent GDB expressions with the ELF loaded are:

```gdb
p/u SystemCoreClock
p/u p.max_cycles
p/u p.over_budget
```

The project defaults to CMake `Debug`; its GNU Arm toolchain flags include
`-O0 -g3`. Confirm the ELF contains DWARF information and the `p` and
`SystemCoreClock` symbols before performing the test; also check whether the
getter code was retained (for example, inspect with
`arm-none-eabi-readelf --sections <plant-elf>` and
`arm-none-eabi-nm -a -C <plant-elf>`). A normal launch configuration may download
the image or reset the MCU; do not use it for post-test inspection. If the
installed CubeIDE/ST-LINK setup cannot attach without reset, do not start a
normal debug launch after the test: it would erase the volatile counters. Keep
the no-reset debug session attached throughout the run as the safe fallback.
If no no-reset attach mode is available, the counters cannot be safely
recovered after the run; do not use a reset-capable launch just to read them.

The current project link uses section garbage collection. The checked Debug
ELF contains DWARF information and the static `p` object/member debug metadata,
but the unreferenced public getter function sections are discarded from the
final ELF. Thus the requested getter calls are not executable debugger
expressions for this build; use `p.max_cycles` and `p.over_budget` as above.
`SystemCoreClock` remains a linked symbol. Verify the exact ELF used for the
test rather than assuming every declared getter was retained.

At 72 MHz, the 100 ms threshold is 7,200,000 cycles, and
`execution_time_ms = cycles / 72000`. The maximum is over all measured
`plant_runtime_service()` calls since initialization. It includes I2C
operations made within that service and interrupt preemption, but excludes the
preceding `plant_transport_service()`. It is not labeled by event type and
therefore cannot be attributed specifically to a thermal model step. A zero
over-budget count means no measured service call exceeded the configured
threshold during the interval represented by the counter. The values are
volatile RAM diagnostics, not protocol telemetry; this utility does not read
them and does not flash firmware.

## Plant physical heater-response experiment (Milestone 6B)

`plant_heater_response_test.py` prepares a single manual PB0 experiment using
the existing firmware. It requires a freshly reset Plant in WAIT_CONFIG and
refuses to reconfigure an already READY, ARMED, RUNNING, COMPLETE, or faulted
Plant. Run this utility independently of the other bring-up tools; they can
otherwise consume the serial traffic or change the Plant state.

Measure the current DAC VCC and provide it through the required `--vcc` option.
The example uses the dynamic experiment's measured 3.28 V; replace that value if the
current measurement differs. From the repository root:

```powershell
python test_automation/tools/plant_heater_response_test.py --port COM3 --baud 115200 --test-id 1 --vcc 3.28 --csv "$env:TEMP\plant_heater_response.csv"
```

The optional `--csv` records every decoded sample used for validation, including
the sample that fails validation, its phase, reference temperature, error,
expected DAC code, predicted VOUT, and validation error. Samples and predictions
are also displayed as they arrive. Repeated identical samples add no model
interval; a gap or reordering makes the result INCOMPLETE because the missing
heater latch cannot be recovered safely.

### Wiring and operator procedure

- Disconnect BlackPill PB0 from BluePill PB0 and keep it disconnected throughout.
  No physical heater, switching stage, or actuator may be connected.
- Preserve the external 6.2 kΩ pull-down from BluePill PB0 to GND and the existing
  DAC wiring/power arrangement. Use a common GND with the temporary signal source.
- Apply **3.3 V only, never 5 V**, through approximately 1 kΩ in series with PB0.
  Remove the temporary connection to restore LOW through the external pull-down.
- Connect the multimeter to DAC VOUT and GND for independent analogue observation.
  The tool displays `VOUT = measured_VCC * dac_code / 4096` for each sample.
- Confirm `SAFE` and `DISCONNECTED` when requested. Complete the startup
  LOW→HIGH→LOW diagnostic, holding each level until STATUS confirms it. The tool
  checks ordered ARM_TEST ACK/ARMED and readiness bit 4, confirms ARMED/PB0 LOW,
  then sends START_TEST once.
- Hold PB0 LOW until the stable baseline is reported. Apply HIGH when the
  HEATING prompt appears and hold it until four complete on intervals have been
  observed. Remove HIGH when the COOLING prompt appears and keep PB0 LOW for at
  least four complete off intervals. STOP_TEST is then sent once with normal
  reason 0; ACK, normal TEST_COMPLETE, COMPLETE state, and PLANT_VALID LOW are
  required for success.

Run the utility in an interactive terminal. Console input and UART reception
share the main loop, using non-blocking keyboard input; no background thread
reads stdin. While a prompt is waiting, periodic STATUS_REQUEST transmissions
and routine UART console output are paused. UART frames still get parsed and
validated, including any outstanding STATUS reply. Telemetry is recorded
immediately and its console lines are displayed after the input line completes.
After Enter confirms a physical action, a fresh STATUS request verifies the
GPIO level and the usual 0.5-second polling resumes.

Before START_TEST, manual SAFE, DISCONNECTED, and startup-diagnostic prompts
have no operator timeout; this setup time does not consume the active runtime
budget. Serial health checks remain active, and verifying a level after Enter
has a 20-second timeout by default. After START_TEST, each manual prompt has a
20-second limit and displays the remaining active host budget. `--prompt-timeout`
can shorten these active prompt and GPIO verification limits. The 105-second
host run limit and 120-second MCU limit apply only after START_TEST, and the
existing safe stop is attempted if an active prompt or run times out.
`--response-timeout` defaults to 5 seconds and cannot exceed 5 seconds.

The configuration is fixed: initial/ambient 20,000 mC, `tau_s=30`, heater gain
200 mC/s, model/telemetry periods 5,000 ms, `max_runtime_ms=120000`, and zero
disturbances. The heater-on equilibrium is 26 °C. The tool never automatically
resets or flashes a board, starts a second run, or sends another configuration.
On missing data, timeout, cancellation, or validation failure after an attempted
START, it attempts one STOP_TEST with HOST_SAFETY_ABORT reason 2 when the board
may still be running. A known FAULT/COMPLETE state or an explicit START NACK does
not need a stop attempt. An unconfirmed stop is reported; inspect the Plant state
before another experiment. A failed stop is never retried automatically.

### Input latching, expected timeline, and numerical checks

The firmware integrates the previous `heater_latched` value first, then samples
PB0, writes/verifies the DAC, stores the newly sampled latch, and emits
telemetry. Therefore `TELEMETRY.heater_input` describes the **next** interval;
the preceding sample's `heater_input` drives the temperature reported now.
START_TEST initially latches the confirmed LOW input. The tool tracks these
actual observations rather than assigning a fixed sample-index offset to a
manual transition. The first sample reporting HIGH may still be 20 °C, and the
first sample reporting LOW may still show a temperature increase.

For prompt responses just after the relevant sample, an illustrative timeline
from START acceptance is:

| Time | Observation |
|---:|---|
| ~5 s | Stable LOW baseline, 20.000 °C; prompt to apply HIGH |
| ~10 s | HIGH is sampled for the next interval; temperature can remain 20.000 °C |
| ~15, 20, 25, 30 s | Four complete on integrations: 21.000, 21.833, 22.528, 23.106 °C |
| ~30 s | Prompt to remove HIGH |
| ~35 s | LOW is sampled; the previous HIGH latch can still produce 23.589 °C |
| ~40, 45, 50, 55 s | Four off integrations: approximately 22.991, 22.492, 22.077, 21.731 °C |
| After cooling | Normal STOP_TEST and COMPLETE confirmation |

Manual timing can move these boundaries or add intervals. The run is limited
to 105 host seconds from START transmission, leaving 15 seconds before the
120-second MCU fail-safe for stop/report latency. STOP confirmation has an
overall 10-second deadline covering write, ACK, TEST_COMPLETE, and final STATUS.
Delays that prevent finishing
within this window produce an incomplete result and a stop attempt.

The reference retains full precision in mC and applies
`Tnext = T + 5 * ((20000 - T) / 30 + 200 * completed_input)` for every observed
step; it does not round the reference after each integration. Temperature
agreement is checked within ±2 mC, allowing the firmware's nearest-mC output
rounding and floating-point evaluation differences. The DAC reference is the
documented linear mapping from -20..60 °C to code 100..4000, rounded to nearest;
±1 code allows floating-point behavior at half-code boundaries. Adjacent sample
indices must be contiguous, and unsigned uptime differences must be 5,000 ms
within ±250 ms. Timestamp subtraction supports MCU uptime wraparound; it does
not change the model's fixed 5-second integration step. Telemetry silence for
more than 8 seconds is incomplete even if heartbeats still arrive.

PASS requires a stable baseline, four increasing complete on intervals, four
decreasing complete off intervals toward 20 °C, model/DAC consistency, no
unexpected faults or state changes, and a normal stop. A digital PASS confirms
the telemetry and register-readback path observed during that run. Predicted
voltage and DAC readback do not prove dynamic analogue behavior: VOUT must be
observed independently with the multimeter, using the current measured supply.
STATUS and telemetry are sampled observations and cannot exclude brief PB0
glitches or measure physical edge timing. Host tests prepare this experiment;
they do not constitute a physical run or analogue verification.

## Recorded Milestone 6 bench results

The owner has completed the selected physical Plant experiments: UART and
configuration bring-up, DAC code/analogue-point checks, the PB0 startup
diagnostic with ARM_TEST, independent 2000 ms maximum-runtime expiry, a normal
three-step run, DWT inspection, and a dynamic heating/cooling run. The
[milestone status](../../docs/MILESTONE_STATUS.md#milestone-6-physical-plant-verification)
records the results and their limits; the [unaltered dynamic CSV](../evidence/milestone6/README.md)
preserves 14 samples from that physical experiment.

The dynamic run used measured VCC 3.28 V. Initial/final VOUT readings of
1.645 V / 1.718 V were supplied by the owner independently of the CSV's
predictions of approximately 1.6416 V / 1.7169 V. In the CSV, the PC `cooling`
phase begins before the heater latch changes to LOW. The sample first reporting
LOW still integrates the preceding HIGH; this is the firmware's intended
sample-and-latch order, not a failed cooling response. The example timeline
above illustrates prompt actions near sample boundaries; it is not the exact
timing of this longer manual experiment.

These completed experiments do not make every utility behavior, fault path,
configuration, or analogue operating point physically verified. Future runs
still require their own wiring checks, current VCC measurement, and result
capture. The latest recorded Python host regression run passed 55/55 tests;
it remains separate from bench evidence and was not rerun for this documentation update.

## DUT 7A safe sensing bench check

This is a procedure for the owner, **not a record of physical verification**.
Use the unchanged architecture wiring/power arrangement and external pull-downs.
No heater/actuator is connected. Keep the DUT PB0 to Plant PB0 link connected;
do not use the M6 manual-HIGH utilities for this procedure. The DUT must never
produce HIGH in 7A. Do not ARM or START either board.

1. Record revision, compiler, configuration and ELF hash for
   `build/dut-7a/dut_stm32f411.elf`. Flash it yourself and reset the DUT/Plant.
   Probe HEATER_CMD at both boards with a logic analyser; require LOW during
   reset, startup and every check below. Software fault-handler writes are best
   effort; they cannot guarantee output safety under arbitrary hardware faults.
2. Before Plant configuration, inspect `d` with `dut_runtime.c` selected in a
   matching Debug session. Require `plant_valid=0`, `feedback_valid=0`, no runtime
   fault, protocol state WAIT_CONFIG (2), test ID 0, STATUS bits 0/1/2 all zero.
   Raw ADC may still reflect a retained DAC voltage; this does not make it valid.
3. Run the existing DUT USB check (replace COM ports):

   ```powershell
   python test_automation/tools/hil_smoke_test.py --port COM4 --role dut
   ```

   Require a valid DUT STATUS and continued 1000 ms heartbeats. BOOT_REPORT is
   sent once when USB becomes ready and need not replay on a later connection.
   If captured, require firmware 0.7.0 and capabilities 0x0035 (USB, ADC, TIM2,
   HIL GPIO). Cold-start and enumeration are new bench checks, not proved by
   the earlier M5 USB observation. This smoke utility validates STATUS fields
   but does not display raw ADC or all flag values; inspect `protocol` and `d`
   in the debugger, or decode the existing STATUS payload.

   After audit remediation, boot/heartbeat TX submission waits for configured
   CDC with class data available; a blocked STATUS response is retained and
   retried before a new heartbeat. Configuration still does not prove the PC
   application is reading. Verify a single STATUS_REQUEST receives a response
   after temporary TX backpressure clears. Repeated blocked polls coalesce into
   one current snapshot; the pending flag clears on enqueue, not USB completion.
4. Configure the Plant without starting it:

   ```powershell
   python test_automation/tools/plant_bringup.py --port COM3
   ```

   Its existing default is initial/ambient 20000 mC, yielding DAC 2050. Verify
   Plant ACK/READY and PB1 HIGH. After the next DUT acquisition, expect STATUS
   bits 0 and 2 set and bit 1 clear (flags 0x0005 without other diagnostics).
   The DUT remains WAIT_CONFIG; the Plant's unfinished heater diagnostic is
   expected and is not part of 7A.
5. Watch `d.timer_events`, `sample_attempts`, `samples_acquired`,
   `last_attempt_ms`, `last_sample_ms`, `adc_raw`, `temperature_mC`,
   `sample_result`, `adc_error`, and `feedback_valid`. Require one acquisition
   per second, no overlap/errors, and `max_acquisition_ms <=100`. A provisional
   timestamp-interval check is 1000 +/-10 ms during this idle bench test; measure
   actual timing before treating that as a qualified bound. Avoid halting during
   timing measurement. Debugger halts can allow timer events to coalesce and do
   not constitute a normal workload.

   The runtime now checks the expected callback schedule even if no callback
   arrives. Each event is due on the original 1000 ms phase with **10 ms**
   provisional delivery tolerance; >10 ms late (including a missing first
   callback) latches fault 5/detail 3. A late ISR cannot rebase that deadline.
   Overlap is detail 1; acquisition >100 ms is detail 2. Inspect `next_event_ms`
   and `event_late` in `d`. No catch-up or automatic recovery is performed.
   The comparison requires a progressing HAL tick and service within 2^31 ms.
   Observed sample expiry permanently removes its qualification while retaining
   raw/timestamp history. A timing fault additionally requires reset before
   acquisition can resume.
6. Check selected static Plant-configured points. The default tool covers 20 C.
   Other points use ordinary Plant CONFIG_SET while Plant is READY, with initial
   and ambient temperature equal to the table value, `tau_s=300`, heater gain
   **0**, both periods 5000 ms, max runtime 600000 ms and no disturbances. Zero
   gain keeps the +60 C configuration inside the Plant envelope. Use a fresh
   command sequence for each changed configuration, wait for its matching ACK
   and valid READY status, then wait at least two DUT samples before measuring.
   No new firmware, custom protocol or standalone utility is required. For an
   interactive sender, reuse the existing frame encoder from the repo root:

   ```python
   import sys, struct, serial
   sys.path.insert(0, "test_automation/tools")
   import hil_smoke_test as wire
   p = serial.Serial("COM3", 115200, timeout=0.1)
   seq, test_id, temp_mC = 100, 2, -20000
   payload = struct.pack("<BIiiIiHHIB", 2, test_id, temp_mC, temp_mC,
                         300, 0, 5000, 5000, 600000, 0)
   p.write(wire.encode_frame(0x10, seq, payload))
   parser = wire.StreamParser()
   frames = parser.feed(p.read(256))  # repeat reads; print/inspect parsed ACK/NACK
   [(hex(f.message_type), f.payload.hex()) for f in frames]
   ```

   Keep the parser across reads. Confirm ACK 0x20 has the command sequence,
   acknowledged type 0x10, role 2 and error 0; send STATUS_REQUEST (0x04) with a
   fresh sequence and decode READY=3, fault=0 and flags bits 0/3 set. Do not
   proceed on NACK or missing confirmation. Repeat with new sequence/test ID
   and `temp_mC=60000`, rebuilding the payload; close `p` before another utility
   opens COM3. This is an interactive example, not a stop-and-wait test runner.

   | Configured temperature | DAC code | Ideal ADC, ratio=1 | Ideal converted result | Predicted VOUT at VCC=3.3 V |
   |---|---:|---:|---:|---:|
   | -20 C | 100 | 100 | -20000 mC | 0.08057 V |
   | +20 C | 2050 | 2050 | 20000 mC | 1.65161 V |
   | +60 C | 4000 | 4000 | 60000 mC | 3.22266 V |

   These are nominal expectations, not measured results. Measure the current
   DAC supply and VOUT separately; predicted VOUT is VCC*code/4096. The initial
   proposed point-comparison tolerance is **+/-32 ADC counts**, approximately
   **+/-0.657 C** with the nominal inverse. The global plausibility range is
   separately 68..4032. Neither is calibrated/physically verified. One ADC count
   is approximately 20.513 mC; nearest-mC software rounding contributes <=0.5 mC.
   Historical endpoint/noise observations motivated the 32-count allowance,
   not a fitted gain/offset calibration. DAC 2048 corresponds nominally to
   19.959 C, not exactly 20 C.
7. Reset the Plant while the DUT continues. Require observed PB1 LOW, STATUS
   bit 0/bit 2 clear, and PB0 LOW; DUT stays WAIT_CONFIG without fault 4. After
   reconfiguration, bit 2 must wait for a new successful sample after HIGH.
   Brief LOW/HIGH glitches between polls are not guaranteed to be detected.
8. For invalid analogue input, first isolate PA1 from DAC VOUT, then tie the
   isolated ADC input to GND. Require raw near zero, OUT_OF_RANGE (8), increasing
   range-error count, feedback bit 2 clear and PB0 LOW. Never short the driven
   DAC output. An open input alone can float into range and cannot prove this
   check. Restore wiring and require a fresh valid sample to recover.
9. An analogue disconnect does not cause an ADC completion timeout. For a
   controlled debugger-only timeout test, break just before `HAL_ADC_Start`
   with the previous conversion stopped. In the debugger set
   `hadc1.Instance->CR2 = (hadc1.Instance->CR2 & ~0x3F000000U) | 0x10000000U`
   (EXTSEL=TIM1_CC1, EXTEN=rising); TIM1 is not configured in this firmware.
   This suppresses HAL software start and waits for an absent trigger. Merely
   changing `hadc1.Init` would not change the hardware trigger bits used by
   HAL_ADC_Start. Resume with SysTick running. Expect a 5 ms poll
   timeout (tick granularity adds latency), `sample_result=3`, poll status
   HAL_TIMEOUT=3, `sample_ok=0`, bit 2 clear, cleanup attempted, and PB0 LOW.
   Reset immediately to restore the generated configuration; reconfigure the
   Plant if needed. This debugger intervention deliberately invalidates normal
   timing evidence. Host tests already cover timeout/error logic; this bench
   injection remains unverified.

The declared getter `dut_runtime_snapshot()` may be garbage-collected when
unreferenced; inspect retained `d` directly with the matching ELF. Coherent
multi-field reads may require a halt; keep those separate from timing captures.
STATUS can additionally show bit 5 after TX queue overflow (for example with
configured CDC but a non-reading host). That indication latches through
counters; reset before a clean regression capture. Timer overlap or acquisition
>100 ms enters latched FAULT (5); TIM2 start failure uses fault 6. Capture status,
snapshot, probe results and measured voltages; do not call 7A physically verified
until these checks have actually been performed.

## Host regression tests

From the repository root:

```powershell
python -m unittest discover -s test_automation/tests -v
```

Host tests validate Python utility behavior and protocol handling; they do not establish physical serial communication or peripheral behavior on a target board.
