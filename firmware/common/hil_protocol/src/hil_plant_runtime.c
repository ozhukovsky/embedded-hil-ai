#include "hil_plant_runtime.h"

enum {
    HEATER_DIAG_DISARMED = 0U,
    HEATER_DIAG_WAIT_LOW = 1U,
    HEATER_DIAG_WAIT_RISING = 2U,
    HEATER_DIAG_WAIT_FALLING = 3U,
    HEATER_DIAG_COMPLETE = 4U
};

void hil_plant_heater_diag_disarm(hil_plant_heater_diag_t *diag) {
    if (diag != 0) diag->state = HEATER_DIAG_DISARMED;
}

void hil_plant_heater_diag_arm(hil_plant_heater_diag_t *diag, uint8_t initial_high) {
    if (diag == 0) return;
    diag->state = initial_high ? HEATER_DIAG_WAIT_LOW : HEATER_DIAG_WAIT_RISING;
}

void hil_plant_heater_diag_edge(hil_plant_heater_diag_t *diag, uint8_t level_high) {
    if (diag == 0) return;
    switch (diag->state) {
    case HEATER_DIAG_WAIT_LOW:
        if (!level_high) diag->state = HEATER_DIAG_WAIT_RISING;
        break;
    case HEATER_DIAG_WAIT_RISING:
        if (level_high) diag->state = HEATER_DIAG_WAIT_FALLING;
        break;
    case HEATER_DIAG_WAIT_FALLING:
        if (!level_high) diag->state = HEATER_DIAG_COMPLETE;
        break;
    default:
        break;
    }
}

uint8_t hil_plant_heater_diag_complete(const hil_plant_heater_diag_t *diag) {
    return (uint8_t)(diag != 0 && diag->state == HEATER_DIAG_COMPLETE);
}

uint8_t hil_plant_runtime_deadline_expired(uint8_t running,
                                          uint32_t run_start_ms,
                                          uint32_t now_ms,
                                          uint32_t max_runtime_ms) {
    if (!running || max_runtime_ms == 0U) return 0U;
    return (uint8_t)((uint32_t)(now_ms - run_start_ms) >= max_runtime_ms);
}
