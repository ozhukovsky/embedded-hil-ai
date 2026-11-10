#include "hil_protocol.h"

#include <string.h>

static uint16_t read_u16_le(const uint8_t *data)
{
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8U));
}

static void write_u16_le(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)(value & 0xFFU);
    data[1] = (uint8_t)((value >> 8U) & 0xFFU);
}

uint16_t hil_crc16_ccitt_false(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFFU;
    size_t index;
    uint8_t bit;

    if ((data == NULL) && (length != 0U)) {
        return 0U;
    }

    for (index = 0U; index < length; ++index) {
        crc ^= (uint16_t)((uint16_t)data[index] << 8U);
        for (bit = 0U; bit < 8U; ++bit) {
            if ((crc & 0x8000U) != 0U) {
                crc = (uint16_t)((crc << 1U) ^ 0x1021U);
            } else {
                crc = (uint16_t)(crc << 1U);
            }
        }
    }

    return crc;
}

hil_codec_status_t hil_frame_encode(const hil_frame_t *frame,
                                    uint8_t *output,
                                    size_t output_capacity,
                                    size_t *output_length)
{
    size_t total_length;
    uint16_t crc;

    if (output_length == NULL) {
        return HIL_CODEC_NULL_ARGUMENT;
    }
    *output_length = 0U;

    if ((frame == NULL) || (output == NULL)) {
        return HIL_CODEC_NULL_ARGUMENT;
    }
    if (frame->version != HIL_PROTOCOL_VERSION) {
        return HIL_CODEC_UNSUPPORTED_VERSION;
    }
    if (frame->payload_length > HIL_PROTOCOL_MAX_PAYLOAD) {
        return HIL_CODEC_PAYLOAD_TOO_LARGE;
    }
    if ((frame->payload == NULL) && (frame->payload_length != 0U)) {
        return HIL_CODEC_NULL_ARGUMENT;
    }

    total_length = HIL_PROTOCOL_OVERHEAD + (size_t)frame->payload_length;
    if (output_capacity < total_length) {
        return HIL_CODEC_OUTPUT_TOO_SMALL;
    }

    output[0] = HIL_SOF_BYTE_0;
    output[1] = HIL_SOF_BYTE_1;
    output[2] = frame->version;
    output[3] = frame->message_type;
    write_u16_le(&output[4], frame->sequence);
    write_u16_le(&output[6], frame->payload_length);

    if (frame->payload_length != 0U) {
        (void)memcpy(&output[8], frame->payload, frame->payload_length);
    }

    /* CRC covers Version through the final payload byte, excluding SOF. */
    crc = hil_crc16_ccitt_false(&output[2],
                                6U + (size_t)frame->payload_length);
    write_u16_le(&output[8U + frame->payload_length], crc);

    *output_length = total_length;
    return HIL_CODEC_OK;
}

void hil_stream_parser_init(hil_stream_parser_t *parser)
{
    if (parser != NULL) {
        (void)memset(parser, 0, sizeof(*parser));
    }
}

static void parser_consume_prefix(hil_stream_parser_t *parser, uint16_t count)
{
    if (count == 0U) {
        return;
    }

    if (count >= parser->used) {
        parser->used = 0U;
    } else {
        (void)memmove(parser->buffer,
                      &parser->buffer[count],
                      (size_t)(parser->used - count));
        parser->used = (uint16_t)(parser->used - count);
    }
}

static void parser_discard_prefix(hil_stream_parser_t *parser, uint16_t count)
{
    parser_consume_prefix(parser, count);
    parser->discarded_bytes += count;
}

/* Locate SOF. Preserve a trailing A5 because the next byte may be 5A. */
static int find_sof(const uint8_t *buffer, uint16_t used, uint16_t *position)
{
    uint16_t index;

    for (index = 0U; (uint16_t)(index + 1U) < used; ++index) {
        if ((buffer[index] == HIL_SOF_BYTE_0) &&
            (buffer[index + 1U] == HIL_SOF_BYTE_1)) {
            *position = index;
            return 1;
        }
    }

    return 0;
}

static void parser_process_available(hil_stream_parser_t *parser,
                                     hil_frame_callback_t callback,
                                     void *context)
{
    uint16_t sof_position;

    for (;;) {
        uint16_t payload_length;
        uint16_t expected_crc;
        uint16_t actual_crc;
        uint16_t total_length;
        hil_frame_view_t frame_view;

        if (parser->used < 2U) {
            return;
        }

        if (find_sof(parser->buffer, parser->used, &sof_position) == 0) {
            if (parser->buffer[parser->used - 1U] == HIL_SOF_BYTE_0) {
                parser_discard_prefix(parser, (uint16_t)(parser->used - 1U));
            } else {
                parser_discard_prefix(parser, parser->used);
            }
            return;
        }

        if (sof_position != 0U) {
            parser_discard_prefix(parser, sof_position);
        }

        /* SOF (2) + fixed header (6) are required before length validation. */
        if (parser->used < 8U) {
            return;
        }

        if (parser->buffer[2] != HIL_PROTOCOL_VERSION) {
            ++parser->invalid_versions;
            parser_discard_prefix(parser, 1U);
            continue;
        }

        payload_length = read_u16_le(&parser->buffer[6]);
        if (payload_length > HIL_PROTOCOL_MAX_PAYLOAD) {
            ++parser->invalid_lengths;
            parser_discard_prefix(parser, 1U);
            continue;
        }

        total_length = (uint16_t)(HIL_PROTOCOL_OVERHEAD + payload_length);
        if (parser->used < total_length) {
            return;
        }

        expected_crc = read_u16_le(&parser->buffer[8U + payload_length]);
        actual_crc = hil_crc16_ccitt_false(&parser->buffer[2],
                                           6U + (size_t)payload_length);
        if (actual_crc != expected_crc) {
            ++parser->invalid_crcs;
            parser_discard_prefix(parser, 1U);
            continue;
        }

        frame_view.version = parser->buffer[2];
        frame_view.message_type = parser->buffer[3];
        frame_view.sequence = read_u16_le(&parser->buffer[4]);
        frame_view.payload_length = payload_length;
        frame_view.payload = &parser->buffer[8];

        ++parser->frames_ok;
        if (callback != NULL) {
            callback(&frame_view, context);
        }

        parser_consume_prefix(parser, total_length);
    }
}

void hil_stream_parser_feed(hil_stream_parser_t *parser,
                            const uint8_t *data,
                            size_t length,
                            hil_frame_callback_t callback,
                            void *context)
{
    size_t index;

    if ((parser == NULL) || ((data == NULL) && (length != 0U))) {
        return;
    }

    for (index = 0U; index < length; ++index) {
        /* Normally parsing after every byte keeps this buffer below capacity. */
        if (parser->used >= HIL_PROTOCOL_MAX_FRAME) {
            parser_discard_prefix(parser, 1U);
        }

        parser->buffer[parser->used] = data[index];
        ++parser->used;
        parser_process_available(parser, callback, context);
    }
}
