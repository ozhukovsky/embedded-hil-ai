#include "hil_plant_model.h"

#include <limits.h>
#include <math.h>
#include <string.h>

static int in_envelope(int32_t value) {
    return value >= HIL_PLANT_TEMP_MIN_MC && value <= HIL_PLANT_TEMP_MAX_MC;
}

int hil_plant_model_validate(const hil_plant_config_t *c) {
    uint8_t i;
    int64_t equilibrium_mC;
    if (c == NULL || c->test_id == 0U || c->tau_s < 5U ||
        c->heater_gain_mC_per_s < 0 || c->model_step_ms != HIL_PLANT_DEFAULT_STEP_MS ||
        c->telemetry_period_ms == 0U || c->max_runtime_ms == 0U ||
        c->disturbance_count > HIL_PLANT_MAX_DISTURBANCES ||
        !in_envelope(c->initial_temp_mC) || !in_envelope(c->initial_ambient_mC)) return 0;
    /* uint32 * nonnegative int32 fits int64; the bounds above keep addition safe. */
    equilibrium_mC = (int64_t)c->initial_ambient_mC +
                     (int64_t)c->tau_s * (int64_t)c->heater_gain_mC_per_s;
    if (equilibrium_mC > HIL_PLANT_TEMP_MAX_MC) return 0;
    for (i = 0U; i < c->disturbance_count; ++i) {
        if (!in_envelope(c->disturbances[i].new_ambient_temp_mC) ||
            c->disturbances[i].offset_ms > c->max_runtime_ms ||
            (i != 0U && c->disturbances[i].offset_ms <= c->disturbances[i - 1U].offset_ms)) return 0;
        equilibrium_mC = (int64_t)c->disturbances[i].new_ambient_temp_mC +
                         (int64_t)c->tau_s * (int64_t)c->heater_gain_mC_per_s;
        if (equilibrium_mC > HIL_PLANT_TEMP_MAX_MC) return 0;
    }
    return 1;
}

int hil_plant_model_init(hil_plant_model_t *m, const hil_plant_config_t *c) {
    if (m == NULL || !hil_plant_model_validate(c)) return 0;
    memset(m, 0, sizeof(*m));
    m->config = *c;
    m->temperature_c = (double)c->initial_temp_mC / 1000.0;
    m->ambient_c = (double)c->initial_ambient_mC / 1000.0;
    m->tau_s = (double)c->tau_s;
    m->heater_gain_c_per_s = (double)c->heater_gain_mC_per_s / 1000.0;
    while (m->disturbance_index < c->disturbance_count &&
           c->disturbances[m->disturbance_index].offset_ms == 0U) {
        m->ambient_c = (double)c->disturbances[m->disturbance_index].new_ambient_temp_mC / 1000.0;
        ++m->disturbance_index;
    }
    return 1;
}

int hil_plant_model_step(hil_plant_model_t *m, uint8_t heater, int32_t *out_mC) {
    double dt, next;
    int32_t rounded;
    if (m == NULL || out_mC == NULL || heater > 1U || m->tau_s < 5.0) return 0;
    dt = (double)HIL_PLANT_DEFAULT_STEP_MS / 1000.0;
    next = m->temperature_c + dt * ((m->ambient_c - m->temperature_c) / m->tau_s +
                                    (heater != 0U ? m->heater_gain_c_per_s : 0.0));
    if (!isfinite(next) || next < -20.0 || next > 60.0) return 0;
    rounded = (int32_t)(next * 1000.0 + (next >= 0.0 ? 0.5 : -0.5));
    if (!in_envelope(rounded)) return 0;
    m->temperature_c = next;
    if (m->elapsed_ms <= UINT32_MAX - HIL_PLANT_DEFAULT_STEP_MS)
        m->elapsed_ms += HIL_PLANT_DEFAULT_STEP_MS;
    else return 0;
    while (m->disturbance_index < m->config.disturbance_count &&
           m->config.disturbances[m->disturbance_index].offset_ms <= m->elapsed_ms) {
        m->ambient_c = (double)m->config.disturbances[m->disturbance_index].new_ambient_temp_mC / 1000.0;
        ++m->disturbance_index;
    }
    *out_mC = rounded;
    return 1;
}

int hil_plant_temperature_to_dac(int32_t temperature_mC, uint16_t *code) {
    double mapped;
    if (code == NULL || !in_envelope(temperature_mC)) return 0;
    mapped = 100.0 + (((double)temperature_mC / 1000.0 + 20.0) / 80.0) * 3900.0;
    if (mapped < HIL_PLANT_DAC_MIN_CODE) mapped = HIL_PLANT_DAC_MIN_CODE;
    if (mapped > HIL_PLANT_DAC_MAX_CODE) mapped = HIL_PLANT_DAC_MAX_CODE;
    *code = (uint16_t)(mapped + 0.5);
    return 1;
}
