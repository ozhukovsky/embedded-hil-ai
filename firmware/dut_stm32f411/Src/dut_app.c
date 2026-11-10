#include "dut_app.h"
#include "hil_board_transport.h"
#include "hil_protocol_app.h"
#include "main.h"

static hil_protocol_app_t app;

void dut_app_init(void) {
    /* USB CDC is initialized by MX_USB_DEVICE_Init before this point. */
    (void)hil_protocol_app_init(&app, dut_transport_instance(), HIL_ROLE_DUT,
                                (uint16_t)(1U << 0), HAL_GetTick());
}

void dut_app_service(void) {
    hil_protocol_app_service(&app, HAL_GetTick());
}

const hil_protocol_app_t *dut_app_diagnostics(void) { return &app; }
