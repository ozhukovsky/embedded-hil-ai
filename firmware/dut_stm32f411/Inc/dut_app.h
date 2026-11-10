#ifndef DUT_APP_H
#define DUT_APP_H
#include "hil_protocol_app.h"
void dut_app_init(void);
void dut_app_service(void);
const hil_protocol_app_t *dut_app_diagnostics(void);
#endif
