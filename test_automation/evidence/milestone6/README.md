# Milestone 6 Plant experiment evidence

`plant_heater_response.csv` is an unmodified copy of the local CSV exported by
`plant_heater_response_test.py` and supplied with the owner's physical results.
It was preserved in the repository during the 2026-11-11 documentation update;
the original local file was retained. That date records documentation and
preservation, not a wall-clock experiment timestamp absent from the CSV.
The adjacent `.gitattributes` disables newline conversion for this CSV so its
recorded byte hash remains valid across Git checkouts.

SHA-256 of the preserved file:

```text
16bef56ead6dda997d23e65031513fccb29cac2788236bca0342641191e20a91
```

The file records test ID 1 and 14 contiguous samples (indices 0–13), with
5000 ms intervals between MCU uptimes 26946 and 91946 ms. It includes decoded
temperature, ambient, heater latch, DAC code and PLANT_VALID, plus the host
phase, completed-interval input, reference prediction, error and predicted
voltage. Numerical inspection reproduced its reference values and found a
maximum absolute error of 0.4898834 m°C and matching DAC codes. It is captured
experiment data, not a synthetic host-test fixture.

The `phase` field belongs to the interactive PC procedure. `cooling` starts
when the prompt is issued; it does not prove LOW has reached PB0. Sample 8
still reports HIGH. Sample 9 reports the newly sampled LOW while integrating
the previous HIGH; sample 10 begins the completed LOW integrations. The
`completed_input` column makes this distinction explicit.

`reference_temp_mC`, `error_mC`, `expected_dac_code`, and `expected_vout_V` are
host-derived values. Predicted voltage uses the provided measured VCC of
3.28 V; **it is not a multimeter measurement**. The CSV contains neither the
initial/final VOUT measurements nor DWT readings or raw CONFIG/ACK/STOP frames.
An empty `validation_error` column does not independently prove normal STOP.
Those separate owner-reported bench observations and the verification limits
are recorded in [milestone status](../../../docs/MILESTONE_STATUS.md).

The selected tool configuration was initial/ambient 20000 m°C, `tau_s=30`,
heater gain 200 m°C/s, model/telemetry periods 5000 ms, `max_runtime_ms=120000`,
and zero disturbances. The CSV alone does not identify the exact firmware ELF,
source commit, instrument calibration, or all physical transition times.
