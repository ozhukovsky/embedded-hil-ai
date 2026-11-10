#!/usr/bin/env python3
"""Event-driven, idealized HIL timing experiments.

This models:
- A first-order thermal plant integrated by Forward Euler every 5 s.
- A DAC output that holds the most recently published plant temperature.
- A hysteresis thermostat that samples that held temperature periodically.
- A plant GPIO read only at plant-update boundaries.

Coincident event order is explicitly defined:
  1. Plant integrates the preceding interval using the previously latched heater state.
  2. Plant publishes the new temperature (idealized DAC update and settle).
  3. Configuration changes at that timestamp are applied.
  4. The controller runs if one of its events is due.
  5. Plant samples the GPIO to latch the heater state for the next interval.

This is a software scheduling model, not proof that independent STM32 timers
will execute in this order. Hardware synchronization/handshake is not modeled.
"""

from __future__ import annotations

import argparse
import math
from dataclasses import dataclass
from typing import Dict, List, Tuple


@dataclass
class Config:
    tau_s: float = 300.0
    heating_rate_c_per_s: float = 0.02
    plant_period_s: float = 5.0
    controller_period_s: float = 1.0
    controller_phase_s: float = 0.0
    ambient_c: float = 18.0
    initial_temperature_c: float = 18.0
    setpoint_c: float = 22.0
    hysteresis_half_width_c: float = 0.5
    duration_s: float = 3600.0


def update_thermostat(temperature_c: float, setpoint_c: float,
                      heater_on: bool, half_width_c: float) -> bool:
    """Return the next heater state using a two-threshold thermostat."""
    lower = setpoint_c - half_width_c
    upper = setpoint_c + half_width_c
    if not heater_on and temperature_c <= lower:
        return True
    if heater_on and temperature_c >= upper:
        return False
    return heater_on


def simulate(config: Config,
             ambient_changes: Dict[float, float] | None = None,
             setpoint_changes: Dict[float, float] | None = None
             ) -> Tuple[List[Tuple[float, float, bool]], List[Tuple[float, bool, bool]]]:
    """Return plant samples (time, temperature, heater-for-next-step) and transitions."""
    ambient_changes = ambient_changes or {}
    setpoint_changes = setpoint_changes or {}

    # Use integer milliseconds for deterministic event ordering.
    def ms(seconds: float) -> int:
        value = seconds * 1000.0
        rounded = round(value)
        if not math.isclose(value, rounded, abs_tol=1e-7):
            raise ValueError("Periods, phases and event times must be whole milliseconds.")
        return int(rounded)

    plant_period_ms = ms(config.plant_period_s)
    controller_period_ms = ms(config.controller_period_s)
    controller_phase_ms = ms(config.controller_phase_s)
    duration_ms = ms(config.duration_s)

    if plant_period_ms <= 0 or controller_period_ms <= 0:
        raise ValueError("Plant and controller periods must be positive.")
    if not (0 <= controller_phase_ms < controller_period_ms):
        raise ValueError("Controller phase must be in [0, controller period).")
    if duration_ms % plant_period_ms != 0:
        raise ValueError("Duration must be an integer multiple of plant period.")

    amb_events = {ms(t): v for t, v in ambient_changes.items()}
    sp_events = {ms(t): v for t, v in setpoint_changes.items()}

    temperature = config.initial_temperature_c
    dac_temperature = temperature  # DAC initially represents the initial plant state.
    ambient = config.ambient_c
    setpoint = config.setpoint_c

    # Startup initialization: controller calculates a safe/initial command,
    # then the plant latches it for the first integration interval.
    heater_gpio = update_thermostat(
        dac_temperature, setpoint, False, config.hysteresis_half_width_c
    )
    heater_applied = heater_gpio

    plant_samples: List[Tuple[float, float, bool]] = [
        (0.0, temperature, heater_applied)
    ]
    transitions: List[Tuple[float, bool, bool]] = []

    next_plant_ms = plant_period_ms
    # A t=0 initialization sample is separate from the recurring controller schedule.
    next_controller_ms = (controller_phase_ms if controller_phase_ms > 0
                          else controller_period_ms)

    while min(next_plant_ms, next_controller_ms) <= duration_ms:
        now_ms = min(next_plant_ms, next_controller_ms)
        is_plant_event = now_ms == next_plant_ms
        is_controller_event = now_ms == next_controller_ms

        if is_plant_event:
            dt = config.plant_period_s
            u = 1.0 if heater_applied else 0.0
            temperature += dt * (
                (ambient - temperature) / config.tau_s
                + config.heating_rate_c_per_s * u
            )
            dac_temperature = temperature

        # Apply external configuration changes at the event time, after the
        # preceding plant interval has been integrated.
        if now_ms in amb_events:
            ambient = amb_events[now_ms]
        if now_ms in sp_events:
            setpoint = sp_events[now_ms]

        if is_controller_event:
            heater_gpio = update_thermostat(
                dac_temperature, setpoint, heater_gpio,
                config.hysteresis_half_width_c
            )
            next_controller_ms += controller_period_ms

        if is_plant_event:
            if heater_gpio != heater_applied:
                transitions.append((now_ms / 1000.0, heater_applied, heater_gpio))
            # GPIO sample is the input held during the NEXT plant interval.
            heater_applied = heater_gpio
            plant_samples.append((now_ms / 1000.0, temperature, heater_applied))
            next_plant_ms += plant_period_ms

    return plant_samples, transitions


def summarize(samples: List[Tuple[float, float, bool]],
              transitions: List[Tuple[float, bool, bool]],
              start_s: float) -> dict:
    window = [row for row in samples if row[0] >= start_s]
    temps = [row[1] for row in window]
    recent_transitions = [tr for tr in transitions if tr[0] >= start_s]
    on_starts = [t for t, old, new in recent_transitions if not old and new]
    cycles = [on_starts[i + 1] - on_starts[i] for i in range(len(on_starts) - 1)]
    return {
        "min_c": min(temps),
        "max_c": max(temps),
        "mean_c": sum(temps) / len(temps),
        "transitions": len(recent_transitions),
        "cycle_s": sum(cycles) / len(cycles) if cycles else None,
    }


def run_case(name: str, *, ambient_changes=None, setpoint_changes=None):
    cfg = Config()
    samples, transitions = simulate(cfg, ambient_changes, setpoint_changes)
    summary = summarize(samples, transitions, 2400.0)
    print(f"\n{name}")
    print(f"  final temperature: {samples[-1][1]:.6f} °C")
    print(f"  steady-window min/max/mean: "
          f"{summary['min_c']:.6f} / {summary['max_c']:.6f} / "
          f"{summary['mean_c']:.6f} °C")
    print(f"  heater transitions after 2400 s: {summary['transitions']}")
    if summary["cycle_s"] is not None:
        print(f"  mean heater cycle: {summary['cycle_s']:.3f} s")
    print("  transitions after 1200 s (first 6):",
          [x for x in transitions if x[0] >= 1200][:6])
    return samples, transitions


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plot", action="store_true", help="Plot the four scenarios (requires matplotlib).")
    args = parser.parse_args()

    # Controller/phase comparison under nominal conditions.
    print("Controller timing comparison (steady window: 1800..3600 s)")
    for period, phase in [(0.1, 0.0), (1.0, 0.0), (5.0, 0.0), (1.0, 0.5), (5.0, 0.5)]:
        cfg = Config(controller_period_s=period, controller_phase_s=phase)
        samples, transitions = simulate(cfg)
        s = summarize(samples, transitions, 1800.0)
        print(f"  controller={period:g} s, phase={phase:g} s: "
              f"min={s['min_c']:.4f} °C, max={s['max_c']:.4f} °C, "
              f"mean={s['mean_c']:.4f} °C, cycle={s['cycle_s']} s")

    nominal_samples, nominal_transitions = run_case("Nominal: ambient 18 °C, setpoint 22 °C")
    ambient_up_samples, ambient_up_transitions = run_case(
        "Ambient step: 18 -> 20 °C at 1200 s",
        ambient_changes={1200.0: 20.0})
    ambient_down_samples, ambient_down_transitions = run_case(
        "Ambient step: 18 -> 16 °C at 1200 s",
        ambient_changes={1200.0: 16.0})
    setpoint_samples, setpoint_transitions = run_case(
        "Setpoint step: 22 -> 23 °C at 1200 s",
        setpoint_changes={1200.0: 23.0})

    # Basic range assertions for the chosen scenarios.
    for name, samples in [
        ("nominal", nominal_samples),
        ("ambient_up", ambient_up_samples),
        ("ambient_down", ambient_down_samples),
        ("setpoint_up", setpoint_samples),
    ]:
        temps = [row[1] for row in samples]
        if min(temps) < -20.0 or max(temps) > 60.0:
            raise RuntimeError(f"{name}: plant temperature left [-20, 60] °C")
    print("\nRange checks: PASS (all tested plant temperatures stayed in [-20, 60] °C)")

    if args.plot:
        try:
            import matplotlib.pyplot as plt
        except ImportError as exc:
            raise SystemExit("Install matplotlib with: python -m pip install matplotlib") from exc
        for label, samples in [
            ("Nominal", nominal_samples),
            ("Ambient 18 -> 20 °C", ambient_up_samples),
            ("Ambient 18 -> 16 °C", ambient_down_samples),
            ("Setpoint 22 -> 23 °C", setpoint_samples),
        ]:
            t = [row[0] for row in samples]
            temp = [row[1] for row in samples]
            plt.plot(t, temp, label=label)
        plt.xlabel("Time (s)")
        plt.ylabel("Temperature (°C)")
        plt.title("Closed-loop HIL timing experiments: plant 5 s / DUT 1 s")
        plt.grid(True)
        plt.legend()
        plt.tight_layout()
        plt.show()


if __name__ == "__main__":
    main()
