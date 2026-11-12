#include "dut_app.h"
#include "dut_runtime.h"

void dut_app_init(void) {
    dut_runtime_init();
}

void dut_app_service(void) {
    dut_runtime_service();
}

const hil_protocol_app_t *dut_app_diagnostics(void) { return dut_runtime_protocol(); }
