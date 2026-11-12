#ifndef HIL_PROTOCOL_APP_H
#define HIL_PROTOCOL_APP_H

#include "hil_protocol_messages.h"
#include "hil_protocol_reliability.h"
#include "hil_transport.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HIL_APP_RX_BUDGET 4U
#define HIL_APP_HEARTBEAT_PERIOD_MS 1000U
#define HIL_APP_FW_MAJOR 0U
#define HIL_APP_FW_MINOR 5U
#define HIL_APP_FW_PATCH 0U

typedef void (*hil_protocol_app_frame_handler_t)(
    void *context, const hil_transport_frame_t *frame, uint32_t now_ms);

typedef struct {
    hil_transport_t *transport;
    hil_tx_sequence_t tx_sequence;
    hil_rx_sequence_monitor_t rx_sequence;
    uint8_t role, state, boot_pending, heartbeat_pending, heartbeat_blocked;
    uint8_t transport_ready, status_pending;
    uint16_t capabilities;
    uint8_t fw_major, fw_minor, fw_patch;
    uint16_t fault_code, status_flags;
    uint32_t test_id;
    hil_protocol_app_frame_handler_t frame_handler;
    void *frame_handler_context;
    uint32_t last_heartbeat_ms;
    uint32_t frames_consumed, status_requests, malformed_status_requests;
    uint32_t status_enqueue_failures, boot_enqueue_failures;
    uint32_t heartbeat_missed_periods, heartbeat_queue_full_events;
} hil_protocol_app_t;

hil_codec_status_t hil_protocol_app_init(hil_protocol_app_t *app,
                                          hil_transport_t *transport,
                                          uint8_t role, uint16_t capabilities,
                                          uint32_t now_ms);
/* Bounded foreground work; `now_ms` is a monotonic tick such as HAL_GetTick(). */
void hil_protocol_app_service(hil_protocol_app_t *app, uint32_t now_ms);
/* Default ready for the UART/Plant. A DUT can defer boot/status/heartbeat TX
   until CDC is configured; RX still runs. Readiness is not host receipt.
   One current STATUS snapshot is retained under backpressure; blocked polls
   coalesce. The pending flag clears only after a successful enqueue. */
void hil_protocol_app_set_transport_ready(hil_protocol_app_t *app, uint8_t ready);
void hil_protocol_app_set_frame_handler(hil_protocol_app_t *app,
                                        hil_protocol_app_frame_handler_t handler,
                                        void *context);
void hil_protocol_app_set_status(hil_protocol_app_t *app, uint8_t state,
                                 uint16_t fault_code, uint32_t test_id,
                                 uint16_t status_flags);
void hil_protocol_app_set_firmware_version(hil_protocol_app_t *app,
                                            uint8_t major, uint8_t minor,
                                            uint8_t patch);
hil_codec_status_t hil_protocol_app_send_message(hil_protocol_app_t *app,
                                                  uint8_t type,
                                                  const uint8_t *payload,
                                                  uint16_t length,
                                                  hil_transport_priority_t priority);

#ifdef __cplusplus
}
#endif
#endif
