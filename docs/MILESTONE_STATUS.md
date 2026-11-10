# Milestone status and verification evidence

This page is the project's public status summary. It distinguishes source implementation, automated checks, cross-compilation, flashing, and bench observations. A PASS in one category does not imply a PASS in another. Historical checks are reported as recorded; the Milestone 6 results below were supplied by the project owner and were not rerun for this documentation update.

## Summary

| Milestone | Software implementation | Native tests / sanitizers | Cross-compilation | Flashing / physical evidence | Remaining work |
|---|---|---|---|---|---|
| 1 — Protocol framing | Implemented: portable framing/parser core | Historical module status records codec/parser checks; no fresh run here | Included in later target builds | No separate M1 physical test claimed | None specific to framing milestone; broader protocol features are separate increments |
| 2 — STM32 project integration | Implemented: common modules integrated into both target CMake builds | Historical records only; not rerun here | Both targets subsequently cross-built | No separate M2 bench claim | See later milestones for endpoint and system evidence |
| 3A — Payload serialization and validation | Implemented | Historical module status records native and sanitizer checks; not rerun here | Included in later target builds | No separate physical claim | Plant model envelope validation was added with M6; full test lifecycle remains future work |
| 3B — Reliability primitives | Implemented: TX/RX sequence diagnostics and accepted-command/response cache | Historical module status records native and sanitizer checks; not rerun here | Included in later target builds | No separate physical claim | Full application behavior is outside this milestone |
| 4 — Hardware communication transport | Implemented: USB CDC and interrupt-driven USART transport adapters and bounded queues | Historical module status records native transport checks; not rerun here | Included in later target builds | Transport was physically exercised in M5, below | Latest M6 BluePill image still requires physical verification |
| 5 — Protocol application bring-up | Implemented: BOOT_REPORT, STATUS request/response, heartbeat and minimal dispatch | Historical module status records native app checks; not rerun here | Both targets later cross-built | M5 firmware USB CDC (BlackPill) and USART1/FT232RL (BluePill) communication were physically verified. This evidence applies to the M5 firmware tested then, not the later M6 BluePill image. | Full DUT/Plant test lifecycle was not part of M5 |
| 6 — Plant simulator | Plant software implementation complete | Owner-reported: native CTest 6/6; ASan/UBSan CTest 6/6; Python host tests 8/8 | Owner-reported: both STM32 targets cross-built after M6 | Latest M6 BluePill firmware has not been flashed or physically verified | Measure MCP4725 analogue output; verify heater-interface LOW→HIGH→LOW; measure Plant 100 ms execution budget on hardware |
| 7 — DUT firmware | Not implemented | Not applicable | No M7-specific build claim | Not verified | Implement and verify DUT controller behavior |
| 8 — Integrated HIL operation | Not implemented | Not applicable | No M8-specific build claim | Not verified | Integrate and verify complete end-to-end test operation |

## Evidence boundaries and open checks

- **Software implementation** means the relevant source and build integration exist. It does not establish that a particular binary is running on a board.
- **Native tests** exercise host builds. They do not prove MCU timing, peripheral electrical behavior, or wiring.
- **Cross-compilation** establishes a target image linked successfully. It is not flashing or physical verification.
- **M5 physical communication** was reported successful for the M5 firmware versions tested on each board. It must not be transferred as evidence for the later M6 BluePill firmware.
- **M6 results** (6/6 native CTest, 6/6 ASan/UBSan CTest, 8/8 Python host tests, and both target cross-builds) are project-owner-reported results. They were not rerun for this documentation change.
- **Hardware work still open:** latest M6 Plant firmware flash/bring-up; DAC analogue output measurement; repeatable heater LOW→HIGH→LOW diagnostic; measurement against the 100 ms Plant execution budget; then Milestones 7 and 8.

## Historical measurements and build sizes

Earlier milestone notes contain image sizes, RAM totals, test counts, and board observations from intermediate firmware snapshots. Those values describe the particular historical builds and setups recorded at the time. They are omitted from this current summary rather than presented as current image metrics. Preserve dated snapshots only when the firmware revision, toolchain, command, and measurement conditions are recorded alongside them.
