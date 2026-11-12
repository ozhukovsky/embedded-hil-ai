#ifndef HIL_BOARD_TRANSPORT_H
#define HIL_BOARD_TRANSPORT_H
#include "hil_transport.h"
hil_transport_t *dut_transport_instance(void);
void dut_transport_init(void);
void dut_transport_service(void);
/* CDC configured and class data available; does not prove a host is reading. */
uint8_t dut_transport_ready(void);
void dut_usb_rx_rearm_result(uint8_t result);
#endif
