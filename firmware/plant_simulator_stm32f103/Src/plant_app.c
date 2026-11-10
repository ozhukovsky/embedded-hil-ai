#include "plant_app.h"
#include "hil_board_transport.h"
#include "hil_protocol_app.h"
#include "main.h"
#include "plant_runtime.h"

uint8_t plant_app_init(void) {
    return plant_runtime_init();
}

void plant_app_service(void) {
    plant_runtime_service();
}

const hil_protocol_app_t *plant_app_diagnostics(void) { return plant_runtime_diagnostics(); }
