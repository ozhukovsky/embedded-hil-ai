#include "hil_board_transport.h"
#include "usbd_cdc_if.h"
#include "usb_device.h"
#include <string.h>

extern USBD_HandleTypeDef hUsbDeviceFS;

static hil_transport_t transport;
static uint8_t tx_inflight[HIL_PROTOCOL_MAX_FRAME];
static volatile uint8_t tx_busy;
static volatile uint8_t tx_complete;
static volatile uint8_t rx_rearm_needed;
static hil_transport_priority_t tx_priority;

hil_transport_t *dut_transport_instance(void) { return &transport; }
uint8_t dut_transport_ready(void) {
    uint32_t irq_mask = __get_PRIMASK();
    uint8_t ready;
    __disable_irq();
    ready = (uint8_t)(hUsbDeviceFS.dev_state == USBD_STATE_CONFIGURED &&
                       hUsbDeviceFS.pClassData != NULL);
    __set_PRIMASK(irq_mask);
    return ready;
}
void dut_transport_init(void) {
    hil_transport_init(&transport);
    tx_busy = 0U; tx_complete = 0U; rx_rearm_needed = 0U;
}

void dut_usb_receive_bytes(uint8_t *bytes, uint32_t length) {
    uint32_t offset = 0U;
    while (offset < length) {
        uint16_t chunk = (uint16_t)((length - offset) > UINT16_MAX ? UINT16_MAX : (length - offset));
        (void)hil_transport_rx_push_isr(&transport, bytes + offset, chunk);
        offset += chunk;
    }
}

void dut_usb_transmit_complete(void) { tx_complete = 1U; }
void dut_usb_rx_rearm_result(uint8_t result) { rx_rearm_needed = (result != USBD_OK); }

void dut_transport_service(void) {
    hil_transport_wire_frame_t frame;
    uint8_t result;
    (void)hil_transport_rx_service(&transport, HIL_TRANSPORT_RX_BUDGET);
    if (rx_rearm_needed != 0U) {
        uint32_t irq_mask = __get_PRIMASK();
        __disable_irq();
        if (hUsbDeviceFS.dev_state == USBD_STATE_CONFIGURED && hUsbDeviceFS.pClassData != NULL)
            dut_usb_rx_rearm_result(USBD_CDC_ReceivePacket(&hUsbDeviceFS));
        __set_PRIMASK(irq_mask);
    }
    if (tx_complete != 0U) {
        tx_complete = 0U;
        (void)hil_transport_tx_pop_priority(&transport, tx_priority);
        tx_busy = 0U;
    }
    if (tx_busy != 0U) return;
    if (hil_transport_tx_peek(&transport, &frame, &tx_priority) != HIL_TRANSPORT_OK) return;
    memcpy(tx_inflight, frame.bytes, frame.length);
    result = CDC_Transmit_FS(tx_inflight, frame.length);
    if (result == USBD_OK) {
        tx_busy = 1U;
    } else if (result != USBD_BUSY) {
        ++transport.tx_driver_start_errors;
    }
    /* USBD_BUSY leaves the queued frame untouched and is retried on a later loop. */
}
