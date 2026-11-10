#include "hil_transport.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static hil_frame_t frame(uint8_t type, uint16_t seq, const uint8_t *payload, uint16_t len) {
    hil_frame_t f = { HIL_PROTOCOL_VERSION, type, seq, len, payload };
    return f;
}

static void test_rx_ring_and_parser(void) {
    hil_transport_t t;
    uint8_t payload[HIL_PROTOCOL_MAX_PAYLOAD], wire[HIL_PROTOCOL_MAX_FRAME];
    hil_transport_frame_t out;
    hil_frame_t f;
    size_t wire_len;
    memset(payload, 0x5a, sizeof(payload));
    f = frame(HIL_MSG_TELEMETRY, 9U, payload, sizeof(payload));
    assert(hil_frame_encode(&f, wire, sizeof(wire), &wire_len) == HIL_CODEC_OK);
    hil_transport_init(&t);
    /* Move ring indexes near end, then wrap with a fragmented maximum frame. */
    {
        uint8_t filler[250] = {0};
        assert(hil_transport_rx_push_isr(&t, filler, sizeof(filler)) == sizeof(filler));
        assert(hil_transport_rx_service(&t, sizeof(filler)) == sizeof(filler));
    }
    assert(hil_transport_rx_push_isr(&t, wire, 17U) == 17U);
    assert(hil_transport_rx_service(&t, 8U) == 8U);
    assert(hil_transport_rx_push_isr(&t, wire + 17U, (uint16_t)(wire_len - 17U)) == wire_len - 17U);
    assert(hil_transport_rx_service(&t, HIL_TRANSPORT_RX_BUDGET) == wire_len - 8U);
    assert(hil_transport_rx_pop(&t, &out) == HIL_TRANSPORT_OK);
    assert(out.message_type == HIL_MSG_TELEMETRY && out.payload_length == HIL_PROTOCOL_MAX_PAYLOAD);
    assert(memcmp(out.payload, payload, sizeof(payload)) == 0);
    /* Two concatenated frames and partial parser retention. */
    hil_transport_init(&t);
    assert(hil_transport_rx_push_isr(&t, wire, (uint16_t)wire_len) == wire_len);
    assert(hil_transport_rx_push_isr(&t, wire, (uint16_t)wire_len) == wire_len);
    (void)hil_transport_rx_service(&t, 7U);
    (void)hil_transport_rx_service(&t, HIL_TRANSPORT_RX_BUDGET);
    (void)hil_transport_rx_service(&t, HIL_TRANSPORT_RX_BUDGET);
    assert(hil_transport_rx_pop(&t, &out) == HIL_TRANSPORT_OK);
    assert(hil_transport_rx_pop(&t, &out) == HIL_TRANSPORT_OK);
}

static void test_rx_overflow(void) {
    hil_transport_t t;
    uint8_t bytes[300] = {0};
    hil_transport_init(&t);
    assert(hil_transport_rx_push_isr(&t, bytes, sizeof(bytes)) == 255U);
    assert(t.rx.overflow_bytes == 45U);
}

static void test_tx_priority_and_full(void) {
    hil_transport_t t;
    hil_transport_wire_frame_t w;
    hil_frame_t f;
    hil_transport_priority_t priority;
    unsigned i;
    hil_transport_init(&t);
    f = frame(HIL_MSG_TELEMETRY, 1U, NULL, 0U);
    for (i = 0; i < HIL_TRANSPORT_TX_TELEMETRY_CAPACITY; ++i) {
        f.sequence = (uint16_t)i;
        assert(hil_transport_tx_enqueue(&t, &f, HIL_TRANSPORT_TELEMETRY) == HIL_TRANSPORT_OK);
    }
    assert(hil_transport_tx_enqueue(&t, &f, HIL_TRANSPORT_TELEMETRY) == HIL_TRANSPORT_TELEMETRY_DROPPED);
    assert(t.tx_telemetry_drops == 1U);
    f = frame(HIL_MSG_ACK, 33U, NULL, 0U);
    assert(hil_transport_tx_enqueue(&t, &f, HIL_TRANSPORT_TELEMETRY) == HIL_TRANSPORT_OK);
    assert(hil_transport_tx_peek(&t, &w, &priority) == HIL_TRANSPORT_OK);
    assert(priority == HIL_TRANSPORT_CONTROL);
    assert(w.bytes[3] == HIL_MSG_ACK);
    assert(hil_transport_tx_pop(&t) == HIL_TRANSPORT_OK);
    assert(hil_transport_tx_peek(&t, &w, &priority) == HIL_TRANSPORT_OK);
    assert(w.bytes[3] == HIL_MSG_TELEMETRY);
    while (hil_transport_tx_pop(&t) == HIL_TRANSPORT_OK) { }
    f = frame(HIL_MSG_NACK, 34U, NULL, 0U);
    assert(hil_transport_tx_enqueue(&t, &f, HIL_TRANSPORT_CONTROL) == HIL_TRANSPORT_OK);
    f = frame(HIL_MSG_FAULT_REPORT, 35U, NULL, 0U);
    assert(hil_transport_tx_enqueue(&t, &f, HIL_TRANSPORT_CONTROL) == HIL_TRANSPORT_OK);
    f = frame(HIL_MSG_TEST_COMPLETE, 36U, NULL, 0U);
    assert(hil_transport_tx_enqueue(&t, &f, HIL_TRANSPORT_CONTROL) == HIL_TRANSPORT_FULL);
    assert(t.tx_control_full == 1U);
    f = frame(HIL_MSG_TELEMETRY, 37U, NULL, 0U);
    assert(hil_transport_tx_enqueue(&t, &f, HIL_TRANSPORT_TELEMETRY) == HIL_TRANSPORT_OK);
    f = frame(HIL_MSG_TEST_COMPLETE, 38U, NULL, 0U);
    assert(hil_transport_tx_enqueue(&t, &f, HIL_TRANSPORT_CONTROL) == HIL_TRANSPORT_FULL);
    assert(t.telemetry.count == 1U);
    assert(hil_transport_tx_peek(&t, &w, &priority) == HIL_TRANSPORT_OK && w.bytes[3] == HIL_MSG_NACK);
    /* peek leaves item owned until successful transmission; completion pops it. */
    assert(hil_transport_tx_pop(&t) == HIL_TRANSPORT_OK);
    assert(hil_transport_tx_peek(&t, &w, &priority) == HIL_TRANSPORT_OK && w.bytes[3] == HIL_MSG_FAULT_REPORT);
    hil_transport_init(&t);
    f = frame(HIL_MSG_ACK, 50U, NULL, 0U);
    assert(hil_transport_tx_enqueue(&t, &f, HIL_TRANSPORT_CONTROL) == HIL_TRANSPORT_OK);
    f = frame(HIL_MSG_ARMED, 51U, NULL, 0U);
    assert(hil_transport_tx_enqueue(&t, &f, HIL_TRANSPORT_TELEMETRY) == HIL_TRANSPORT_OK);
    f = frame(HIL_MSG_TELEMETRY, 52U, NULL, 0U);
    assert(hil_transport_tx_enqueue(&t, &f, HIL_TRANSPORT_TELEMETRY) == HIL_TRANSPORT_OK);
    assert(hil_transport_tx_peek(&t, &w, &priority) == HIL_TRANSPORT_OK && w.bytes[3] == HIL_MSG_ACK);
    assert(hil_transport_tx_pop_priority(&t, priority) == HIL_TRANSPORT_OK);
    assert(hil_transport_tx_peek(&t, &w, &priority) == HIL_TRANSPORT_OK && w.bytes[3] == HIL_MSG_ARMED);
    hil_transport_init(&t);
    f = frame(HIL_MSG_TELEMETRY, 60U, NULL, 0U);
    assert(hil_transport_tx_enqueue(&t, &f, HIL_TRANSPORT_TELEMETRY) == HIL_TRANSPORT_OK);
    assert(hil_transport_tx_peek(&t, &w, &priority) == HIL_TRANSPORT_OK &&
           priority == HIL_TRANSPORT_TELEMETRY);
    f = frame(HIL_MSG_ACK, 61U, NULL, 0U);
    assert(hil_transport_tx_enqueue(&t, &f, HIL_TRANSPORT_CONTROL) == HIL_TRANSPORT_OK);
    /* Completion belongs to the telemetry lane that was actually in flight. */
    assert(hil_transport_tx_pop_priority(&t, priority) == HIL_TRANSPORT_OK);
    assert(hil_transport_tx_peek(&t, &w, &priority) == HIL_TRANSPORT_OK &&
           w.bytes[3] == HIL_MSG_ACK && priority == HIL_TRANSPORT_CONTROL);
}

int main(void) {
    test_rx_ring_and_parser();
    test_rx_overflow();
    test_tx_priority_and_full();
    puts("HIL transport tests passed");
    return 0;
}
