# Embedded HIL Test Bench with AI-Assisted Validation

**Status:** Architecture and initial design

## Overview

This project explores Hardware-in-the-Loop (HIL) testing of embedded firmware and investigates how AI can assist with test generation, execution and failure analysis.

The initial implementation will focus on a thermal-control system consisting of a real embedded controller and a simulated thermal environment.

The system will combine STM32-based firmware, hardware signal interfaces, a mathematical plant model and automated test execution. AI-assisted capabilities will be introduced after establishing the baseline testing workflow.

## Proposed Architecture

- **STM32F411 BlackPill:** Device Under Test (DUT), running the thermal-control firmware.
- **STM32F103 BluePill:** Plant simulator, implementing the thermal model.
- **MCP4725 DAC:** Generates an analog sensor signal based on the simulated temperature.
- **PC:** Test orchestration, fault injection, data collection and AI integration.
- **Debugging equipment:** Oscilloscope, logic analyzer and multimeter.

The DUT controls a simulated heating system. Its output affects the thermal model, which generates updated sensor feedback for the DUT, forming a closed-loop HIL setup.

## Development Roadmap

1. **HIL implementation:** Build and verify the two-microcontroller closed-loop system.
2. **Test automation:** Introduce Python-based test execution, fault injection and result collection.
3. **AI integration:** Experiment with AI-assisted test generation, failure diagnosis and test recommendations.
4. **Evaluation:** Compare AI-assisted workflows against conventional testing using repeatable test scenarios and measurable results.

## Initial Test Scenarios

- Normal temperature regulation.
- Sensor failure and invalid readings.
- Sensor drift and measurement noise.
- Heater stuck ON or OFF.
- Communication failures.
- Controller reset and recovery.

## Current Focus

Defining the hardware interfaces, simulation model, communication mechanisms and timing requirements for the initial HIL prototype.