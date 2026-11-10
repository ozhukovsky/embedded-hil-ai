#ifndef PLANT_APP_H
#define PLANT_APP_H
#include <stdint.h>
#include "hil_protocol_app.h"
uint8_t plant_app_init(void);
void plant_app_service(void);
const hil_protocol_app_t *plant_app_diagnostics(void);
#endif
