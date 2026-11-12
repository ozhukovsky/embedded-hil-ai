# Milestone 7A — DUT Safe Sensing Foundation: hardware verification

**Report date:** 2026-11-12.

**Evidence:** Project-owner physical bench measurements and
diagnostics reported during Milestone 7A verification on 2026-11-12.

**Disposition:** PASS for the selected functional sensing and safe-output
checks below, with explicit OBSERVATION and NOT TESTED items. This is not
calibrated analogue qualification or completion of the full DUT/controller
milestone.

## 1. Scope and evidence boundaries

This report preserves tests already performed by the owner. No hardware test,
firmware build, automated test suite, or flashing operation was performed while
preparing this document. No source changes, commit, or push are part of this
documentation task.

Milestone 7A exercises the DUT safe sensing foundation: ADC acquisition,
Plant-valid qualification, invalid-feedback handling, timing-fault detection,
USB status communication, and a heater command held LOW. The DUT remains in
`WAIT_CONFIG`; a valid Plant and valid feedback do not mean the DUT is READY,
ARMED, RUNNING, or controlling a closed loop.

The temperature points are **configured simulated Plant temperatures represented
by the DAC**, not measured physical chamber or ambient temperatures. Physical
voltage measurements, debugger snapshots, and protocol STATUS values are
identified separately below. A software `heater_high=0` is not itself a physical
PB0 voltage measurement.

The available conversation record contains a consolidated account of the earlier
measurements and direct owner diagnostic/confirmation messages for the final
TIM2 fault and reset checks. Individual test timestamps, instrument models and
calibration uncertainties, scope acquisition settings/captures, complete raw
frame logs, and an immutable identifier/hash of the flashed firmware were not
preserved with this evidence. Consequently, this report does not establish that
the tested binary is identical to a particular GitHub revision or current local
build.

### Result labels

| Label | Meaning in this report |
|---|---|
| **PASS** | The owner-reported result supports the named functional check under the recorded conditions. It does not imply exhaustive coverage or calibrated accuracy. |
| **OBSERVATION** | A measured variation, warning, or evidence limitation remains documented without claiming resolution. |
| **NOT TESTED** | The specific behavior was not physically demonstrated in the recorded work, or the evidence needed to qualify it is absent. |

## 2. Bench configuration

| Item | Reported configuration |
|---|---|
| DUT | STM32F411CEU6 BlackPill |
| Plant | STM32F103C8T6 BluePill |
| DAC | MCP4725, nominal 3.3 V supply |
| Analogue feedback | MCP4725 VOUT to BlackPill PA1 / ADC1_IN1 |
| Heater command | BlackPill PB0, active HIGH; held LOW for 7A |
| Plant validity | BlackPill PB1 receives `PLANT_VALID` from the Plant |
| Plant communication | COM3, USART1 through FT232RL |
| DUT communication | COM9, native USB CDC |
| Reference | Shared ground between DUT, Plant, DAC, and serial adapter |
| Diagnostics | ST-Link debugger / Live Watch, protocol STATUS and HEARTBEAT; multimeter and oscilloscope for the physical observations identified below |

The project context defines the shared 5 V supply arrangement, separate board
3.3 V regulator outputs, DAC power from BlackPill 3.3 V, and external 6.2 kΩ
pull-downs on the heater and Plant-valid receiver lines. These are architecture
context, not new measurements from this report. Earlier power-ripple and USB
current-margin limitations remain outside this verification's closure.

## 3. Verification summary

| Check | Result | Evidence / qualification |
|---|---|---|
| Heater startup safety | **PASS** | PB0 reported LOW; no startup HIGH pulse observed on the oscilloscope. Capture limits were not fully recorded. |
| DUT USB communication | **PASS** | STATUS responses and periodic HEARTBEAT observed; congestion warning remains a separate observation. |
| ADC at −20, +20, +60 °C | **PASS** | Functional response at three configured Plant points; absolute accuracy is not qualified. |
| `PLANT_VALID` loss and restoration | **PASS** | Flags `0x0025 → 0x0020 → 0x0025`, without DUT reset. |
| ADC out-of-range rejection | **PASS** | PA1 measured 0 V; raw 1; 674 range errors; feedback invalid. |
| Restored analogue wiring after power cycle | **PASS** | Valid feedback returned after power-off rewiring and power-on; cold-start restoration only. |
| ADC conversion timeout handling | **PASS** | 10 acquisition errors, `HAL_TIMEOUT`, feedback invalid. |
| Live recovery after ADC timeout | **PASS** | No DUT reset; nine further successful acquisitions; feedback valid again. |
| Missing TIM2 event detection | **PASS** | Debugger-assisted injection produced fault 5/detail 3 and invalid feedback. |
| Physical safe output during TIM2 fault | **PASS** | BlackPill PB0 measured 0 V with a multimeter during the fault. |
| Recovery after timing-fault reset | **PASS** | DUT reset; fault code 0, raw 2063, feedback valid. |
| Analogue variation / accuracy | **OBSERVATION** | +60 °C raw 3965–3996; rare 3965 is outside the provisional point-accuracy band. |
| Historical USB TX warning | **OBSERVATION** | STATUS bit 5 persisted after communication recovered; diagnostic behavior is not fully resolved. |
| Live ADC out-of-range recovery | **NOT TESTED** | Wiring restoration required a system power cycle. |
| Calibrated analogue accuracy | **NOT TESTED** | No traceable end-to-end calibration, reference-ratio characterization, or final error budget. |
| DUT 7B/7C and integrated closed-loop operation | **NOT TESTED** | Outside 7A; no completion claim for these increments or Milestone 8. |

## 4. Normal analogue sensing: three temperature points

The nominal mapping is DAC code 100 at −20 °C, 2050 at +20 °C, and 4000 at
+60 °C. These are mapping targets, not independent measurements. The DUT uses
a nominal DAC-supply / ADC-reference ratio of 1; that assumption is not a
calibration.

| Configured Plant temperature | Physical DAC VOUT | Physical PA1 / wiring check | DUT ADC raw | DUT temperature snapshot | Other reported diagnostics |
|---|---|---|---|---|---|
| −20 °C | Approximately 0.079–0.085 V across successive measurements | PA1 0.085 V relative to BlackPill GND; direct PA1-to-VOUT difference 0 mV | 117 | −19651 m°C (−19.651 °C) | `feedback_valid=1`, `acquisition_errors=0`, `fault_code=0` |
| +20 °C | 1.645 V | No separate PA1 measurement retained for this point | 2042 | 19836 m°C (19.836 °C) | `feedback_valid=1`, `fault_code=0` |
| +60 °C | Approximately 3.20–3.21 V | PA1 approximately 3.20 V | 3965 snapshot; 3965–3996 observed in Live Watch | 59282 m°C (59.282 °C) snapshot | All observed raw values remained within the firmware plausibility range |

For the static +60 °C check, initial and ambient Plant temperatures were both
`60000 m°C`, with `heater_gain_mC_per_s=0`.

**PASS — functional sensing:** the three points demonstrate a sensible response
across the selected simulated temperature range.

**OBSERVATION — analogue accuracy:** the provisional +60 °C point-accuracy band
was raw **3968–4032** (4000 ±32 counts). A rare raw **3965** was **3 counts below**
that band. It still falls inside the firmware's broader plausibility range
**68–4032**. Plausibility acceptance and point-accuracy acceptance are different
checks; not every observed sample passed the provisional accuracy tolerance.

The record does not quantify noise statistics or establish whether reference
ratio, offset, noise, or other analogue effects explain the variation. Meter
resolution, uncertainty, and simultaneous sample alignment are unavailable.
No calibration or final temperature-accuracy qualification is claimed. Earlier
three-point diagnostic statistics in PROJECT_CONTEXT.md belong to prior
bring-up and are not substituted for these 7A measurements.

## 5. Plant-valid loss and recovery

The owner reset/reconfigured the Plant while leaving the DUT running. DUT STATUS
flags changed as follows:

```text
Plant valid / feedback valid:  0x0025 -> 0x0020 -> 0x0025
```

Bits 0 and 2 indicate Plant validity and feedback validity respectively; bit 1
(heater command HIGH) remained clear. Bit 5 (`0x0020`) is the TX warning discussed
in Section 10 and was present through this sequence.

**PASS:** loss and restoration of Plant validity and feedback validity were
observed without resetting the DUT. This does not prove capture of arbitrarily
short PB1 glitches between software observations.

## 6. ADC out-of-range injection and cold-start restoration

### 6.1 Out-of-range rejection

Power was turned OFF before rewiring. MCP4725 VOUT was disconnected from PA1,
and PA1 was connected to GND through a **10 kΩ resistor**. After power-on, the
Plant was configured normally and reached READY. PA1 was physically measured
at **0 V**.

| Diagnostic | Observed value |
|---|---|
| DUT STATUS state | 2 (`WAIT_CONFIG`) |
| STATUS flags | `0x0021` |
| `timer_events` | 674 |
| `samples_acquired` | 674 |
| `acquisition_errors` | 0 |
| `range_errors` | 674 |
| `adc_raw` | 1 |
| `feedback_valid` | 0 |
| `plant_valid` | 1 |
| `heater_high` | 0 (software diagnostic) |
| `fault_code` | 0 |

**PASS:** conversions completed, but the implausible input was rejected and
feedback stayed invalid despite the Plant-valid input being HIGH. A zero
runtime fault code is consistent with this 7A invalid-sample/retry path; it
does not make the out-of-range sample valid.

### 6.2 Restored wiring after power cycle

The entire system was powered OFF before reconnecting MCP4725 VOUT to PA1.
After power-on and Plant configuration, the owner recorded:

| Diagnostic | Observed value |
|---|---|
| STATUS flags | `0x0025` |
| `adc_raw` | 2048 |
| `temperature_mC` | 19959 |
| `feedback_valid` | 1 |
| `fault_code` | 0 |

**PASS — cold-start restoration:** normal sensing returned with the wiring
restored. **NOT TESTED — live out-of-range recovery:** the DUT was power-cycled,
so this result cannot demonstrate recovery from an out-of-range sample while
the same DUT runtime continues.

## 7. ADC timeout injection and live recovery

### 7.1 Timeout handling

Using ST-Link, the owner changed `ADC1.CR2.EXTEN` from `00` to `01` to suppress
the normal software-triggered acquisition path and produce a conversion
timeout. This was a debugger-assisted injection, not a naturally occurring ADC
failure.

| Diagnostic | Timeout snapshot |
|---|---|
| `timer_events` | 181 |
| `samples_acquired` | 171 |
| `acquisition_errors` | 10 |
| `poll_status` | 3 (`HAL_TIMEOUT`) |
| `sample_result` | 3 |
| `last_acquisition_ms` | 6 |
| `feedback_valid` | 0 |
| `plant_valid` | 1 |
| `heater_high` | 0 (software diagnostic) |
| `fault_code` | 0 |

**PASS:** failed acquisitions were counted and feedback invalidated, while the
Plant remained valid and the heater diagnostic stayed LOW. The recorded 6 ms
is a HAL-tick diagnostic for this acquisition, not an independently measured
worst-case duration.

### 7.2 Live recovery without resetting the DUT

The owner restored `ADC1.CR2.EXTEN` from `01` to `00` through the debugger and
**did not reset the DUT**.

| Diagnostic | Before restoration | After restoration |
|---|---|---|
| `timer_events` | 181 | 190 |
| `samples_acquired` | 171 | 180 |
| `acquisition_errors` | 10 | 10 |
| `poll_status` | 3 (`HAL_TIMEOUT`) | 0 (`HAL_OK`) |
| `feedback_valid` | 0 | 1 |
| `adc_raw` | Not retained for timeout snapshot | 2055 |
| `temperature_mC` | Not retained for timeout snapshot | 20103 |
| `fault_code` | 0 | 0 |
| `heater_high` | 0 | 0 |

**PASS — live ADC timeout recovery:** nine additional timer events produced nine
additional successful acquisitions, without increasing the error count, and
feedback became valid again within the same DUT runtime. This is separate from
the cold-start wiring restoration in Section 6.

## 8. Missing TIM2 event injection and reset recovery

### 8.1 Missing-event detection

The owner disabled `TIM2.DIER.UIE` with the debugger while leaving
`TIM2.CR1.CEN` enabled. This suppressed update interrupts while the counter
remained enabled at injection. It verifies the injected missing-callback path,
not every possible timer or CPU failure.

| Diagnostic | Fault snapshot |
|---|---|
| `timer_events` | 10 |
| `sample_attempts` | 10 |
| `samples_acquired` | 10 |
| `overlap_events` | 0 |
| `acquisition_errors` / `range_errors` | 0 / 0 |
| `event_late` | 1 |
| `fault_code` | 5 |
| `fault_detail` | 3 |
| `timer_running` | 0 |
| `feedback_valid` | 0 |
| `plant_valid` | 1 |
| `heater_high` | 0 (software diagnostic) |
| Last retained `adc_raw` / `temperature_mC` | 2038 / 19754 |

**PASS:** the DUT detected the missing event, latched timing fault 5/detail 3,
stopped acquisition, and invalidated feedback even though the Plant remained
valid. Retained ADC/temperature history did not constitute valid current
feedback. No exact physical fault-detection latency was recorded.

### 8.2 Physical PB0 measurement during fault

With the timing fault active, the owner measured **BlackPill PB0 relative to GND
at 0 V with a multimeter** and explicitly confirmed the result in the chat.

**PASS:** the physical heater-command pin was LOW during this injected timing
fault. This supplements `heater_high=0`; it does not prove the absence of short
transients during every fault condition.

### 8.3 Recovery by DUT reset

The owner reset the BlackPill, leaving the Plant operational, then confirmed:

| Diagnostic | After reset |
|---|---|
| `fault_code` | 0 |
| `adc_raw` | 2063 |
| `feedback_valid` | 1 |

**PASS — reset recovery:** valid acquisition resumed after DUT reset. This does
not demonstrate automatic live clearing of the latched timing fault; reset is
the intended recovery boundary for this fault.

## 9. Physical heater-output evidence

| Condition | Physical evidence | Boundary |
|---|---|---|
| Startup / safe-state observation | PB0 reported LOW; no startup HIGH pulse observed on the oscilloscope | Scope trigger, time base, bandwidth, capture duration, and pulse-detection limit were not fully recorded; no universal no-glitch guarantee |
| Injected TIM2 timing fault | PB0-to-GND measured 0 V with a multimeter | Confirms the observed steady physical level during this fault |
| ADC out-of-range and timeout snapshots | `heater_high=0` in diagnostics | No separate physical PB0 voltage or transient capture retained for each of these injections |

These checks support the selected 7A safe-output behavior. They do not qualify
arbitrary CPU, GPIO, bus, or power failures, nor a heater power stage or actuator.

## 10. USB communication and TX warning

**PASS — communication:** DUT USB CDC STATUS and periodic HEARTBEAT were
verified in the reported bench work. Communication subsequently recovered
after host-side congestion.

**OBSERVATION — persistent TX diagnostic:** delayed opening of the host COM port
was associated with USB TX congestion counters and historical STATUS bit 5.
The warning could remain set after normal communication resumed, as reflected
by `0x0025` with otherwise valid sensing. Exact counter values are not retained
here. The root cause and clearing/latching semantics of the persistent warning
are not fully resolved. It is not presented as either a proven harmless warning
or a resolved transport defect.

Recovered communication alone does not qualify all queued STATUS retry cases,
USB disconnect/reconnect sequences, or sustained-congestion behavior.

## 11. Remaining limits and milestone disposition

- **OBSERVATION:** analogue variation near +60 °C includes a sample outside the
  provisional point-accuracy band. Functional plausibility passed; accuracy
  qualification remains open.
- **OBSERVATION:** persistent USB TX warning behavior remains unresolved despite
  subsequent successful communication.
- **OBSERVATION:** startup scope capture limits and firmware/instrument
  traceability were not fully recorded. Earlier project-context power-integrity
  and current-margin caveats are not closed by these results.
- **NOT TESTED:** live recovery from ADC out-of-range without DUT reset/power cycle.
- **NOT TESTED:** traceable DAC-to-ADC calibration, final reference-ratio/error
  budget, and absolute accuracy across the full analogue range.
- **NOT TESTED:** exhaustive timing/load/fault coverage, full USB reconnect
  recovery, or arbitrary hardware-failure safety.
- **NOT TESTED:** Milestone 7B configuration/startup lifecycle, 7C controller,
  and Milestone 8 integrated closed-loop operation.

The recorded evidence supports completion of **Milestone 7A's selected physical
functional verification**, with the above limitations retained. It does not
establish that the tested implementation has been committed or published, or
that all Milestone 7 functionality is complete.

Previously recorded software checks in the local milestone documentation
(native CTest 9/9, ASan/UBSan CTest 9/9, Python 55/55, and successful DUT/Plant
cross-builds) are historical software evidence. They were not rerun for this
report and are not physical test outcomes.

## 12. Sources and documentation context

- Measurements and diagnostic results were reported by the project owner
  during physical bench verification on November 12, 2026. This report
  consolidates that owner-reported evidence; it is not a new execution log.
- [Milestone status and evidence](../MILESTONE_STATUS.md): evidence-category
  conventions and historical software results. The pre-existing local 7A
  software-only status predates this report's physical evidence.
- [DUT 7A bench procedure](../../test_automation/tools/README.md#dut-7a-safe-sensing-bench-check):
  local procedure and provisional limits; a procedure is not proof that every
  listed check was executed.
- [Architecture and wiring](../HIL_ARCHITECTURE_DIAGRAMS.md) and
  [Protocol V1.0](../HIL_PROTOCOL_V1.0.md): interface and protocol context.
- Local internal `docs/PROJECT_CONTEXT.md`, baseline 2026-11-10: architecture,
  analogue mapping/reference assumptions, and prior bring-up limitations.
  It was consulted without modification and is not required to publish this
  self-contained report.
- GitHub documentation conventions were checked in
  [milestone status](https://github.com/ozhukovsky/embedded-hil-ai/blob/main/docs/MILESTONE_STATUS.md)
  and [PC tools](https://github.com/ozhukovsky/embedded-hil-ai/blob/main/test_automation/tools/README.md).
  The published documents available during preparation predate the local 7A
  verification material and are not evidence that GitHub contains the tested
  firmware.
