# Milestone status and verification evidence

This page distinguishes source implementation, automated checks, cross-compilation, flashing, and bench observations. A PASS in one category does not imply a PASS in another.

**Evidence update: 2026-11-11.** Milestone 6 is complete for the selected Plant verification scope below. Physical results were reported by the project owner. The dynamic-run CSV has been preserved and its numerical results checked against the source model. No hardware experiment, firmware build, or test suite was rerun for this documentation update.

## Summary

| Milestone | Software implementation | Native tests / sanitizers | Cross-compilation | Flashing / physical evidence | Remaining work |
|---|---|---|---|---|---|
| 1 — Protocol framing | Implemented: portable framing/parser core | Historical module status records codec/parser checks; no fresh run here | Included in later target builds | No separate M1 physical test claimed | None specific to framing milestone; broader protocol features are separate increments |
| 2 — STM32 project integration | Implemented: common modules integrated into both target CMake builds | Historical records only; not rerun here | Both targets subsequently cross-built | No separate M2 bench claim | See later milestones for endpoint and system evidence |
| 3A — Payload serialization and validation | Implemented | Historical module status records native and sanitizer checks; not rerun here | Included in later target builds | No separate physical claim | Plant lifecycle and envelope validation were added with M6; DUT and integrated lifecycle work remains in M7/M8 |
| 3B — Reliability primitives | Implemented: TX/RX sequence diagnostics and accepted-command/response cache | Historical module status records native and sanitizer checks; not rerun here | Included in later target builds | No separate physical claim | Full application behavior is outside this milestone |
| 4 — Hardware communication transport | Implemented: USB CDC and interrupt-driven USART transport adapters and bounded queues | Historical module status records native transport checks; not rerun here | Included in later target builds | Transport was physically exercised in M5; Plant UART also exercised in M6 | Broader transport fault coverage remains open |
| 5 — Protocol application bring-up | Implemented: BOOT_REPORT, STATUS request/response, heartbeat and minimal dispatch | Historical module status records native app checks; not rerun here | Both targets later cross-built | M5 firmware USB CDC (BlackPill) and USART1/FT232RL (BluePill) communication were physically verified. This evidence applies to the M5 firmware tested then, not the later M6 BluePill image. | Full DUT/Plant test lifecycle was not part of M5 |
| 6 — Plant simulator, including 6A/6B | Complete for the selected scope: Plant runtime, EXTI diagnostic, independent runtime limit, and physical-validation utilities | Latest recorded results: native CTest 7/7; ASan/UBSan CTest 7/7; Python 55/55 | Both M6A targets linked with GNU Arm 14.2.1; a later BluePill Debug ELF was built and its ST-Link V2 flash verified | Owner-reported passes for UART/configuration, DAC, PB0/ARM, runtime expiry, timing, normal run, and dynamic response; CSV retained below | Coverage limitations below; CubeMX regeneration reproducibility is not recorded; M7/M8 remain open |
| 7 — DUT firmware | Not implemented | Not applicable | No M7-specific build claim | Not verified | Implement and verify DUT controller behavior |
| 8 — Integrated HIL operation | Not implemented | Not applicable | No M8-specific build claim | Not verified | Integrate and verify complete end-to-end test operation |

## Evidence boundaries and open checks

- **Software implementation** means the relevant source and build integration exist. It does not establish that a particular binary is running on a board.
- **Native tests** exercise host builds. They do not prove MCU timing, peripheral electrical behavior, or wiring.
- **Cross-compilation** establishes a target image linked successfully. It is not flashing or physical verification.
- **M5 physical communication** applies to the M5 firmware tested then. M6 Plant verification is separate evidence, recorded below; the DUT controller and integrated HIL operation are still unverified.
- **Historical M6 software checks:** the owner reported 6/6 native CTest, 6/6 ASan/UBSan CTest, 8/8 Python tests, and successful cross-builds of both targets. The suite later grew; these counts describe the earlier snapshot.
- **Recorded M6A checks:** Clang 21.1 with ASan/UBSan passed the seven-test host suite. Both targets linked using GNU Arm 14.2.1; reported linker usage was BlackPill RAM 10,392 B / FLASH 37,636 B and BluePill RAM 3,544 B / FLASH 33,492 B. These are build outputs, not measurements. CubeMX CLI was unavailable during that implementation, so the `.ioc` and affected EXTI code were synchronized manually. Subsequent bench checks establish the reported functional behavior, but do not establish CubeMX regeneration reproducibility.
- **Latest recorded Python checks:** 55/55 passed after the interactive-console fix, including 24 heater-response utility tests. These use host simulations; they are distinct from the physical experiments and were not rerun for this documentation change.

## Milestone 6 physical Plant verification

These are project-owner bench observations, supported where noted by the
[preserved dynamic-run evidence](../test_automation/evidence/milestone6/README.md).
They close the selected nominal Plant checks, not exhaustive hardware qualification.

| Experiment | Recorded result | Evidence boundary |
|---|---|---|
| UART/configuration bring-up | Plant USART1/FT232RL communication, CONFIG_SET and READY verified | Owner-reported physical run; separate from earlier M5 transport verification |
| Initial DAC check | MCP4725 code 2050 and analogue VOUT verified; earlier owner measurements were VCC 3.29 V and VOUT 1.647 V | One measured operating point, not calibration across the DAC range |
| Heater-interface startup check | Physical PB0 LOW→HIGH→LOW diagnostic and ARM_TEST/ARMED readiness verified | Nominal manual level sequence; no claim for arbitrarily short pulses |
| Independent maximum runtime | `max_runtime_ms=2000` expired before the 5000 ms model step; fault 7, PLANT_VALID=0 and fault-aborted TEST_COMPLETE | Protocol V1.0 outcome is `2` (fault-aborted), referred to as FAULT in the bench result. An exact trip timestamp was not supplied |
| DWT timing | `SystemCoreClock=72000000`, `p.max_cycles=98520`, `p.over_budget=0`; maximum observed service call ≈1.368 ms | Observed maximum for that run, not a worst-case execution-time guarantee |
| Normal Plant run | Normal completion with three 5-second model steps | Separate from the longer dynamic-response experiment |
| Dynamic heater response | Normal completion; 14 telemetry samples, all adjacent intervals 5000 ms; maximum 23.991 °C, final 21.924 °C; maximum absolute reference error <0.49 m°C | Owner-reported completion; sample statistics independently checked from the CSV; analogue measurements below are separate evidence |

### Dynamic response and analogue observations

The [unaltered CSV](../test_automation/evidence/milestone6/plant_heater_response.csv)
contains test ID 1, sample indices 0–13, and MCU uptime 26946–91946 ms. Recomputing
the Forward Euler reference with the previous sample's heater latch gives a
maximum absolute error of **0.4898834 m°C**. All recorded DAC codes match the
linear temperature mapping; the CSV contains no validation-error entries.

The selected utility configuration uses initial/ambient 20 °C, `tau_s=30`,
heater gain 200 m°C/s, model/telemetry periods 5000 ms, a 120000 ms maximum
runtime and zero disturbances. The CSV does not contain the raw CONFIG_SET,
ACK or STOP exchanges; normal completion is the owner's separate report.

For this dynamic experiment the owner measured **DAC VCC=3.28 V**:

| Point | Temperature / DAC code from telemetry | Predicted VOUT (`VCC × code / 4096`) | Multimeter VOUT reported by owner |
|---|---|---|---|
| Initial | 20.000 °C / 2050 | 1.6416 V | 1.645 V |
| Final | 21.924 °C / 2144 | 1.7169 V | 1.718 V |

These initial/final measurements support analogue output response at the
observed points. They do not measure every intermediate voltage or establish
absolute accuracy across the full DAC range; meter uncertainty and calibration
were not supplied.

### Heater-input latching observed on the bench

Each model step integrates the previously latched input, then samples PB0 for
the next interval and publishes that new latch in `TELEMETRY.heater_input`.
The PC's `cooling` phase denotes the prompt to remove HIGH, not confirmation
that the physical pin is already LOW.

In the CSV, the cooling prompt phase begins at sample 8 while `heater_input`
is still HIGH. Sample 9 first reports LOW, but its completed interval still
uses the previous HIGH and reaches the 23.991 °C maximum. Sample 10 is the
first completed LOW interval and falls to 23.326 °C; four LOW integrations
finish at 21.924 °C. This is the established sample-and-latch behavior, not a
firmware defect or evidence of incorrect cooling.

### Timing interpretation and remaining limits

`98520 / 72000 = 1.368333… ms`, below the 100 ms / 7,200,000-cycle threshold.
The maximum is retained over all measured `plant_runtime_service()` calls
since initialization, including I2C operations invoked there and interrupt
preemption; it excludes the preceding `plant_transport_service()`. The counter
does not label the operation that produced the maximum. Zero over-budget
events applies to that observation interval only.

Remaining work includes hardware fault injection, additional model settings
and disturbances, analogue range/reference/calibration characterization, and
timing under other loads and failure paths. The retained CSV has no wall-clock
timestamp, immutable firmware/ELF identifier, raw frame log, DWT snapshot, or
multimeter trace. The documentation-update date is not a reconstructed test
timestamp. Milestone 7 DUT implementation and Milestone 8 integrated HIL
verification remain incomplete.

## Historical measurements and build sizes

Earlier milestone notes contain image sizes, RAM totals, test counts, and board observations from intermediate snapshots. The M6A sizes above are retained as historical build outputs, not presented as measurements from the dynamic experiment. Future captures should record the firmware revision/ELF hash, toolchain, command, date, and measurement conditions alongside their results.
