#ifndef HIL_PLANT_RUNTIME_H
#define HIL_PLANT_RUNTIME_H

#include <stdint.h>

typedef struct {
    /* One-byte ISR/foreground state; reset it with the EXTI IRQ masked. */
    volatile uint8_t state;
} hil_plant_heater_diag_t;

void hil_plant_heater_diag_disarm(hil_plant_heater_diag_t *diag);
void hil_plant_heater_diag_arm(hil_plant_heater_diag_t *diag, uint8_t initial_high);
void hil_plant_heater_diag_edge(hil_plant_heater_diag_t *diag, uint8_t level_high);
uint8_t hil_plant_heater_diag_complete(const hil_plant_heater_diag_t *diag);

/* Unsigned subtraction makes the test valid across one uint32_t tick wrap. */
uint8_t hil_plant_runtime_deadline_expired(uint8_t running,
                                          uint32_t run_start_ms,
                                          uint32_t now_ms,
                                          uint32_t max_runtime_ms);

#endif
