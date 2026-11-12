# Milestone status and verification evidence

This page distinguishes source implementation, automated checks, cross-compilation, flashing, and bench observations. A PASS in one category does not imply a PASS in another.

**Evidence update: 2026-11-12.** Milestone 6 remains complete for the selected Plant verification scope below. Its physical results were reported by the owner; its retained CSV was checked against the source model. Milestone 7A implementation is complete and its selected physical functional verification is **PASS**, based on project-owner bench results reported on November 12, 2026. See the [7A hardware verification report](verification/MILESTONE_7A_HARDWARE_VERIFICATION.md) for measurements, observations, untested items, and evidence limitations. Milestones 7B and 7C are not yet implemented; Milestone 8 closed-loop integration is not verified.

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
| 7 — DUT firmware | 7A implementation complete; 7B lifecycle and 7C controller not yet implemented | 7A: native 9/9, ASan/UBSan 9/9, Python 55/55 | Both 7A targets Debug linked with GNU Arm 14.2.1 | **PASS** for selected 7A physical functional verification; [owner-reported bench evidence and limits](verification/MILESTONE_7A_HARDWARE_VERIFICATION.md) | Implement 7B/7C; retain 7A observations and untested limits |
| 8 — Integrated HIL operation | Not implemented | Not applicable | No M8-specific build claim | Closed-loop integration not verified | Integrate and verify complete end-to-end test operation |

## Evidence boundaries and open checks

- **Software implementation** means the relevant source and build integration exist. It does not establish that a particular binary is running on a board.
- **Native tests** exercise host builds. They do not prove MCU timing, peripheral electrical behavior, or wiring.
- **Cross-compilation** establishes a target image linked successfully. It is not flashing or physical verification.
- **M5 physical communication** applies to the M5 firmware tested then. M6 Plant verification is separate evidence, recorded below; the DUT controller and integrated HIL operation are still unverified.
- **Historical M6 software checks:** the owner reported 6/6 native CTest, 6/6 ASan/UBSan CTest, 8/8 Python tests, and successful cross-builds of both targets. The suite later grew; these counts describe the earlier snapshot.
- **Recorded M6A checks:** Clang 21.1 with ASan/UBSan passed the seven-test host suite. Both targets linked using GNU Arm 14.2.1; reported linker usage was BlackPill RAM 10,392 B / FLASH 37,636 B and BluePill RAM 3,544 B / FLASH 33,492 B. These are build outputs, not measurements. CubeMX CLI was unavailable during that implementation, so the `.ioc` and affected EXTI code were synchronized manually. Subsequent bench checks establish the reported functional behavior, but do not establish CubeMX regeneration reproducibility.
- **Latest recorded Python checks:** 55/55 passed after the interactive-console fix, including 24 heater-response utility tests. These use host simulations and are distinct from the physical experiments; fresh finalization results are recorded below.

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
timestamp. Milestone 7A implementation and selected physical verification are
complete; 7B/7C remain unimplemented and Milestone 8 closed-loop integration
remains unverified.

## Milestone 7A safe sensing

**Implementation: complete. Selected physical functional verification: PASS.**
The [hardware verification report](verification/MILESTONE_7A_HARDWARE_VERIFICATION.md)
records the owner-reported bench results from November 12, 2026. Its
PASS/OBSERVATION/NOT TESTED distinctions remain applicable: analogue accuracy
and persistent USB TX diagnostics remain observations; calibration, live
out-of-range recovery, and broader fault coverage are not qualified.
These results do not complete 7B/7C or verify Milestone 8 closed-loop integration.

The DUT starts its existing TIM2 at 1 s and acquires ADC1_IN1 in the foreground.
HEATER_CMD is only driven LOW. Initialization enters WAIT_CONFIG; DUT lifecycle
handlers, startup HIGH pulses, controller, RUNNING telemetry and watchdog remain
absent. Clock, ADC, GPIO, NVIC, USB descriptors and `.ioc` settings are unchanged.
Error_Handler and fatal CPU exception handlers attempt safe output. USB submission
and rearm check readiness with IRQ protection; complete reconnect recovery is
outside 7A. Software cannot guarantee safety after CPU/bus/GPIO/power failure.

Nominal conversion assumes DAC supply / ADC reference = 1 and inverts the
100..4000 code / -20..60 C mapping with nearest-mC rounding. A provisional
32-count margin accepts raw 68..4032, covering historical observations without
using them as calibration. Temperature is not clamped at the margin. STATUS
bit 2 also requires an error-free sample, PLANT_VALID HIGH before and after it
and still HIGH, no runtime fault, and age <=1100 ms. Observing LOW requires a
new sample after HIGH returns. Observed sample expiry also clears qualification
persistently, so a complete tick-counter cycle cannot revive the old sample.
These checks do not prove wire continuity or
absolute analogue accuracy.

The RAM-only snapshot `d` in `dut_runtime.c` records samples, HAL results,
timestamps, counters, GPIO observations, pending/acquiring/overlap state, and
maximum acquisition duration in HAL ticks. The expected TIM2 event deadline is
phase-locked to timer startup and advances by 1000 ms per on-time callback,
with a provisional 10 ms allowance for interrupt delivery/tick quantization.
Foreground checks detect missing callbacks, including the first; a late callback
sets a sticky flag before it can advance the schedule. Either path latches
fault 5/detail 3, distinct from overlap (detail 1) and acquisition >100 ms
(detail 2). There is no catch-up or automatic recovery. The modular deadline
comparison requires observations less than 2^31 ms apart and a progressing HAL
tick; this is not an independent watchdog. ADC polling uses a 5 ms timeout;
stop is attempted on every path. ADC errors/range failures invalidate feedback
and permit the next scheduled retry in WAIT_CONFIG. Timer overlap or acquisition
>100 ms latches fault 5 and stops acquisition; TIM2 start failure latches fault 6.
Fault reports are best effort, STATUS exposes the latch, and reset is required.
No test is active, so no TEST_COMPLETE is sent. Timing uses HAL ticks, not DWT;
timeouts require a progressing tick. Interrupt-masked/coalesced timer events
cannot be counted reliably. Brief PLANT_VALID glitches between polls may escape
observation.

Initial 7A checks on 2026-11-11 (before targeted remediation; current results below):

- GNU native CTest: **9/9 PASS**, GCC 15.2.0, Debug, warnings as errors.
- ASan/UBSan CTest: **9/9 PASS**, MSYS2 Clang 20.1.8, Debug, both sanitizers
  enabled and configured to halt on error.
- Existing Python unittest suite: **55/55 PASS**.
- BlackPill Debug cross-build: **PASS**, GNU Arm 14.2.1; linker usage RAM
  10,464 B, FLASH 41,484 B. No flashing or physical verification.
- Initial Plant cross-build not required: shared firmware sources and Plant sources/build
  configuration are unchanged. The common host CMake only gained DUT test targets.

Commands from the repository root (installed Ninja/compiler paths shown):

```powershell
cmake -S firmware/common/hil_protocol -B build/host-7a -G Ninja -DCMAKE_C_COMPILER=C:/msys64/ucrt64/bin/gcc.exe -DCMAKE_BUILD_TYPE=Debug
cmake --build build/host-7a
ctest --test-dir build/host-7a --output-on-failure
$env:PATH = 'C:\msys64\clang64\bin;' + $env:PATH
cmake -S firmware/common/hil_protocol -B build/host-7a-sanitize -G Ninja -DCMAKE_C_COMPILER=C:/msys64/clang64/bin/clang.exe -DCMAKE_BUILD_TYPE=Debug '-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer' '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined'
cmake --build build/host-7a-sanitize
$env:ASAN_OPTIONS = 'halt_on_error=1'
$env:UBSAN_OPTIONS = 'halt_on_error=1:print_stacktrace=1'
ctest --test-dir build/host-7a-sanitize --output-on-failure
python -B -m unittest discover -s test_automation/tests -v
cmake -S firmware/dut_stm32f411 -B build/dut-7a -G Ninja -DCMAKE_TOOLCHAIN_FILE=D:/Projects/embedded/embedded-hil-ai/firmware/dut_stm32f411/cmake/gcc-arm-none-eabi.cmake -DCMAKE_BUILD_TYPE=Debug
cmake --build build/dut-7a
```

### Targeted remediation after independent audit

All three independent findings were reproduced before production fixes, using
`test_dut_runtime.exe audit-a`, `audit-b`, and `audit-c`. Their new assertions
failed as expected: no fault after a 1000 ms sample then service at 5101 ms with
callbacks suppressed; zero STATUS responses after boot/heartbeat filled TX;
and old feedback becoming valid after observed expiry and a full tick cycle.
The same commands pass after correction.

The timer and expiry fixes are DUT-local. STATUS retry is in the shared app
because it consumes STATUS_REQUEST before board-specific dispatch. One pending
current STATUS snapshot is retained, retried ahead of periodic heartbeat, and
cleared only on successful enqueue; repeated polls while blocked coalesce.
Payloads use the current state at enqueue time, with a sequence allocated only
on successful enqueue. Existing queued frames retain FIFO ordering. DUT gates
boot/status/heartbeat submission on configured CDC plus class-data availability.
RX continues while unavailable. This does not imply the PC application is reading:
the configured-but-blocked-host case is separately tested. Plant defaults to ready
and needs no board-source change. Full USB reconnect recovery is still absent.

Fresh remediation results: focused A/B/C **PASS**, native **9/9 PASS** (GCC 15.2),
ASan/UBSan **9/9 PASS** (Clang 20.1.8), Python **55/55 PASS**, BlackPill Debug
**PASS** (RAM 10,472 B / FLASH 41,932 B), Plant Debug **PASS** (RAM 3,544 B /
FLASH 33,580 B), both GNU Arm 14.2.1. No hardware verification or flashing was
performed during those software checks; later owner-reported bench evidence
is linked above.
The full suite includes shared pending-STATUS tests for both DUT and Plant,
missing first/later events, a late callback before foreground service, jitter
within 10 ms, wrap-crossing deadlines, persistent sample expiry, and valid
acquisition after reset. C's test explicitly checks the cleared qualification
field so a simultaneous timing fault cannot mask failure to clear it.

Commands executed against the existing native/sanitizer/DUT build configurations
above; Plant used a separate build tree to preserve the owner's IDE build:

```powershell
cmake --build build/host-7a
& build/host-7a/test_dut_runtime.exe audit-a
& build/host-7a/test_dut_runtime.exe audit-b
& build/host-7a/test_dut_runtime.exe audit-c
ctest --test-dir build/host-7a --output-on-failure
$env:PATH = 'C:\msys64\clang64\bin;' + $env:PATH
cmake --build build/host-7a-sanitize
$env:ASAN_OPTIONS = 'halt_on_error=1'
$env:UBSAN_OPTIONS = 'halt_on_error=1:print_stacktrace=1'
ctest --test-dir build/host-7a-sanitize --output-on-failure
python -B -m unittest discover -s test_automation/tests -v
cmake --build build/dut-7a
cmake -S firmware/plant_simulator_stm32f103 -B build/plant-7a-remediation -G Ninja -DCMAKE_TOOLCHAIN_FILE=D:/Projects/embedded/embedded-hil-ai/firmware/plant_simulator_stm32f103/cmake/gcc-arm-none-eabi.cmake -DCMAKE_BUILD_TYPE=Debug
cmake --build build/plant-7a-remediation
git diff --check
```

The runtime host test uses real shared protocol queues/service and a minimal
HAL stub. It covers acquisition failures/retry, Plant-valid qualification/loss,
stale feedback, tick wrap, STATUS payload, overlap before/during acquisition,
execution budget, timer start failure, and acquisition with blocked TX. It does
not verify USB hardware, actual HAL/register timing or ADC accuracy. See the
[7A bench procedure](../test_automation/tools/README.md#dut-7a-safe-sensing-bench-check).
Calibration/thresholds, startup diagnostic coordination and running-test fault
policies remain decisions for subsequent increments.

### Final automated validation on November 12, 2026

These checks were actually rerun against the local working tree for manual
commit review, using the existing standalone CMake build configurations.
All builds below were clean rebuilds; no board was flashed or physically
retested during this finalization. These results do not identify the binary
used for the owner's earlier physical measurements.

| Check | Result |
|---|---|
| Native Debug CTest, GCC 15.2.0 | **PASS: 9/9** |
| ASan/UBSan Debug CTest, Clang 20.1.8, halt on error | **PASS: 9/9**, no sanitizer diagnostics |
| Python 3.12.6 unittest suite | **PASS: 55/55** |
| STM32F411 DUT Debug, GNU Arm 14.2.1 | **PASS**, RAM 10,472 B / FLASH 41,932 B |
| STM32F103 Plant Debug, GNU Arm 14.2.1 | **PASS**, RAM 3,544 B / FLASH 33,580 B |

Exact validation commands from the repository root (each build succeeded
before its dependent CTest run):

```powershell
cmake --build build/host-7a --clean-first
ctest --test-dir build/host-7a --output-on-failure

$env:PATH = 'C:\msys64\clang64\bin;' + $env:PATH
cmake --build build/host-7a-sanitize --clean-first
$env:ASAN_OPTIONS = 'halt_on_error=1'
$env:UBSAN_OPTIONS = 'halt_on_error=1:print_stacktrace=1'
ctest --test-dir build/host-7a-sanitize --output-on-failure

python -B -m unittest discover -s test_automation/tests -v
cmake --build build/dut-7a --clean-first
cmake --build build/plant-7a-remediation --clean-first
```

The existing sanitizer cache enables
`-fsanitize=address,undefined -fno-omit-frame-pointer` for compilation and
`-fsanitize=address,undefined` for linking. All commands returned exit code 0.
Python argument-validation tests intentionally print rejected-argument messages;
these were expected test output, and the suite ended with `OK`.

## Historical measurements and build sizes

Earlier milestone notes contain image sizes, RAM totals, test counts, and board observations from intermediate snapshots. The M6A sizes above are retained as historical build outputs, not presented as measurements from the dynamic experiment. Future captures should record the firmware revision/ELF hash, toolchain, command, date, and measurement conditions alongside their results.
