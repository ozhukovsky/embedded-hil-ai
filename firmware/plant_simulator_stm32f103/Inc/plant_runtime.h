#ifndef PLANT_RUNTIME_H
#define PLANT_RUNTIME_H
#include "hil_protocol_app.h"
#include <stdint.h>
uint8_t plant_runtime_init(void);
void plant_runtime_service(void);
const hil_protocol_app_t *plant_runtime_diagnostics(void);
uint32_t plant_runtime_max_loop_cycles(void);
uint32_t plant_runtime_over_budget_count(void);
#endif
