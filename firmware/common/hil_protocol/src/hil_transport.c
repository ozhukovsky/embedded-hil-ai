#include "hil_transport.h"
#include <string.h>

#define RX_MASK (HIL_TRANSPORT_RX_RING_SIZE - 1U)

static int critical_type(uint8_t type) {
    return type == HIL_MSG_ACK || type == HIL_MSG_NACK || type == HIL_MSG_ARMED ||
           type == HIL_MSG_FAULT_REPORT || type == HIL_MSG_TEST_COMPLETE;
}

static void parsed_frame(const hil_frame_view_t *view, void *context) {
    hil_transport_t *t = (hil_transport_t *)context;
    uint8_t slot;
    hil_transport_frame_t *dst;
    if (t->rx_frame_count == HIL_TRANSPORT_RX_FRAME_CAPACITY) {
        ++t->rx_frame_drops;
        return;
    }
    slot = (uint8_t)((t->rx_frame_head + t->rx_frame_count) % HIL_TRANSPORT_RX_FRAME_CAPACITY);
    dst = &t->rx_frames[slot];
    dst->version = view->version;
    dst->message_type = view->message_type;
    dst->sequence = view->sequence;
    dst->payload_length = view->payload_length;
    if (view->payload_length != 0U) memcpy(dst->payload, view->payload, view->payload_length);
    ++t->rx_frame_count;
}

void hil_transport_init(hil_transport_t *t) {
    if (t == NULL) return;
    memset(t, 0, sizeof(*t));
    hil_stream_parser_init(&t->parser);
}

uint16_t hil_transport_rx_push_isr(hil_transport_t *t, const uint8_t *bytes, uint16_t length) {
    uint16_t i, accepted = 0U, head, next;
    if (t == NULL || (bytes == NULL && length != 0U)) return 0U;
    head = t->rx.head;
    for (i = 0U; i < length; ++i) {
        next = (uint16_t)((head + 1U) & RX_MASK);
        if (next == t->rx.tail) { ++t->rx.overflow_bytes; continue; }
        t->rx.bytes[head] = bytes[i];
        head = next;
        ++accepted;
    }
    t->rx.head = head;
    return accepted;
}

uint16_t hil_transport_rx_service(hil_transport_t *t, uint16_t budget) {
    uint16_t done = 0U;
    uint8_t byte;
    if (t == NULL) return 0U;
    while (done < budget) {
        uint16_t tail = t->rx.tail;
        if (tail == t->rx.head) break;
        byte = t->rx.bytes[tail];
        t->rx.tail = (uint16_t)((tail + 1U) & RX_MASK);
        hil_stream_parser_feed(&t->parser, &byte, 1U, parsed_frame, t);
        ++done;
    }
    return done;
}

hil_transport_result_t hil_transport_rx_pop(hil_transport_t *t, hil_transport_frame_t *frame) {
    if (t == NULL || frame == NULL) return HIL_TRANSPORT_INVALID;
    if (t->rx_frame_count == 0U) return HIL_TRANSPORT_EMPTY;
    *frame = t->rx_frames[t->rx_frame_head];
    t->rx_frame_head = (uint8_t)((t->rx_frame_head + 1U) % HIL_TRANSPORT_RX_FRAME_CAPACITY);
    --t->rx_frame_count;
    return HIL_TRANSPORT_OK;
}

hil_transport_result_t hil_transport_tx_enqueue(hil_transport_t *t, const hil_frame_t *frame,
                                                 hil_transport_priority_t priority) {
    hil_transport_wire_frame_t encoded;
    size_t encoded_length = 0U;
    hil_codec_status_t status;
    uint8_t slot;
    if (t == NULL || frame == NULL || priority > HIL_TRANSPORT_TELEMETRY) return HIL_TRANSPORT_INVALID;
    memset(&encoded, 0, sizeof(encoded));
    status = hil_frame_encode(frame, encoded.bytes, sizeof(encoded.bytes), &encoded_length);
    if (status != HIL_CODEC_OK || encoded_length > UINT8_MAX) return HIL_TRANSPORT_INVALID;
    encoded.length = (uint8_t)encoded_length;
    if (critical_type(frame->message_type)) priority = HIL_TRANSPORT_CONTROL;
    if (priority == HIL_TRANSPORT_CONTROL) {
        if (t->control.count == HIL_TRANSPORT_TX_CONTROL_CAPACITY) {
            ++t->tx_control_full;
            return HIL_TRANSPORT_FULL;
        }
        slot = (uint8_t)((t->control.head + t->control.count) % HIL_TRANSPORT_TX_CONTROL_CAPACITY);
        t->control.slots[slot] = encoded;
        ++t->control.count;
        return HIL_TRANSPORT_OK;
    }
    if (t->telemetry.count == HIL_TRANSPORT_TX_TELEMETRY_CAPACITY) {
        ++t->tx_telemetry_drops;
        return HIL_TRANSPORT_TELEMETRY_DROPPED;
    }
    slot = (uint8_t)((t->telemetry.head + t->telemetry.count) % HIL_TRANSPORT_TX_TELEMETRY_CAPACITY);
    t->telemetry.slots[slot] = encoded;
    ++t->telemetry.count;
    return HIL_TRANSPORT_OK;
}

hil_transport_result_t hil_transport_tx_peek(const hil_transport_t *t, hil_transport_wire_frame_t *frame,
                                              hil_transport_priority_t *priority) {
    if (t == NULL || frame == NULL || priority == NULL) return HIL_TRANSPORT_INVALID;
    if (t->control.count != 0U) {
        *frame = t->control.slots[t->control.head];
        *priority = HIL_TRANSPORT_CONTROL;
    } else if (t->telemetry.count != 0U) {
        *frame = t->telemetry.slots[t->telemetry.head];
        *priority = HIL_TRANSPORT_TELEMETRY;
    }
    else return HIL_TRANSPORT_EMPTY;
    return HIL_TRANSPORT_OK;
}

hil_transport_result_t hil_transport_tx_pop(hil_transport_t *t) {
    if (t == NULL) return HIL_TRANSPORT_INVALID;
    if (t->control.count != 0U) {
        t->control.head = (uint8_t)((t->control.head + 1U) % HIL_TRANSPORT_TX_CONTROL_CAPACITY);
        --t->control.count;
    } else if (t->telemetry.count != 0U) {
        t->telemetry.head = (uint8_t)((t->telemetry.head + 1U) % HIL_TRANSPORT_TX_TELEMETRY_CAPACITY);
        --t->telemetry.count;
    } else return HIL_TRANSPORT_EMPTY;
    return HIL_TRANSPORT_OK;
}

hil_transport_result_t hil_transport_tx_pop_priority(hil_transport_t *t,
                                                       hil_transport_priority_t priority) {
    if (t == NULL || priority > HIL_TRANSPORT_TELEMETRY) return HIL_TRANSPORT_INVALID;
    if (priority == HIL_TRANSPORT_CONTROL) {
        if (t->control.count == 0U) return HIL_TRANSPORT_EMPTY;
        t->control.head = (uint8_t)((t->control.head + 1U) % HIL_TRANSPORT_TX_CONTROL_CAPACITY);
        --t->control.count;
    } else {
        if (t->telemetry.count == 0U) return HIL_TRANSPORT_EMPTY;
        t->telemetry.head = (uint8_t)((t->telemetry.head + 1U) % HIL_TRANSPORT_TX_TELEMETRY_CAPACITY);
        --t->telemetry.count;
    }
    return HIL_TRANSPORT_OK;
}
