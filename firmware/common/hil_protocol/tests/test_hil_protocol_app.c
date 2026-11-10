#include "hil_protocol_app.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void inject_status_request(hil_transport_t *transport, uint16_t sequence) {
    uint8_t wire[HIL_PROTOCOL_MAX_FRAME];
    size_t length = 0U;
    hil_frame_t frame = { HIL_PROTOCOL_VERSION, HIL_MSG_STATUS_REQUEST, sequence, 0U, NULL };
    assert(hil_frame_encode(&frame, wire, sizeof(wire), &length) == HIL_CODEC_OK);
    assert(hil_transport_rx_push_isr(transport, wire, (uint16_t)length) == length);
    assert(hil_transport_rx_service(transport, HIL_TRANSPORT_RX_BUDGET) == length);
}

static void inject_frame(hil_transport_t *transport, uint8_t type, uint16_t sequence,
                         const uint8_t *payload, uint16_t payload_length) {
    uint8_t wire[HIL_PROTOCOL_MAX_FRAME];
    size_t length = 0U;
    hil_frame_t frame = { HIL_PROTOCOL_VERSION, type, sequence, payload_length, payload };
    assert(hil_frame_encode(&frame, wire, sizeof(wire), &length) == HIL_CODEC_OK);
    assert(hil_transport_rx_push_isr(transport, wire, (uint16_t)length) == length);
    assert(hil_transport_rx_service(transport, HIL_TRANSPORT_RX_BUDGET) == length);
}

typedef struct { hil_protocol_app_t *app; uint32_t calls; } handler_context_t;
static void frame_handler(void *opaque, const hil_transport_frame_t *frame, uint32_t now) {
    handler_context_t *context = (handler_context_t *)opaque;
    assert(frame->message_type == HIL_MSG_CONFIG_SET && frame->sequence == 12U && now == 55U);
    ++context->calls;
    hil_protocol_app_set_status(context->app, HIL_STATE_READY, 0U, 77U, 9U);
}

static hil_transport_wire_frame_t dequeue(hil_transport_t *transport) {
    hil_transport_wire_frame_t wire;
    hil_transport_priority_t priority;
    assert(hil_transport_tx_peek(transport, &wire, &priority) == HIL_TRANSPORT_OK);
    assert(priority == HIL_TRANSPORT_CONTROL);
    assert(hil_transport_tx_pop_priority(transport, priority) == HIL_TRANSPORT_OK);
    return wire;
}

static void test_boot_status_sequences_and_roles(void) {
    hil_transport_t transport;
    hil_protocol_app_t app;
    hil_transport_wire_frame_t wire;
    hil_boot_report_t boot;
    hil_status_message_t status;
    hil_sequence_observation_t observation;
    hil_transport_init(&transport);
    assert(hil_protocol_app_init(&app, &transport, HIL_ROLE_DUT, 1U, 10U) == HIL_CODEC_OK);
    assert(app.state == HIL_STATE_INITIALIZING);
    hil_protocol_app_service(&app, 10U);
    wire = dequeue(&transport);
    assert(wire.bytes[3] == HIL_MSG_BOOT_REPORT && wire.bytes[4] == 0U);
    assert(hil_decode_boot_report(&wire.bytes[8], 12U, &boot) == HIL_CODEC_OK);
    assert(boot.role == HIL_ROLE_DUT && boot.capabilities == 1U);
    assert(app.boot_pending == 0U);

    app.state = HIL_STATE_READY; /* STATUS_REQUEST is legal in any state. */
    inject_status_request(&transport, 4U);
    hil_protocol_app_service(&app, 120U);
    wire = dequeue(&transport);
    assert(wire.bytes[3] == HIL_MSG_STATUS && wire.bytes[4] == 1U);
    assert(hil_decode_status(&wire.bytes[8], 14U, &status) == HIL_CODEC_OK);
    assert(status.role == HIL_ROLE_DUT && status.state == HIL_STATE_READY &&
           status.test_id == 0U && status.fault_code == 0U && status.uptime_ms == 120U);

    inject_status_request(&transport, 6U);
    hil_protocol_app_service(&app, 130U);
    wire = dequeue(&transport);
    assert(wire.bytes[3] == HIL_MSG_STATUS && wire.bytes[4] == 2U);
    assert(app.status_requests == 2U && app.frames_consumed == 2U);
    assert(app.rx_sequence.frames_observed == 2U && app.rx_sequence.gap_events == 1U);
    assert(hil_rx_sequence_observe(&app.rx_sequence, 6U, &observation) == HIL_CODEC_OK);
    assert(observation.duplicate_sequence != 0U);

    hil_transport_init(&transport);
    assert(hil_protocol_app_init(&app, &transport, HIL_ROLE_PLANT, 2U, 0U) == HIL_CODEC_OK);
    hil_protocol_app_service(&app, 0U);
    (void)dequeue(&transport);
    inject_status_request(&transport, 1U);
    hil_protocol_app_service(&app, 1U);
    wire = dequeue(&transport);
    assert(hil_decode_status(&wire.bytes[8], 14U, &status) == HIL_CODEC_OK);
    assert(status.role == HIL_ROLE_PLANT && status.state == HIL_STATE_INITIALIZING);
}

static void test_queue_full_and_heartbeat(void) {
    hil_transport_t transport;
    hil_protocol_app_t app;
    hil_transport_wire_frame_t wire;
    hil_transport_priority_t priority;
    hil_heartbeat_t heartbeat;
    hil_frame_t filler = { HIL_PROTOCOL_VERSION, HIL_MSG_STATUS, 500U, 0U, NULL };
    hil_transport_init(&transport);
    assert(hil_protocol_app_init(&app, &transport, HIL_ROLE_DUT, 1U, 0U) == HIL_CODEC_OK);
    hil_protocol_app_service(&app, 0U); /* queues BOOT_REPORT */
    assert(hil_transport_tx_enqueue(&transport, &filler, HIL_TRANSPORT_CONTROL) == HIL_TRANSPORT_OK);
    inject_status_request(&transport, 9U);
    hil_protocol_app_service(&app, 1U);
    assert(app.status_enqueue_failures == 1U && app.tx_sequence.next_sequence == 1U);
    hil_protocol_app_service(&app, 999U);
    assert(app.heartbeat_pending == 0U);
    hil_protocol_app_service(&app, 1000U);
    assert(app.heartbeat_pending != 0U && app.heartbeat_queue_full_events == 1U);
    (void)hil_transport_tx_pop(&transport);
    hil_protocol_app_service(&app, 1001U);
    assert(app.heartbeat_pending == 0U);
    assert(hil_transport_tx_peek(&transport, &wire, &priority) == HIL_TRANSPORT_OK);
    /* Filler remains at the head; pop it, then inspect heartbeat. */
    assert(wire.bytes[3] == HIL_MSG_STATUS);
    assert(hil_transport_tx_pop_priority(&transport, priority) == HIL_TRANSPORT_OK);
    wire = dequeue(&transport);
    assert(wire.bytes[3] == HIL_MSG_HEARTBEAT && wire.bytes[4] == 1U);
    assert(hil_decode_heartbeat(&wire.bytes[8], 10U, &heartbeat) == HIL_CODEC_OK);
    assert(heartbeat.role == HIL_ROLE_DUT && heartbeat.state == HIL_STATE_INITIALIZING &&
           heartbeat.uptime_ms == 1001U);
    assert(app.heartbeat_missed_periods == 0U);

    hil_transport_init(&transport);
    assert(hil_protocol_app_init(&app, &transport, HIL_ROLE_PLANT, 2U, 0U) == HIL_CODEC_OK);
    hil_protocol_app_service(&app, 0U);
    (void)dequeue(&transport);
    hil_protocol_app_service(&app, 3500U);
    assert(app.heartbeat_missed_periods == 2U);
    wire = dequeue(&transport);
    assert(wire.bytes[3] == HIL_MSG_HEARTBEAT);
    assert(hil_decode_heartbeat(&wire.bytes[8], 10U, &heartbeat) == HIL_CODEC_OK);
    assert(heartbeat.role == HIL_ROLE_PLANT && heartbeat.uptime_ms == 3500U);
}

static void test_board_hook_and_dynamic_status(void) {
    hil_transport_t transport;
    hil_protocol_app_t app;
    hil_transport_wire_frame_t wire;
    hil_status_message_t status;
    handler_context_t context;
    uint8_t command[1] = {HIL_ROLE_PLANT};
    hil_transport_init(&transport);
    assert(hil_protocol_app_init(&app,&transport,HIL_ROLE_PLANT,0U,0U)==HIL_CODEC_OK);
    context.app=&app;context.calls=0U;
    hil_protocol_app_set_frame_handler(&app,frame_handler,&context);
    hil_protocol_app_service(&app,0U);
    (void)dequeue(&transport);
    inject_frame(&transport,HIL_MSG_CONFIG_SET,12U,command,sizeof(command));
    hil_protocol_app_service(&app,55U);
    assert(context.calls==1U);
    inject_status_request(&transport,13U);
    hil_protocol_app_service(&app,56U);
    wire=dequeue(&transport);
    assert(wire.bytes[3]==HIL_MSG_STATUS);
    assert(hil_decode_status(&wire.bytes[8],14U,&status)==HIL_CODEC_OK);
    assert(status.role==HIL_ROLE_PLANT&&status.state==HIL_STATE_READY&&
           status.test_id==77U&&status.status_flags==9U);
}

int main(void) {
    test_boot_status_sequences_and_roles();
    test_queue_full_and_heartbeat();
    test_board_hook_and_dynamic_status();
    puts("HIL application tests passed");
    return 0;
}
