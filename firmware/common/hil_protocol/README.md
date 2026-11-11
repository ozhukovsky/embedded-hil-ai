# Shared HIL protocol module

This directory contains the portable Protocol V1.0 framing, payload codecs, reliability primitives, shared transport queues, application service, and Plant model code. It is compiled directly into the two STM32 firmware executables; the standalone CMake project here is for host testing and must not be added as a subdirectory of an embedded target (it also defines host test executables).

## Source modules

- `src/hil_protocol.c` — fixed-memory frame encoding, CRC-16/CCITT-FALSE, incremental stream parser.
- `src/hil_protocol_messages.c` — explicit payload serialization, deserialization, and validation.
- `src/hil_protocol_reliability.c` — per-link TX sequence, RX sequence diagnostics, and accepted-command/response cache.
- `src/hil_transport.c` — hardware-independent bounded RX/TX queues, frame parser service, priority selection, and overflow accounting.
- `src/hil_protocol_app.c` — shared foreground protocol application service, BOOT_REPORT, STATUS handling, heartbeat scheduling, and common frame processing.
- `src/hil_plant_model.c` — portable thermal plant model and temperature-to-DAC mapping used by the Plant target.
- `src/hil_plant_runtime.c` — portable heater-edge diagnostic state and wrap-safe runtime-deadline helper used by the Plant target.
- `include/` — public declarations for these modules.

Board-specific peripheral adapters and application/runtime integration remain in each STM32 project. USB CDC, USART/HAL callbacks, ADC/I2C device access, board GPIO, timers, and board state transitions are not implemented by this hardware-independent module.

## Native tests

Requires CMake 3.16+ and a C99 compiler. From the repository root:

```sh
cmake -S firmware/common/hil_protocol -B build/host
cmake --build build/host
ctest --test-dir build/host --output-on-failure
```

The standalone project enables warnings as errors and defines seven CTest executables covering framing, message codecs, reliability, transport, application service, the Plant model, and portable Plant runtime helpers. Use a separate build tree with sanitizer compiler/linker flags when running ASan/UBSan; see the repository milestone status for recorded historical results. Selected physical Plant observations are separate evidence and do not replace these host checks.

## STM32 integration

Both firmware CMake files list the shared `.c` files directly with paths based on `CMAKE_CURRENT_LIST_DIR` and add this module's `include` directory. The BlackPill includes framing, messages, reliability, transport, and app service. The BluePill includes those modules plus the Plant model and runtime helpers. Keep each shared source listed once per target. The standalone CMake configuration is retained for host builds and tests.

For current project-level implementation and verification status, see [milestone status](../../../docs/MILESTONE_STATUS.md). The normative wire format is in [Protocol V1.0](../../../docs/HIL_PROTOCOL_V1.0.md).
