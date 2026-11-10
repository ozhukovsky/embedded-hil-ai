#ifndef HIL_PROTOCOL_H
#define HIL_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* HIL PC Communication Protocol V1.0 framing constants. */
#define HIL_PROTOCOL_VERSION       0x01U
#define HIL_SOF_BYTE_0             0xA5U
#define HIL_SOF_BYTE_1             0x5AU
#define HIL_PROTOCOL_MAX_PAYLOAD   64U
#define HIL_PROTOCOL_OVERHEAD      10U
#define HIL_PROTOCOL_MAX_FRAME    (HIL_PROTOCOL_OVERHEAD + HIL_PROTOCOL_MAX_PAYLOAD)

/* Protocol V1.0 message identifiers. */
typedef enum {
    HIL_MSG_BOOT_REPORT   = 0x01U,
    HIL_MSG_STATUS        = 0x02U,
    HIL_MSG_HEARTBEAT     = 0x03U,
    HIL_MSG_STATUS_REQUEST= 0x04U,
    HIL_MSG_CONFIG_SET    = 0x10U,
    HIL_MSG_ARM_TEST      = 0x11U,
    HIL_MSG_START_TEST    = 0x12U,
    HIL_MSG_STOP_TEST     = 0x13U,
    HIL_MSG_ACK           = 0x20U,
    HIL_MSG_NACK          = 0x21U,
    HIL_MSG_ARMED         = 0x22U,
    HIL_MSG_TELEMETRY     = 0x30U,
    HIL_MSG_FAULT_REPORT  = 0x31U,
    HIL_MSG_TEST_COMPLETE = 0x32U
} hil_message_type_t;

typedef enum {
    HIL_CODEC_OK = 0,
    HIL_CODEC_NULL_ARGUMENT,
    HIL_CODEC_UNSUPPORTED_VERSION,
    HIL_CODEC_PAYLOAD_TOO_LARGE,
    HIL_CODEC_OUTPUT_TOO_SMALL,
    HIL_CODEC_INVALID_LENGTH,
    HIL_CODEC_INVALID_VALUE
} hil_codec_status_t;

/*
 * Frame representation used by the encoder. Payload storage belongs to the
 * caller and must remain valid for the duration of hil_frame_encode().
 */
typedef struct {
    uint8_t version;
    uint8_t message_type;
    uint16_t sequence;
    uint16_t payload_length;
    const uint8_t *payload;
} hil_frame_t;

/*
 * Non-owning frame view delivered by the stream parser.
 *
 * IMPORTANT: payload points into the parser's internal buffer and is valid
 * only during the callback. Copy it if it must be retained after the callback.
 */
typedef struct {
    uint8_t version;
    uint8_t message_type;
    uint16_t sequence;
    uint16_t payload_length;
    const uint8_t *payload;
} hil_frame_view_t;

typedef void (*hil_frame_callback_t)(const hil_frame_view_t *frame,
                                     void *context);

/*
 * Fixed-memory incremental parser. One parser instance belongs to one
 * transport stream and must be fed from one execution context (normally the
 * main loop, after bytes have been removed from a UART/USB RX buffer).
 */
typedef struct {
    uint8_t buffer[HIL_PROTOCOL_MAX_FRAME];
    uint16_t used;
    uint32_t frames_ok;
    uint32_t discarded_bytes;
    uint32_t invalid_versions;
    uint32_t invalid_lengths;
    uint32_t invalid_crcs;
} hil_stream_parser_t;

/* CRC-16/CCITT-FALSE: poly=0x1021, init=0xFFFF, refin/refout=false, xorout=0. */
uint16_t hil_crc16_ccitt_false(const uint8_t *data, size_t length);

/*
 * Encode one Protocol V1.0 frame into caller-owned storage.
 * Returns HIL_CODEC_OK on success; *output_length is zero on every failure.
 */
hil_codec_status_t hil_frame_encode(const hil_frame_t *frame,
                                    uint8_t *output,
                                    size_t output_capacity,
                                    size_t *output_length);

void hil_stream_parser_init(hil_stream_parser_t *parser);

/*
 * Feed any number of bytes. Handles arbitrary fragmentation and concatenated
 * frames. Invalid candidates are discarded and the parser searches for the
 * next SOF. A correctly framed payload may contain A5 5A without resync.
 */
void hil_stream_parser_feed(hil_stream_parser_t *parser,
                            const uint8_t *data,
                            size_t length,
                            hil_frame_callback_t callback,
                            void *context);

#ifdef __cplusplus
}
#endif

#endif /* HIL_PROTOCOL_H */
