#ifndef HIL_TRANSPORT_H
#define HIL_TRANSPORT_H

#include "hil_protocol.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HIL_TRANSPORT_RX_RING_SIZE 256U /* usable: 255 bytes */
#define HIL_TRANSPORT_RX_FRAME_CAPACITY 4U
#define HIL_TRANSPORT_TX_CONTROL_CAPACITY 2U
#define HIL_TRANSPORT_TX_TELEMETRY_CAPACITY 4U
#define HIL_TRANSPORT_RX_BUDGET 128U

typedef enum { HIL_TRANSPORT_CONTROL = 0, HIL_TRANSPORT_TELEMETRY = 1 } hil_transport_priority_t;
typedef enum { HIL_TRANSPORT_OK = 0, HIL_TRANSPORT_EMPTY, HIL_TRANSPORT_FULL,
               HIL_TRANSPORT_TELEMETRY_DROPPED, HIL_TRANSPORT_INVALID } hil_transport_result_t;
typedef struct { uint8_t bytes[HIL_PROTOCOL_MAX_FRAME]; uint8_t length; } hil_transport_wire_frame_t;
typedef struct {
    uint8_t version, message_type;
    uint16_t sequence, payload_length;
    uint8_t payload[HIL_PROTOCOL_MAX_PAYLOAD];
} hil_transport_frame_t;
typedef struct {
    volatile uint16_t head, tail;
    volatile uint32_t overflow_bytes;
    volatile uint8_t bytes[HIL_TRANSPORT_RX_RING_SIZE];
} hil_transport_rx_ring_t;
typedef struct {
    hil_transport_wire_frame_t slots[HIL_TRANSPORT_TX_CONTROL_CAPACITY];
    uint8_t head, count;
} hil_transport_control_queue_t;
typedef struct {
    hil_transport_wire_frame_t slots[HIL_TRANSPORT_TX_TELEMETRY_CAPACITY];
    uint8_t head, count;
} hil_transport_telemetry_queue_t;
typedef struct {
    hil_transport_rx_ring_t rx;
    hil_stream_parser_t parser;
    hil_transport_frame_t rx_frames[HIL_TRANSPORT_RX_FRAME_CAPACITY];
    uint8_t rx_frame_head, rx_frame_count;
    hil_transport_control_queue_t control;
    hil_transport_telemetry_queue_t telemetry;
    uint32_t rx_frame_drops, tx_control_full, tx_telemetry_drops, tx_driver_start_errors;
} hil_transport_t;

void hil_transport_init(hil_transport_t *transport);
/* ISR/callback producer; returns bytes accepted. Overflow bytes are counted. */
uint16_t hil_transport_rx_push_isr(hil_transport_t *transport, const uint8_t *bytes, uint16_t length);
/* Main-loop service, bounded by max_bytes. Returns bytes parsed. */
uint16_t hil_transport_rx_service(hil_transport_t *transport, uint16_t max_bytes);
hil_transport_result_t hil_transport_rx_pop(hil_transport_t *transport, hil_transport_frame_t *frame);
hil_transport_result_t hil_transport_tx_enqueue(hil_transport_t *transport, const hil_frame_t *frame,
                                                 hil_transport_priority_t priority);
/* Copies highest-priority queued wire frame. */
hil_transport_result_t hil_transport_tx_peek(const hil_transport_t *transport,
                                              hil_transport_wire_frame_t *frame,
                                              hil_transport_priority_t *priority);
/* Convenience pop for a completed transfer of the currently highest lane. */
hil_transport_result_t hil_transport_tx_pop(hil_transport_t *transport);
/* Pop the lane that actually completed; a higher-priority lane can arrive while busy. */
hil_transport_result_t hil_transport_tx_pop_priority(hil_transport_t *transport,
                                                       hil_transport_priority_t priority);

#ifdef __cplusplus
}
#endif
#endif
