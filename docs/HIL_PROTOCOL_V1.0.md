# HIL PC Communication Protocol — Version 1.0

> **Status:** Normative Protocol V1.0 wire specification. Implementation coverage and verification evidence are tracked in [milestone status](MILESTONE_STATUS.md).
> **Date:** 2026-11-10
> **Scope:** PC ↔ BlackPill DUT and PC ↔ BluePill plant simulator.

## 1. Design goals

- Use the same application protocol over both PC serial transports.
- Keep framing, parsing and payloads deterministic and easy to implement in C and Python.
- Detect corrupted frames, missing telemetry and duplicate commands.
- Keep the PC outside the real-time DUT ↔ plant feedback loop.
- Avoid blocking firmware tasks on communication.
- Keep the protocol small; maximum payload is 64 bytes.

## 2. Transports

| Endpoint | Physical link | PC interface | Settings |
|---|---|---|---|
| BlackPill DUT (STM32F411) | USB Full-Speed device, USB CDC | Virtual COM port | CDC line coding may be set to 115200, 8-N-1 for host-tool consistency; this is not a physical USB baud rate |
| BluePill plant (STM32F103) | USART1 through FT232RL | Virtual COM port | 115200 baud, 8 data bits, no parity, 1 stop bit, no flow control |

USB CDC and UART carry identical application frames. The PC maintains an independent sequence-number space and command state for each board connection.

## 3. Frame format

All packets use this byte layout:

| Field | Size | Definition |
|---|---:|---|
| SOF | 2 bytes | `A5 5A` |
| Version | 1 byte | `0x01` for protocol V1.0 |
| Message type | 1 byte | See message ID table |
| Sequence | 2 bytes | Transmitter's 16-bit sequence number |
| Payload length | 2 bytes | Number of payload bytes, 0–64 |
| Payload | 0–64 bytes | Message-specific data |
| CRC | 2 bytes | CRC-16/CCITT-FALSE, low byte transmitted first |

Maximum frame size: `2 + 6 + 64 + 2 = 74 bytes`.

### 3.1 Encoding rules

- All multi-byte integers are little-endian.
- Temperatures use signed `int32` millidegrees Celsius (`m°C`); e.g. 22 °C = 22000 and −5 °C = −5000.
- Time values use unsigned milliseconds unless otherwise specified.
- ADC readings and DAC codes use unsigned integers.
- Boolean fields are one byte: `0 = false/LOW`, `1 = true/HIGH`.
- Do not transmit raw C structures. Serialize and parse fields explicitly; C structure padding and host endianness must not affect the wire format.
- Reserved bits and fields must be transmitted as zero by V1.0 implementations and ignored only where explicitly stated.

### 3.2 CRC

Use **CRC-16/CCITT-FALSE**:

| Parameter | Value |
|---|---|
| Width | 16 bits |
| Polynomial | `0x1021` |
| Initial value | `0xFFFF` |
| RefIn | False |
| RefOut | False |
| XorOut | `0x0000` |
| Check value for ASCII `123456789` | `0x29B1` |

CRC covers `Version` through the last payload byte. It excludes SOF and the CRC bytes themselves. Append the resulting CRC low byte first, then high byte.

### 3.3 Stream parser rules

USB CDC and UART are byte streams. A read may return a partial frame, one frame, or multiple concatenated frames.

1. Search for SOF bytes `A5 5A`.
2. Collect the fixed six-byte header following SOF.
3. Reject unsupported protocol versions and payload lengths greater than 64 before allocating/collecting the payload.
4. Collect exactly the declared payload length and two CRC bytes.
5. Validate CRC before dispatching the message.
6. On an invalid frame, discard the candidate and resynchronise by searching the buffered stream for the next SOF. Do not treat an SOF-like byte sequence inside a correctly length-delimited payload as a new frame.
7. Unknown message IDs with otherwise valid framing/CRC receive NACK error code 1 when a response is meaningful.

No byte escaping or COBS/SLIP encoding is used in V1.0 because the explicit payload length determines frame boundaries.

## 4. Sequence numbers and command reliability

### 4.1 Sequence numbering

- Each transmitter maintains its own independent 16-bit TX sequence counter per link/endpoint.
- Initialise the counter to zero after the transmitter application starts; increment for every transmitted frame, including telemetry and responses; wrap naturally from 65535 to 0.
- The PC maintains separate TX sequence counters for the BlackPill and BluePill connections.
- Receivers track sequence gaps for diagnostics; a gap in telemetry indicates one or more frames may have been lost. Sequence continuity is not used as a reason to discard an otherwise valid command.

### 4.2 Stop-and-wait commands

- The PC sends at most one command awaiting a response per board connection.
- Command ACK timeout: 500 ms.
- Maximum transmissions: 3 total (the original plus up to 2 retries).
- Retries retransmit the exact same frame with the same sequence number and payload.
- If no response is received after the third transmission, the PC treats the command outcome as unknown and requests STATUS before issuing another state-changing command.
- A board remembers the most recently accepted command and its response. If it receives an identical retransmission (same sequence and same command contents), it sends the same logical response again, using a fresh board-to-PC TX sequence number, without executing the command again.
- If a sequence number matches the remembered command but the contents differ, reject it with NACK error code 9 (`SEQUENCE_CONFLICT`).
- Commands must be idempotent with respect to retries: duplicate CONFIG_SET must not restart a test; duplicate START_TEST must not reset the run timer; duplicate STOP_TEST must not repeat unsafe side effects.

A receiver does not require strictly contiguous command sequence numbers. This permits a PC process to reconnect and establish a new command sequence after querying board status. Sequence numbers still identify duplicates and reveal telemetry gaps.

## 5. Message IDs

| ID | Message | Direction | Purpose |
|---:|---|---|---|
| `0x01` | BOOT_REPORT | Board → PC | Board identity, firmware version and reset cause after boot/transport readiness |
| `0x02` | STATUS | Board → PC | Current state and health snapshot |
| `0x03` | HEARTBEAT | Board → PC | Periodic liveness indication |
| `0x04` | STATUS_REQUEST | PC → Board | Request a current STATUS snapshot |
| `0x10` | CONFIG_SET | PC → Board | Configure the addressed board for a test |
| `0x11` | ARM_TEST | PC → Board | Validate readiness and arm the specified test |
| `0x12` | START_TEST | PC → Board | Start the addressed board's local run |
| `0x13` | STOP_TEST | PC → Board | Stop the addressed board safely |
| `0x20` | ACK | Board → PC | Confirm command acceptance/application |
| `0x21` | NACK | Board → PC | Reject a command with an error code |
| `0x22` | ARMED | Board → PC | Report that arm-time checks passed |
| `0x30` | TELEMETRY | Board → PC | Periodic DUT or plant measurements |
| `0x31` | FAULT_REPORT | Board → PC | Report a latched fault |
| `0x32` | TEST_COMPLETE | Board → PC | Report normal, host-stopped or fault-aborted completion |

Role identifiers:

| Value | Role |
|---:|---|
| `1` | DUT — STM32F411 BlackPill |
| `2` | Plant — STM32F103 BluePill |

## 6. Payload definitions

All payload sizes below exclude the 10-byte framing overhead (SOF + fixed header + CRC). Fields are ordered exactly as shown.

### 6.1 BOOT_REPORT — 12 bytes

| Field | Type | Meaning |
|---|---|---|
| role | `u8` | 1 = DUT, 2 = Plant |
| fw_major | `u8` | Firmware major version |
| fw_minor | `u8` | Firmware minor version |
| fw_patch | `u8` | Firmware patch version |
| reset_cause | `u16` | MCU-specific reset-cause bitmask; bit meanings documented by each firmware target |
| capabilities | `u16` | Capability bitmask below |
| uptime_ms | `u32` | Milliseconds since reset |

Capabilities bitmask:

| Bit | Meaning |
|---:|---|
| 0 | USB CDC transport available |
| 1 | UART transport available |
| 2 | ADC available |
| 3 | I2C/DAC available |
| 4 | TIM2 periodic time base available |
| 5 | HIL GPIO interface available |
| 6–15 | Reserved, zero in V1.0 |

A board sends BOOT_REPORT once after its transport is ready. If the PC connects later, STATUS_REQUEST obtains the current state; the board need not replay BOOT_REPORT.

### 6.2 STATUS_REQUEST — 0 bytes

No payload. The board responds with a STATUS message. A separate ACK is not required.

### 6.3 STATUS — 14 bytes

| Field | Type | Meaning |
|---|---|---|
| role | `u8` | Board role |
| state | `u8` | Firmware state enum (Section 7) |
| fault_code | `u16` | 0 if no latched fault |
| test_id | `u32` | Current/last test identifier; 0 if none |
| uptime_ms | `u32` | Milliseconds since reset |
| status_flags | `u16` | Current role-specific status flags |

`status_flags`:

| Bit | DUT interpretation | Plant interpretation |
|---:|---|---|
| 0 | PLANT_VALID input observed HIGH | PLANT_VALID output currently HIGH |
| 1 | HEATER_CMD output currently HIGH | HEATER_CMD input observed HIGH |
| 2 | ADC feedback path valid | Reserved, zero |
| 3 | Reserved, zero | I2C/DAC path valid |
| 4 | Timing fault latched | Timing fault latched |
| 5 | Communication TX fault/overflow latched | Communication TX fault/overflow latched |
| 6–15 | Reserved, zero | Reserved, zero |

### 6.4 HEARTBEAT — 10 bytes

Sent by each board every 1000 ms while the transport is available.

| Field | Type |
|---|---|
| role | `u8` |
| state | `u8` |
| test_id | `u32` |
| uptime_ms | `u32` |

The PC considers a board connection stale after 3000 ms without receiving any valid frame from that board. HEARTBEAT is still sent during READY/ARMED/RUNNING/COMPLETE/FAULT where practical; it is not part of the real-time control loop.

### 6.5 CONFIG_SET — role-specific, 25 bytes DUT / 30–62 bytes Plant

Common prefix:

| Field | Type |
|---|---|
| target_role | `u8` |
| test_id | `u32` |

DUT-specific fields:

| Field | Type | Units / constraint |
|---|---|---|
| setpoint_mC | `i32` | m°C |
| heat_on_threshold_mC | `i32` | m°C; must be below OFF threshold |
| heat_off_threshold_mC | `i32` | m°C; must be above ON threshold |
| control_period_ms | `u16` | V1.0 requires 1000 |
| telemetry_period_ms | `u16` | V1.0 default 1000 |
| max_runtime_ms | `u32` | Local safety upper bound; must be non-zero and greater than expected run duration plus start/stop margin |

Total DUT payload: 25 bytes.

Plant-specific fields:

| Field | Type | Units / constraint |
|---|---|---|
| initial_temp_mC | `i32` | m°C; within −20000…60000 |
| initial_ambient_mC | `i32` | m°C; validate against configured model envelope |
| tau_s | `u32` | Seconds; non-zero |
| heater_gain_mC_per_s | `i32` | m°C/s |
| model_step_ms | `u16` | V1.0 requires 5000 |
| telemetry_period_ms | `u16` | V1.0 default 5000 |
| max_runtime_ms | `u32` | Local safety upper bound, not the host's nominal test duration |
| disturbance_count | `u8` | 0…4 |

Each disturbance record is 8 bytes:

| Field | Type | Units |
|---|---|---|
| offset_ms | `u32` | Milliseconds from the plant's local START_TEST acceptance |
| new_ambient_temp_mC | `i32` | m°C |

Disturbances are sorted by increasing offset and applied by the plant model at the scheduled step. Maximum plant payload is 62 bytes (30-byte fixed portion + four 8-byte records).

All configuration fields must be range-checked by the receiving firmware. Unknown or unsupported settings are rejected; do not silently clamp invalid configuration except where the temperature-to-DAC conversion explicitly specifies code clamping.

**Duration clarification:** the PC controls the nominal experiment duration using its monotonic clock and sends STOP_TEST to end a normal run. `max_runtime_ms` is an independent local fail-safe upper bound on each MCU, configured longer than the intended experiment to allow start skew and orderly stopping. It prevents a board from running indefinitely if the PC disappears. It is not intended to make both boards terminate at exactly the same local elapsed time.

### 6.6 ACK / NACK — 6 bytes

| Field | Type | Meaning |
|---|---|---|
| acknowledged_sequence | `u16` | Sequence number of the command being answered |
| acknowledged_message_type | `u8` | Original command ID |
| responding_role | `u8` | Board role |
| error_code | `u16` | 0 for ACK; non-zero for NACK |

NACK error codes:

| Code | Name | Meaning |
|---:|---|---|
| 1 | UNSUPPORTED_MESSAGE | Unknown or unsupported message ID |
| 2 | WRONG_TARGET_ROLE | Command addressed to another board role |
| 3 | INVALID_STATE | Command not permitted in the current state |
| 4 | INVALID_PAYLOAD_LENGTH | Incorrect payload size |
| 5 | INVALID_CONFIGURATION | One or more configuration values are invalid |
| 6 | TEST_ID_MISMATCH | Command test ID does not match the active configuration |
| 7 | HARDWARE_NOT_READY | Required peripheral or feedback path is not ready |
| 8 | UNSUPPORTED_VERSION | Protocol version not supported |
| 9 | SEQUENCE_CONFLICT | Same recent sequence number received with different command contents |

For CONFIG_SET, ACK means the complete configuration has passed validation and has been stored. For START_TEST, ACK means the board has entered RUNNING and started its local runtime timer. For STOP_TEST, ACK means the local safe stop has been applied. ARM_TEST success returns ACK followed by ARMED; an arm-time failure returns NACK and no ARMED.

### 6.7 ARM_TEST — 5 bytes

| Field | Type |
|---|---|
| target_role | `u8` |
| test_id | `u32` |

The board validates the test ID, current state, configuration and role-specific readiness. On success it sends ACK and then ARMED. On failure it sends NACK and remains unarmed.

### 6.8 ARMED — 7 bytes

| Field | Type |
|---|---|
| role | `u8` |
| test_id | `u32` |
| readiness_flags | `u16` |

Readiness flags:

| Bit | Meaning |
|---:|---|
| 0 | Configuration validated |
| 1 | Local peripheral initialization successful |
| 2 | Analogue feedback path verified: DAC path (Plant) / ADC feedback range check (DUT) |
| 3 | PLANT_VALID observed HIGH (DUT); plant-side bit reserved/zero |
| 4 | HEATER_CMD startup functional check passed |
| 5–15 | Reserved, zero |

### 6.9 START_TEST — 5 bytes

| Field | Type |
|---|---|
| target_role | `u8` |
| test_id | `u32` |

The board enters RUNNING and starts its local runtime timer when it accepts START_TEST. The PC sends START_TEST to the Plant first, waits for ACK, then sends START_TEST to the DUT. Exact simultaneous start is not required; the PC logs both start/ACK times using its monotonic clock.

### 6.10 STOP_TEST — 6 bytes

| Field | Type |
|---|---|
| target_role | `u8` |
| test_id | `u32` |
| stop_reason | `u8` |

Stop reasons:

| Value | Meaning |
|---:|---|
| 0 | Normal host-controlled test completion |
| 1 | User-requested abort |
| 2 | Host safety abort / incomplete run |

For a normal host-controlled stop, the PC stops the DUT first and waits for ACK (DUT drives HEATER_CMD LOW), then stops the plant (plant drives PLANT_VALID LOW). A duplicate STOP_TEST is idempotent. On local max-runtime expiry, the board latches a fault and applies its safe state.

### 6.11 TELEMETRY — role-specific

Telemetry frames do not require per-frame ACK. `sample_index` increments for every scheduled sample, even if a frame is dropped before transmission, so the PC can detect missing samples through index gaps.

DUT TELEMETRY — 22 bytes:

| Field | Type | Units / meaning |
|---|---|---|
| role | `u8` | 1 |
| test_id | `u32` | Active test |
| sample_index | `u32` | Incrementing telemetry sample index |
| uptime_ms | `u32` | MCU uptime |
| adc_raw | `u16` | 12-bit ADC result stored in 16-bit field |
| measured_temp_mC | `i32` | Converted temperature in m°C |
| heater_cmd | `u8` | 0 LOW/OFF, 1 HIGH/ON |
| controller_state | `u8` | 0 heater OFF, 1 heater ON, 2 control inhibited |
| plant_valid | `u8` | PLANT_VALID input, 0/1 |

Plant TELEMETRY — 25 bytes:

| Field | Type | Units / meaning |
|---|---|---|
| role | `u8` | 2 |
| test_id | `u32` | Active test |
| sample_index | `u32` | Incrementing telemetry sample index |
| uptime_ms | `u32` | MCU uptime |
| simulated_temp_mC | `i32` | Plant model temperature in m°C |
| ambient_temp_mC | `i32` | Current ambient temperature in m°C |
| heater_input | `u8` | PB0 observed, 0/1 |
| dac_code | `u16` | Last successfully commanded DAC code |
| plant_valid | `u8` | PLANT_VALID output, 0/1 |

Default telemetry periods: DUT 1000 ms; Plant 5000 ms. Heartbeats remain 1000 ms on both links. Telemetry transmission must not block periodic control/model work. Use a bounded TX queue; if it is full, drop the telemetry sample and expose the gap through `sample_index`. FAULT_REPORT and TEST_COMPLETE have priority over ordinary telemetry.

### 6.12 FAULT_REPORT — 13 bytes

| Field | Type | Meaning |
|---|---|---|
| role | `u8` | Board role |
| test_id | `u32` | Active/last test ID |
| fault_code | `u16` | Fault ID |
| fault_detail | `u16` | Additional role-specific detail |
| uptime_ms | `u32` | Time of fault |

Initial fault codes:

| Code | Fault |
|---:|---|
| 1 | Invalid configuration |
| 2 | I2C/DAC communication failure (Plant) |
| 3 | ADC/sensor input failure (DUT) |
| 4 | PLANT_VALID lost (DUT) |
| 5 | Periodic task deadline/missed event |
| 6 | Internal peripheral/application error |
| 7 | Local maximum runtime exceeded |

A fault is latched. The board applies its safe state and attempts to transmit FAULT_REPORT. The PC may then stop the other board. A missing FAULT_REPORT must be detectable through heartbeat/status and incomplete test records.

### 6.13 TEST_COMPLETE — 10 bytes

| Field | Type | Meaning |
|---|---|---|
| role | `u8` | Board role |
| test_id | `u32` | Test identifier |
| outcome | `u8` | 0 normal, 1 host stop/abort, 2 fault-aborted |
| elapsed_ms | `u32` | Local elapsed run time |

A board in FAULT remains in FAULT after reporting TEST_COMPLETE with outcome 2. Normal STOP_TEST enters COMPLETE.

## 7. Board states and command legality

State values used in STATUS:

| Value | State |
|---:|---|
| 0 | BOOT |
| 1 | INITIALIZING |
| 2 | WAIT_CONFIG |
| 3 | READY |
| 4 | ARMED |
| 5 | RUNNING |
| 6 | FAULT |
| 7 | COMPLETE |

High-level path:

```text
BOOT -> INITIALIZING -> WAIT_CONFIG -> READY -> ARMED -> RUNNING -> COMPLETE
                                  \                         /
                                   ----------> FAULT <------
```

- CONFIG_SET: accepted in WAIT_CONFIG or READY before arming; a new configuration for a different test ID is not accepted while ARMED/RUNNING.
- ARM_TEST: accepted in READY for the configured test ID.
- START_TEST: accepted in ARMED for the same test ID.
- STOP_TEST: accepted in RUNNING; repeated stop for the same completed test is idempotently acknowledged.
- STATUS_REQUEST: accepted in every state.
- Invalid state/role/ID produces NACK without silently changing state.

FAULT is latched. A reset/reinitialization and a new test sequence are required; no automatic resume of interrupted plant or DUT state.

## 8. Test lifecycle and ordering

1. PC opens both COM ports and sends STATUS_REQUEST to each board.
2. Boards report current STATUS; BOOT_REPORT is emitted after each boot when its transport becomes ready.
3. PC sends CONFIG_SET independently to Plant and DUT; both validate and ACK/NACK.
4. Plant initializes I2C, writes the configured initial temperature to the MCP4725 volatile DAC register, and reads the DAC register back where supported. This confirms the digital register contents, not the analogue voltage. Plant raises PLANT_VALID only after its local I2C/DAC output initialization is successful.
5. DUT verifies PLANT_VALID and checks the initial ADC signal against its configured allowed range.
6. Perform the controlled HEATER_CMD LOW→HIGH→LOW interface test. Plant reports the PB0 state it actually observed; PC compares it with DUT telemetry/status. This is a startup diagnostic, not thermostat operation.
7. Both boards report READY after their checks pass.
8. PC sends ARM_TEST to both and receives ACK + ARMED from each.
9. PC sends START_TEST to Plant, waits for ACK, then sends START_TEST to DUT. Record both start times on the PC's monotonic clock; no MCU clock synchronisation is claimed.
10. PC controls the intended experiment duration using its monotonic clock. Both MCUs independently run their local periodic tasks and max-runtime guard. They send heartbeats and telemetry.
11. For normal completion or host abort, stop DUT first and wait for ACK, then stop Plant and wait for ACK. This ensures HEATER_CMD is LOW before PLANT_VALID is deasserted.
12. Save all received frames and report PASS/FAIL/INCOMPLETE based on deterministic test assertions and completeness of required logs.

If the PC connection disappears during an already-started bounded test, firmware may continue locally until a STOP_TEST is received or its configured max_runtime_ms expires. Missing required telemetry makes the host-side result INCOMPLETE, not PASS. The PC is not in the real-time loop.

## 9. Protocol validation vectors and implementation checks

The cases in this section define conformance expectations for implementations. They are not a claim that every endpoint behavior has been implemented or physically verified. See [milestone status](MILESTONE_STATUS.md) for the currently recorded software, host-test, cross-build, flashing, and hardware evidence.

Before integrating both boards, implement host/firmware tests for:

- CRC check string `123456789` → `0x29B1` (wire order `B1 29`).
- Empty-payload STATUS_REQUEST frame encode/decode.
- Minimum and maximum payload lengths (0 and 64).
- Payload containing bytes `A5 5A` without parser resynchronisation inside the payload.
- Partial frame arrival across multiple reads.
- Multiple concatenated frames in one read.
- Invalid SOF, unsupported version, payload length >64 and bad CRC.
- Sequence wraparound 65535 → 0.
- Duplicate command retry: response repeated, state-changing action applied only once.
- Same recent sequence number with changed command contents → NACK 9.
- Telemetry sample-index gap detection.
- Lost heartbeat timeout after 3000 ms.
- Board fault and local max-runtime handling.

## 10. Protocol design boundaries

- No PC command is required to execute a 1-second DUT or 5-second plant cycle.
- No per-cycle handshake between MCUs.
- No telemetry ACK per sample.
- No raw C structure transmission.
- No floating-point wire values in V1.0.
- No protocol command can override the firmware's electrical, ADC range, runtime, state-machine or fault interlocks.
- AI services, when introduced in Stage 4, use the PC test framework. They do not send arbitrary raw protocol frames or bypass deterministic validation.
