# Embedded HIL Test Bench with AI-Assisted Validation

This learning-oriented engineering project explores hardware-in-the-loop (HIL) testing of embedded firmware and how AI can assist with test generation, execution, and failure analysis. It combines two STM32 boards, a simulated thermal plant, hardware signal interfaces, and PC-based test tools.

## System

- **STM32F411 BlackPill:** Device Under Test (DUT), intended to run a thermal-control controller.
- **STM32F103 BluePill:** Plant simulator with a discrete thermal model.
- **MCP4725 DAC:** Converts the Plant's temperature model output to an analogue signal for the DUT ADC.
- **PC:** Python-based protocol tools and test orchestration. The PC is outside the MCU feedback loop.
- **Links:** USB CDC to the DUT; USART1 through an FT232RL adapter to the Plant.

The project is being developed incrementally. The Plant and DUT exchange persistent GPIO signals; the PC configures and observes each board over its own serial link.

## Learning objectives

- Build and verify a two-microcontroller closed-loop HIL system.
- Develop repeatable embedded test automation, including fault-oriented scenarios and result collection.
- Explore AI-assisted test generation and failure analysis after establishing deterministic baseline tests.
- Compare assisted and conventional validation workflows using repeatable scenarios and measurable evidence.

## Current status

Status below separates code and automated evidence from bench evidence:

- **Software:** Milestones 1-5 are implemented. Milestone 6 Plant simulator software is complete. Milestone 7 DUT implementation and Milestone 8 integrated HIL operation are not complete.
- **Automated tests:** The project owner reports Milestone 6 results of 6/6 native CTest tests, 6/6 ASan/UBSan CTest tests, and 8/8 Python host tests passing. These results were not rerun as part of the documentation update.
- **Cross-compilation:** The project owner reports both STM32 targets cross-built successfully after Milestone 6. This confirms compilation/linking as reported, not operation on hardware.
- **Flashing and physical communication:** USB CDC on the BlackPill and USART1/FT232RL on the BluePill were physically verified with the Milestone 5 firmware. The latest Milestone 6 BluePill image has not been flashed or physically verified; the earlier Milestone 5 result does not verify that newer image.
- **Open hardware checks:** MCP4725 analogue output measurement, reliable heater-interface LOW-to-HIGH-to-LOW verification, and measurement of the Plant's 100 ms execution budget remain pending.

See [milestone status](docs/MILESTONE_STATUS.md) for the evidence by increment and remaining work.

## Documentation

- [Architecture and wiring](docs/HIL_ARCHITECTURE_DIAGRAMS.md)
- [Protocol V1.0 specification](docs/HIL_PROTOCOL_V1.0.md)
- [Milestone status and evidence](docs/MILESTONE_STATUS.md)
- [Shared protocol module](firmware/common/hil_protocol/README.md)
- [Plant runtime](firmware/plant_simulator_stm32f103/PLANT_RUNTIME.md)
- [PC tools](test_automation/tools/README.md)

## Build and test

The shared module's standalone CMake project builds the portable code and six native test executables. From the repository root:

```sh
cmake -S firmware/common/hil_protocol -B build/host
cmake --build build/host
ctest --test-dir build/host --output-on-failure
```

The Python host regression suite uses `unittest` and requires Python 3.9+ and `pyserial`:

```powershell
python -m pip install pyserial
python -m unittest discover -s test_automation/tests -v
```

Each firmware target has a CMake project and GNU Arm Embedded toolchain file. With `arm-none-eabi-gcc` and the related tools on `PATH`, configure/build each target from the repository root:

```sh
cmake -S firmware/dut_stm32f411 -B build/dut \
  -DCMAKE_TOOLCHAIN_FILE=firmware/dut_stm32f411/cmake/gcc-arm-none-eabi.cmake
cmake --build build/dut

cmake -S firmware/plant_simulator_stm32f103 -B build/plant \
  -DCMAKE_TOOLCHAIN_FILE=firmware/plant_simulator_stm32f103/cmake/gcc-arm-none-eabi.cmake
cmake --build build/plant
```

## Development direction and limitations

The next work is Milestone 7 (DUT controller implementation), followed by Milestone 8 (integrated HIL operation). The current milestone does not establish DAC voltage accuracy, complete heater-interface behavior, target timing margin, or latest-image physical communication. Initial test scenarios include normal regulation, invalid sensor input, sensor drift/noise, stuck heater output, communication failures, and controller reset/recovery.

This repository explores AI-assisted validation; it does not yet claim completed AI-based test generation or analysis. No project license is declared here. Check the repository and vendor attribution files before reuse or redistribution.
