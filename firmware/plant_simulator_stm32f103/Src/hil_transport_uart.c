#include "hil_board_transport.h"
#include "main.h"
#include <string.h>

extern UART_HandleTypeDef huart1;
static hil_transport_t transport;
static uint8_t rx_byte;
static uint8_t tx_inflight[HIL_PROTOCOL_MAX_FRAME];
static volatile uint8_t tx_busy;
static volatile uint8_t tx_complete;
static volatile uint8_t rx_rearm_needed;
static volatile uint32_t uart_error_events;
static hil_transport_priority_t tx_priority;

hil_transport_t *plant_transport_instance(void) { return &transport; }
uint8_t plant_transport_init(void) {
    hil_transport_init(&transport);
    tx_busy = 0U; tx_complete = 0U; rx_rearm_needed = 0U; uart_error_events = 0U;
    return (uint8_t)(HAL_UART_Receive_IT(&huart1, &rx_byte, 1U) == HAL_OK);
}

void plant_transport_service(void) {
    hil_transport_wire_frame_t frame;
    HAL_StatusTypeDef result;
    (void)hil_transport_rx_service(&transport, HIL_TRANSPORT_RX_BUDGET);
    if (rx_rearm_needed != 0U) {
        rx_rearm_needed = 0U;
        if (HAL_UART_Receive_IT(&huart1, &rx_byte, 1U) != HAL_OK) rx_rearm_needed = 1U;
    }
    if (tx_complete != 0U) {
        tx_complete = 0U;
        (void)hil_transport_tx_pop_priority(&transport, tx_priority);
        tx_busy = 0U;
    }
    if (tx_busy != 0U || hil_transport_tx_peek(&transport, &frame, &tx_priority) != HIL_TRANSPORT_OK) return;
    memcpy(tx_inflight, frame.bytes, frame.length);
    result = HAL_UART_Transmit_IT(&huart1, tx_inflight, frame.length);
    if (result == HAL_OK) tx_busy = 1U;
    else if (result == HAL_ERROR) ++transport.tx_driver_start_errors;
    /* HAL_BUSY/HAL_ERROR leaves the frame queued; successful completion removes it. */
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == USART1) {
        (void)hil_transport_rx_push_isr(&transport, &rx_byte, 1U);
        rx_rearm_needed = (HAL_UART_Receive_IT(&huart1, &rx_byte, 1U) != HAL_OK);
    }
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == USART1) tx_complete = 1U;
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == USART1) {
        ++uart_error_events;
        rx_rearm_needed = 1U;
    }
}

uint32_t plant_transport_uart_error_events(void) { return uart_error_events; }
