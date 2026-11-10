#ifndef HIL_BOARD_TRANSPORT_H
#define HIL_BOARD_TRANSPORT_H
#include "hil_transport.h"
hil_transport_t *plant_transport_instance(void);
uint8_t plant_transport_init(void);
void plant_transport_service(void);
uint32_t plant_transport_uart_error_events(void);
#endif
