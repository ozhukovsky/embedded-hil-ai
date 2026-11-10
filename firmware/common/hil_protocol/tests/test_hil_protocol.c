#include "hil_protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            (void)fprintf(stderr, "FAIL %s:%d: %s\n",                         \
                          __FILE__, __LINE__, #condition);                     \
            return 1;                                                           \
        }                                                                       \
    } while (0)

typedef struct {
    unsigned count;
    hil_frame_view_t frames[8];
    uint8_t payload_copy[8][HIL_PROTOCOL_MAX_PAYLOAD];
} capture_t;

static void capture_frame(const hil_frame_view_t *frame, void *context)
{
    capture_t *capture = (capture_t *)context;
    unsigned slot;

    if ((frame == NULL) || (capture == NULL) || (capture->count >= 8U)) {
        return;
    }

    slot = capture->count;
    capture->frames[slot] = *frame;
    if (frame->payload_length != 0U) {
        (void)memcpy(capture->payload_copy[slot],
                     frame->payload,
                     frame->payload_length);
    }
    capture->frames[slot].payload = capture->payload_copy[slot];
    ++capture->count;
}

static void make_frame(hil_frame_t *frame,
                       uint8_t version,
                       uint8_t message_type,
                       uint16_t sequence,
                       const uint8_t *payload,
                       uint16_t payload_length)
{
    (void)memset(frame, 0, sizeof(*frame));
    frame->version = version;
    frame->message_type = message_type;
    frame->sequence = sequence;
    frame->payload_length = payload_length;
    frame->payload = payload;
}

/* Test-only helper: can construct malformed-version and malformed-length frames. */
static size_t make_raw_frame(uint8_t version,
                             uint8_t message_type,
                             uint16_t sequence,
                             uint16_t declared_length,
                             const uint8_t *payload,
                             size_t payload_bytes,
                             uint8_t *output)
{
    uint16_t crc;
    size_t index;

    output[0] = HIL_SOF_BYTE_0;
    output[1] = HIL_SOF_BYTE_1;
    output[2] = version;
    output[3] = message_type;
    output[4] = (uint8_t)(sequence & 0xFFU);
    output[5] = (uint8_t)((sequence >> 8U) & 0xFFU);
    output[6] = (uint8_t)(declared_length & 0xFFU);
    output[7] = (uint8_t)((declared_length >> 8U) & 0xFFU);
    for (index = 0U; index < payload_bytes; ++index) {
        output[8U + index] = payload[index];
    }
    crc = hil_crc16_ccitt_false(&output[2], 6U + payload_bytes);
    output[8U + payload_bytes] = (uint8_t)(crc & 0xFFU);
    output[9U + payload_bytes] = (uint8_t)((crc >> 8U) & 0xFFU);
    return 10U + payload_bytes;
}

static int test_crc_vector(void)
{
    static const uint8_t check[] = "123456789";
    uint16_t crc = hil_crc16_ccitt_false(check, 9U);
    uint8_t wire_crc[2];

    CHECK(crc == 0x29B1U);
    wire_crc[0] = (uint8_t)(crc & 0xFFU);
    wire_crc[1] = (uint8_t)((crc >> 8U) & 0xFFU);
    CHECK(wire_crc[0] == 0xB1U && wire_crc[1] == 0x29U);
    return 0;
}

static int test_empty_status_request(void)
{
    hil_frame_t tx;
    hil_stream_parser_t parser;
    capture_t capture;
    uint8_t bytes[HIL_PROTOCOL_MAX_FRAME];
    size_t bytes_written = 0U;

    (void)memset(&capture, 0, sizeof(capture));
    make_frame(&tx, HIL_PROTOCOL_VERSION, HIL_MSG_STATUS_REQUEST,
               0x1234U, NULL, 0U);

    CHECK(hil_frame_encode(&tx, bytes, sizeof(bytes), &bytes_written) == HIL_CODEC_OK);
    CHECK(bytes_written == HIL_PROTOCOL_OVERHEAD);
    CHECK(bytes[0] == 0xA5U && bytes[1] == 0x5AU);
    CHECK(bytes[2] == 0x01U && bytes[3] == HIL_MSG_STATUS_REQUEST);
    CHECK(bytes[4] == 0x34U && bytes[5] == 0x12U);
    CHECK(bytes[6] == 0x00U && bytes[7] == 0x00U);

    hil_stream_parser_init(&parser);
    hil_stream_parser_feed(&parser, bytes, bytes_written, capture_frame, &capture);
    CHECK(capture.count == 1U);
    CHECK(capture.frames[0].version == HIL_PROTOCOL_VERSION);
    CHECK(capture.frames[0].message_type == HIL_MSG_STATUS_REQUEST);
    CHECK(capture.frames[0].sequence == 0x1234U);
    CHECK(capture.frames[0].payload_length == 0U);
    CHECK(parser.frames_ok == 1U);
    return 0;
}

static int test_fragmented_and_max_payload_with_embedded_sof(void)
{
    hil_frame_t tx;
    hil_stream_parser_t parser;
    capture_t capture;
    uint8_t bytes[HIL_PROTOCOL_MAX_FRAME];
    uint8_t payload[HIL_PROTOCOL_MAX_PAYLOAD];
    size_t bytes_written = 0U;
    unsigned index;

    (void)memset(&capture, 0, sizeof(capture));
    for (index = 0U; index < HIL_PROTOCOL_MAX_PAYLOAD; ++index) {
        payload[index] = (uint8_t)(index * 17U);
    }
    payload[10] = 0xA5U;
    payload[11] = 0x5AU;
    payload[12] = 0x00U;
    payload[13] = 0xFFU;

    make_frame(&tx, HIL_PROTOCOL_VERSION, HIL_MSG_TELEMETRY,
               0xFFFFU, payload, HIL_PROTOCOL_MAX_PAYLOAD);
    CHECK(hil_frame_encode(&tx, bytes, sizeof(bytes), &bytes_written) == HIL_CODEC_OK);
    CHECK(bytes_written == HIL_PROTOCOL_MAX_FRAME);

    hil_stream_parser_init(&parser);
    for (index = 0U; index < bytes_written; ++index) {
        hil_stream_parser_feed(&parser, &bytes[index], 1U, capture_frame, &capture);
    }

    CHECK(capture.count == 1U);
    CHECK(capture.frames[0].message_type == HIL_MSG_TELEMETRY);
    CHECK(capture.frames[0].sequence == 0xFFFFU);
    CHECK(capture.frames[0].payload_length == HIL_PROTOCOL_MAX_PAYLOAD);
    CHECK(memcmp(capture.frames[0].payload, payload, sizeof(payload)) == 0);
    CHECK(parser.invalid_crcs == 0U);
    return 0;
}

static int test_concatenated_frames_and_noise(void)
{
    hil_frame_t a;
    hil_frame_t b;
    hil_stream_parser_t parser;
    capture_t capture;
    uint8_t frame_a[HIL_PROTOCOL_MAX_FRAME];
    uint8_t frame_b[HIL_PROTOCOL_MAX_FRAME];
    uint8_t stream[4U + 2U * HIL_PROTOCOL_MAX_FRAME];
    size_t len_a = 0U;
    size_t len_b = 0U;

    (void)memset(&capture, 0, sizeof(capture));
    make_frame(&a, HIL_PROTOCOL_VERSION, HIL_MSG_STATUS_REQUEST,
               7U, NULL, 0U);
    make_frame(&b, HIL_PROTOCOL_VERSION, HIL_MSG_HEARTBEAT,
               8U, (const uint8_t[]){1U, 2U, 3U}, 3U);
    CHECK(hil_frame_encode(&a, frame_a, sizeof(frame_a), &len_a) == HIL_CODEC_OK);
    CHECK(hil_frame_encode(&b, frame_b, sizeof(frame_b), &len_b) == HIL_CODEC_OK);

    stream[0] = 0x00U;
    stream[1] = HIL_SOF_BYTE_0;
    stream[2] = 0x00U;
    stream[3] = HIL_SOF_BYTE_0; /* trailing A5 must join the next byte 5A */
    (void)memcpy(&stream[4], frame_a, len_a);
    (void)memcpy(&stream[4U + len_a], frame_b, len_b);

    hil_stream_parser_init(&parser);
    hil_stream_parser_feed(&parser, stream, 4U + len_a + len_b,
                           capture_frame, &capture);
    CHECK(capture.count == 2U);
    CHECK(capture.frames[0].sequence == 7U);
    CHECK(capture.frames[1].sequence == 8U);
    CHECK(capture.frames[1].message_type == HIL_MSG_HEARTBEAT);
    CHECK(capture.frames[1].payload_length == 3U);
    CHECK(parser.frames_ok == 2U);
    CHECK(parser.discarded_bytes == 4U);
    return 0;
}

static int test_invalid_candidates_resynchronize(void)
{
    hil_frame_t good;
    hil_stream_parser_t parser;
    capture_t capture;
    uint8_t stream[128];
    uint8_t good_bytes[HIL_PROTOCOL_MAX_FRAME];
    uint8_t bad_version[HIL_PROTOCOL_OVERHEAD];
    uint8_t bad_length[HIL_PROTOCOL_OVERHEAD];
    uint8_t bad_crc[HIL_PROTOCOL_OVERHEAD];
    size_t good_length = 0U;
    size_t bad_version_length;
    size_t bad_length_length;
    size_t bad_crc_length;
    size_t offset = 0U;

    (void)memset(&capture, 0, sizeof(capture));
    make_frame(&good, HIL_PROTOCOL_VERSION, HIL_MSG_STATUS_REQUEST,
               99U, NULL, 0U);
    CHECK(hil_frame_encode(&good, good_bytes, sizeof(good_bytes), &good_length) == HIL_CODEC_OK);

    bad_version_length = make_raw_frame(0x02U, HIL_MSG_STATUS_REQUEST,
                                        1U, 0U, NULL, 0U, bad_version);
    bad_length_length = make_raw_frame(HIL_PROTOCOL_VERSION, HIL_MSG_STATUS_REQUEST,
                                       2U, 65U, NULL, 0U, bad_length);
    bad_crc_length = make_raw_frame(HIL_PROTOCOL_VERSION, HIL_MSG_STATUS_REQUEST,
                                    3U, 0U, NULL, 0U, bad_crc);
    bad_crc[8] ^= 0x01U;

    (void)memcpy(&stream[offset], bad_version, bad_version_length); offset += bad_version_length;
    (void)memcpy(&stream[offset], bad_length, bad_length_length); offset += bad_length_length;
    (void)memcpy(&stream[offset], bad_crc, bad_crc_length); offset += bad_crc_length;
    (void)memcpy(&stream[offset], good_bytes, good_length); offset += good_length;

    hil_stream_parser_init(&parser);
    hil_stream_parser_feed(&parser, stream, offset, capture_frame, &capture);

    CHECK(capture.count == 1U);
    CHECK(capture.frames[0].sequence == 99U);
    CHECK(parser.invalid_versions == 1U);
    CHECK(parser.invalid_lengths == 1U);
    CHECK(parser.invalid_crcs == 1U);
    CHECK(parser.frames_ok == 1U);
    return 0;
}

static int test_encoder_rejects_invalid_arguments(void)
{
    hil_frame_t frame;
    uint8_t output[HIL_PROTOCOL_MAX_FRAME];
    size_t output_length = 123U;

    make_frame(&frame, HIL_PROTOCOL_VERSION, HIL_MSG_STATUS_REQUEST,
               0U, NULL, 0U);
    CHECK(hil_frame_encode(&frame, output, HIL_PROTOCOL_OVERHEAD - 1U,
                           &output_length) == HIL_CODEC_OUTPUT_TOO_SMALL);
    CHECK(output_length == 0U);

    frame.version = 2U;
    CHECK(hil_frame_encode(&frame, output, sizeof(output),
                           &output_length) == HIL_CODEC_UNSUPPORTED_VERSION);
    CHECK(output_length == 0U);

    frame.version = HIL_PROTOCOL_VERSION;
    frame.payload_length = HIL_PROTOCOL_MAX_PAYLOAD + 1U;
    CHECK(hil_frame_encode(&frame, output, sizeof(output),
                           &output_length) == HIL_CODEC_PAYLOAD_TOO_LARGE);
    CHECK(output_length == 0U);

    frame.payload_length = 1U;
    frame.payload = NULL;
    CHECK(hil_frame_encode(&frame, output, sizeof(output),
                           &output_length) == HIL_CODEC_NULL_ARGUMENT);
    CHECK(output_length == 0U);

    CHECK(hil_frame_encode(NULL, output, sizeof(output),
                           &output_length) == HIL_CODEC_NULL_ARGUMENT);
    CHECK(output_length == 0U);
    CHECK(hil_frame_encode(&frame, NULL, sizeof(output),
                           &output_length) == HIL_CODEC_NULL_ARGUMENT);
    CHECK(output_length == 0U);
    CHECK(hil_frame_encode(&frame, output, sizeof(output), NULL)
          == HIL_CODEC_NULL_ARGUMENT);
    return 0;
}

static int test_sequence_wraparound_arithmetic(void)
{
    uint16_t sequence = 0xFFFFU;
    sequence = (uint16_t)(sequence + 1U);
    CHECK(sequence == 0U);
    return 0;
}

int main(void)
{
    int result;

    result = test_crc_vector(); if (result != 0) return result;
    result = test_empty_status_request(); if (result != 0) return result;
    result = test_fragmented_and_max_payload_with_embedded_sof(); if (result != 0) return result;
    result = test_concatenated_frames_and_noise(); if (result != 0) return result;
    result = test_invalid_candidates_resynchronize(); if (result != 0) return result;
    result = test_encoder_rejects_invalid_arguments(); if (result != 0) return result;
    result = test_sequence_wraparound_arithmetic(); if (result != 0) return result;

    (void)printf("PASS: all HIL Protocol V1.0 framing tests\n");
    return 0;
}
