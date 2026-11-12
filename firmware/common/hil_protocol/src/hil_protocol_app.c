#include "hil_protocol_app.h"
#include <string.h>

static hil_codec_status_t queue_message(hil_protocol_app_t *app, uint8_t type,
                                         const uint8_t *payload, uint16_t length,
                                         hil_transport_priority_t priority) {
    hil_frame_t frame;
    uint16_t sequence, allocated;
    hil_transport_result_t result;
    if (app == NULL || app->transport == NULL) return HIL_CODEC_NULL_ARGUMENT;
    sequence = app->tx_sequence.next_sequence;
    frame.version = HIL_PROTOCOL_VERSION;
    frame.message_type = type;
    frame.sequence = sequence;
    frame.payload_length = length;
    frame.payload = payload;
    result = hil_transport_tx_enqueue(app->transport, &frame, priority);
    if (result != HIL_TRANSPORT_OK) return HIL_CODEC_OUTPUT_TOO_SMALL;
    if (hil_tx_sequence_take(&app->tx_sequence, &allocated) != HIL_CODEC_OK || allocated != sequence)
        return HIL_CODEC_INVALID_VALUE;
    return HIL_CODEC_OK;
}

hil_codec_status_t hil_protocol_app_send_message(hil_protocol_app_t *app, uint8_t type,
                                                  const uint8_t *payload, uint16_t length,
                                                  hil_transport_priority_t priority) {
    return queue_message(app, type, payload, length, priority);
}

void hil_protocol_app_set_frame_handler(hil_protocol_app_t *app,
                                        hil_protocol_app_frame_handler_t handler,
                                        void *context) {
    if (app == NULL) return;
    app->frame_handler = handler;
    app->frame_handler_context = context;
}

void hil_protocol_app_set_transport_ready(hil_protocol_app_t *app, uint8_t ready) {
    if (app != NULL) app->transport_ready = (uint8_t)(ready != 0U);
}

void hil_protocol_app_set_status(hil_protocol_app_t *app, uint8_t state,
                                 uint16_t fault_code, uint32_t test_id,
                                 uint16_t status_flags) {
    if (app == NULL) return;
    app->state = state;
    app->fault_code = fault_code;
    app->test_id = test_id;
    app->status_flags = status_flags;
}

void hil_protocol_app_set_firmware_version(hil_protocol_app_t *app,
                                            uint8_t major, uint8_t minor,
                                            uint8_t patch) {
    if (app == NULL) return;
    app->fw_major = major;
    app->fw_minor = minor;
    app->fw_patch = patch;
}

static void queue_boot_report(hil_protocol_app_t *app, uint32_t now_ms) {
    hil_boot_report_t boot;
    uint8_t payload[12];
    size_t length = 0U;
    boot.role = app->role;
    boot.fw_major = app->fw_major;
    boot.fw_minor = app->fw_minor;
    boot.fw_patch = app->fw_patch;
    boot.reset_cause = 0U; /* Reset source is not captured in this bring-up increment. */
    boot.capabilities = app->capabilities;
    boot.uptime_ms = now_ms;
    if (hil_encode_boot_report(&boot, payload, sizeof(payload), &length) == HIL_CODEC_OK &&
        queue_message(app, HIL_MSG_BOOT_REPORT, payload, (uint16_t)length,
                      HIL_TRANSPORT_CONTROL) == HIL_CODEC_OK) {
        app->boot_pending = 0U;
    } else {
        ++app->boot_enqueue_failures;
    }
}

static void queue_status(hil_protocol_app_t *app, uint32_t now_ms) {
    hil_status_message_t status;
    uint8_t payload[14];
    size_t length = 0U;
    status.role = app->role;
    status.state = app->state;
    status.fault_code = app->fault_code;
    status.test_id = app->test_id;
    status.uptime_ms = now_ms;
    status.status_flags = app->status_flags;
    if (hil_encode_status(&status, payload, sizeof(payload), &length) != HIL_CODEC_OK ||
        queue_message(app, HIL_MSG_STATUS, payload, (uint16_t)length,
                      HIL_TRANSPORT_CONTROL) != HIL_CODEC_OK) {
        ++app->status_enqueue_failures;
    } else app->status_pending = 0U;
}

static void process_rx(hil_protocol_app_t *app, uint32_t now_ms) {
    hil_transport_frame_t frame;
    uint8_t count = 0U;
    while (count < HIL_APP_RX_BUDGET &&
           hil_transport_rx_pop(app->transport, &frame) == HIL_TRANSPORT_OK) {
        hil_sequence_observation_t observation;
        ++count;
        ++app->frames_consumed;
        (void)hil_rx_sequence_observe(&app->rx_sequence, frame.sequence, &observation);
        if (frame.message_type == HIL_MSG_STATUS_REQUEST) {
            hil_status_request_t request;
            ++app->status_requests;
            if (hil_decode_status_request(frame.payload, frame.payload_length, &request) == HIL_CODEC_OK) {
                app->status_pending = 1U;
                if (app->transport_ready != 0U) queue_status(app, now_ms);
            } else
                ++app->malformed_status_requests;
        } else if (app->frame_handler != NULL) {
            app->frame_handler(app->frame_handler_context, &frame, now_ms);
        }
        /* All other valid frame types are intentionally not dispatched yet. */
    }
}

static void service_heartbeat(hil_protocol_app_t *app, uint32_t now_ms) {
    uint32_t periods = (uint32_t)(now_ms - app->last_heartbeat_ms) /
                       HIL_APP_HEARTBEAT_PERIOD_MS;
    if (periods != 0U) {
        app->last_heartbeat_ms += periods * HIL_APP_HEARTBEAT_PERIOD_MS;
        if (app->heartbeat_pending != 0U)
            app->heartbeat_missed_periods += periods;
        else {
            app->heartbeat_pending = 1U;
            if (periods > 1U) app->heartbeat_missed_periods += periods - 1U;
        }
    }
    if (app->heartbeat_pending != 0U && app->transport_ready != 0U &&
        app->boot_pending == 0U && app->status_pending == 0U) {
        hil_heartbeat_t heartbeat;
        uint8_t payload[10];
        size_t length = 0U;
        heartbeat.role = app->role;
        heartbeat.state = app->state;
        heartbeat.test_id = app->test_id;
        heartbeat.uptime_ms = now_ms;
        if (hil_encode_heartbeat(&heartbeat, payload, sizeof(payload), &length) == HIL_CODEC_OK &&
            queue_message(app, HIL_MSG_HEARTBEAT, payload, (uint16_t)length,
                          HIL_TRANSPORT_CONTROL) == HIL_CODEC_OK) {
            app->heartbeat_pending = 0U;
            app->heartbeat_blocked = 0U;
        } else if (app->heartbeat_blocked == 0U) {
            ++app->heartbeat_queue_full_events;
            app->heartbeat_blocked = 1U;
        }
    }
}

hil_codec_status_t hil_protocol_app_init(hil_protocol_app_t *app,
                                          hil_transport_t *transport,
                                          uint8_t role, uint16_t capabilities,
                                          uint32_t now_ms) {
    if (app == NULL || transport == NULL || (role != HIL_ROLE_DUT && role != HIL_ROLE_PLANT))
        return HIL_CODEC_NULL_ARGUMENT;
    memset(app, 0, sizeof(*app));
    app->transport = transport;
    app->role = role;
    app->state = HIL_STATE_INITIALIZING;
    app->capabilities = capabilities;
    app->fw_major = HIL_APP_FW_MAJOR;
    app->fw_minor = HIL_APP_FW_MINOR;
    app->fw_patch = HIL_APP_FW_PATCH;
    app->last_heartbeat_ms = now_ms;
    app->boot_pending = 1U;
    app->transport_ready = 1U;
    if (hil_tx_sequence_init(&app->tx_sequence) != HIL_CODEC_OK ||
        hil_rx_sequence_monitor_init(&app->rx_sequence) != HIL_CODEC_OK)
        return HIL_CODEC_INVALID_VALUE;
    return HIL_CODEC_OK;
}

void hil_protocol_app_service(hil_protocol_app_t *app, uint32_t now_ms) {
    if (app == NULL || app->transport == NULL) return;
    if (app->transport_ready != 0U) {
        if (app->boot_pending != 0U) queue_boot_report(app, now_ms);
        if (app->status_pending != 0U) queue_status(app, now_ms);
    }
    process_rx(app, now_ms);
    service_heartbeat(app, now_ms);
}
