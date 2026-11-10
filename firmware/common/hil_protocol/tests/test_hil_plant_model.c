#include "hil_plant_model.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static hil_plant_config_t config(void) {
    hil_plant_config_t c;
    memset(&c, 0, sizeof(c));
    c.test_id = 1U;
    c.initial_temp_mC = 20000;
    c.initial_ambient_mC = 20000;
    c.tau_s = 300U;
    c.heater_gain_mC_per_s = 20;
    c.model_step_ms = 5000U;
    c.telemetry_period_ms = 5000U;
    c.max_runtime_ms = 600000U;
    return c;
}

int main(void) {
    hil_plant_config_t c = config();
    hil_plant_model_t m;
    double reference = 20.0;
    int32_t output = 0;
    uint16_t code = 0U;
    unsigned i;
    assert(hil_plant_model_validate(&c));
    assert(hil_plant_model_init(&m, &c));
    for (i = 0U; i < 40U; ++i) {
        reference = reference + 5.0 * ((20.0 - reference) / 300.0 + 0.02);
        assert(hil_plant_model_step(&m, 1U, &output));
        assert(fabs((double)output / 1000.0 - reference) < 0.001);
    }
    assert(hil_plant_temperature_to_dac(-20000, &code) && code == 100U);
    assert(hil_plant_temperature_to_dac(20000, &code) && code == 2050U);
    assert(hil_plant_temperature_to_dac(60000, &code) && code == 4000U);
    assert(!hil_plant_temperature_to_dac(60001, &code));
    /* Heater permanently OFF at equal ambient remains at equilibrium. */
    c = config();
    assert(hil_plant_model_init(&m, &c));
    for (i = 0U; i < 20U; ++i) assert(hil_plant_model_step(&m, 0U, &output));
    assert(output == 20000);
    /* With heater continuously ON, the long-run state approaches Ta + tau*Kh. */
    c = config();
    assert(hil_plant_model_init(&m, &c));
    for (i = 0U; i < 300U; ++i) assert(hil_plant_model_step(&m, 1U, &output));
    assert(fabs(m.temperature_c - 26.0) < 0.05);
    /* A disturbance is applied at the 5-second boundary after that interval. */
    c = config(); c.disturbance_count = 1U;
    c.disturbances[0].offset_ms = 5000U; c.disturbances[0].new_ambient_temp_mC = 30000;
    assert(hil_plant_model_init(&m, &c));
    assert(hil_plant_model_step(&m, 0U, &output));
    assert(output == 20000 && m.ambient_c == 30.0);
    assert(hil_plant_model_step(&m, 0U, &output));
    assert(output == 20167);
    /* Offset zero applies at run initialization before the first interval. */
    c = config(); c.disturbance_count = 1U;
    c.disturbances[0].offset_ms = 0U; c.disturbances[0].new_ambient_temp_mC = 10000;
    assert(hil_plant_model_init(&m, &c) && m.ambient_c == 10.0);
    /* Minimum stable tau (dt/tau == 1) remains valid. */
    c = config(); c.tau_s = 5U; c.heater_gain_mC_per_s = 1000;
    assert(hil_plant_model_validate(&c));
    assert(hil_plant_model_init(&m, &c));
    assert(hil_plant_model_step(&m, 0U, &output) && output == 20000);
    c = config(); c.initial_temp_mC = -20000; c.initial_ambient_mC = -20000;
    assert(hil_plant_model_init(&m, &c));
    assert(hil_plant_model_step(&m, 0U, &output) && output == -20000);
    c.tau_s = 4U;
    assert(!hil_plant_model_validate(&c));
    c = config(); c.heater_gain_mC_per_s = -1;
    assert(!hil_plant_model_validate(&c));
    c = config(); c.tau_s = 3000U;
    assert(!hil_plant_model_validate(&c));
    c = config(); c.initial_ambient_mC = 60001;
    assert(!hil_plant_model_validate(&c));
    c = config(); c.heater_gain_mC_per_s = 140;
    assert(!hil_plant_model_validate(&c));
    c = config(); c.disturbance_count = 2U;
    c.disturbances[0].offset_ms = 10000U; c.disturbances[0].new_ambient_temp_mC = 10000;
    c.disturbances[1].offset_ms = 5000U; c.disturbances[1].new_ambient_temp_mC = 30000;
    assert(!hil_plant_model_validate(&c));
    c = config(); c.disturbance_count = 1U;
    c.disturbances[0].offset_ms = 5000U; c.disturbances[0].new_ambient_temp_mC = 10000;
    assert(hil_plant_model_init(&m, &c));
    assert(hil_plant_model_step(&m, 0U, &output));
    assert(m.ambient_c == 10.0 && m.elapsed_ms == 5000U);
    puts("plant model tests passed");
    return 0;
}
