#ifndef HIL_PLANT_MODEL_H
#define HIL_PLANT_MODEL_H

#include "hil_protocol_messages.h"

#define HIL_PLANT_TEMP_MIN_MC (-20000)
#define HIL_PLANT_TEMP_MAX_MC 60000
#define HIL_PLANT_DEFAULT_STEP_MS 5000U
#define HIL_PLANT_DAC_MIN_CODE 100U
#define HIL_PLANT_DAC_MAX_CODE 4000U

typedef struct {
    double temperature_c;
    double ambient_c;
    double tau_s;
    double heater_gain_c_per_s;
    uint32_t elapsed_ms;
    uint8_t disturbance_index;
    hil_plant_config_t config;
} hil_plant_model_t;

/* The accepted model envelope is -20..+60 C for initial and disturbed ambient. */
int hil_plant_model_validate(const hil_plant_config_t *config);
int hil_plant_model_init(hil_plant_model_t *model,
                         const hil_plant_config_t *config);
/* Advances one fixed 5-second interval using the previously latched heater. */
int hil_plant_model_step(hil_plant_model_t *model, uint8_t heater_was_on,
                         int32_t *temperature_mC);
/* Maps an in-envelope model temperature; invalid model values are rejected. */
int hil_plant_temperature_to_dac(int32_t temperature_mC, uint16_t *dac_code);

#endif
