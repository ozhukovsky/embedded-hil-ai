
import math
import matplotlib.pyplot as plt

# Illustrative thermal model parameters
T_INITIAL = 30.0       # °C
T_AMBIENT = 20.0       # °C
TAU = 300.0            # seconds
SIMULATION_TIME = 900  # seconds

# Candidate numerical integration steps
TIME_STEPS = [30, 15, 5, 1, 0.5, 0.1, 0.05]


def analytical_solution(t):
    """Exact solution of the cooling model."""
    return T_AMBIENT + (
        T_INITIAL - T_AMBIENT
    ) * math.exp(-t / TAU)


def simulate_euler(dt):
    """Simulate cooling using Forward Euler."""
    number_of_steps = round(SIMULATION_TIME / dt)

    if not math.isclose(
        number_of_steps * dt, SIMULATION_TIME
    ):
        raise ValueError("Time step must divide simulation time.")

    temperature = T_INITIAL
    times = [0.0]
    temperatures = [temperature]
    max_error = 0.0

    for step in range(1, number_of_steps + 1):
        temperature += (
            (T_AMBIENT - temperature) / TAU
        ) * dt

        t = step * dt
        exact = analytical_solution(t)

        error = abs(temperature - exact)
        max_error = max(max_error, error)

        times.append(t)
        temperatures.append(temperature)

    return times, temperatures, max_error


# Calculate the analytical reference trajectory
reference_times = [
    i * SIMULATION_TIME / 1000
    for i in range(1001)
]
reference_temperatures = [
    analytical_solution(t) for t in reference_times
]

# Compare numerical solutions
results = []

plt.figure(figsize=(10, 6))
plt.plot(
    reference_times,
    reference_temperatures,
    label="Analytical solution",
    linewidth=2
)

for dt in TIME_STEPS:
    times, temperatures, max_error = simulate_euler(dt)

    results.append((dt, max_error))

    plt.plot(
        times,
        temperatures,
        label=f"Euler, dt={dt:g} s",
        alpha=0.7
    )

plt.xlabel("Time (seconds)")
plt.ylabel("Temperature (°C)")
plt.title("Analytical vs Forward Euler thermal simulation")
plt.grid(True)
plt.legend()
plt.show()

# Print numerical accuracy results
print("\nNumerical accuracy comparison")
print("-" * 42)
print(f"{'Time step (s)':<18}{'Maximum error (°C)':<20}")

for dt, error in results:
    print(f"{dt:<18.3f}{error:<20.8f}")
