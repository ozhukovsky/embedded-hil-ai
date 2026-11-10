#ifndef HIL_PROTOCOL_RELIABILITY_H
#define HIL_PROTOCOL_RELIABILITY_H

#include "hil_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HIL_COMMAND_CACHE_PAYLOAD_MAX HIL_PROTOCOL_MAX_PAYLOAD
#define HIL_COMMAND_CACHE_RESPONSE_MAX 2U

/* One instance per independent transmit link. First allocated sequence is 0. */
typedef struct {
  uint16_t next_sequence;
} hil_tx_sequence_t;

hil_codec_status_t hil_tx_sequence_init(hil_tx_sequence_t *counter);
hil_codec_status_t hil_tx_sequence_take(hil_tx_sequence_t *counter,
                                        uint16_t *sequence);

/*
 * One instance per receive link, observing every valid frame on that link.
 * last_sequence records the actual latest observation; sequence_anchor keeps
 * forward progress for modular gap/out-of-order classification. Gaps are only
 * diagnostic and never cause a frame to be rejected.
 */
typedef struct {
  uint8_t initialized;
  uint16_t last_sequence;
  uint16_t sequence_anchor;
  uint32_t frames_observed;
  uint32_t gap_events;
  uint32_t missing_frames;
  uint32_t duplicate_frames;
  uint32_t out_of_order_frames;
} hil_rx_sequence_monitor_t;

typedef struct {
  uint8_t first_frame;
  uint8_t gap_detected;
  uint8_t duplicate_sequence;
  uint8_t out_of_order;
  uint16_t sequence;
  uint16_t missing_count;
  uint16_t last_sequence;
} hil_sequence_observation_t;

hil_codec_status_t
hil_rx_sequence_monitor_init(hil_rx_sequence_monitor_t *monitor);
hil_codec_status_t
hil_rx_sequence_observe(hil_rx_sequence_monitor_t *monitor, uint16_t sequence,
                        hil_sequence_observation_t *observation);

typedef enum {
  HIL_COMMAND_NEW = 0,
  HIL_COMMAND_DUPLICATE,
  HIL_COMMAND_SEQUENCE_CONFLICT
} hil_command_result_t;

/* Input form for a logical response before it is copied into the cache. */
typedef struct {
  uint8_t message_type;
  uint16_t payload_length;
  const uint8_t *payload;
} hil_response_message_t;

/* Owning, fixed-size response stored for replay. */
typedef struct {
  uint8_t message_type;
  uint16_t payload_length;
  uint8_t payload[HIL_PROTOCOL_MAX_PAYLOAD];
} hil_cached_response_t;

/* Fixed-memory cache of the most recently accepted command and its responses.
 */
typedef struct {
  uint8_t valid;
  uint16_t sequence;
  uint8_t message_type;
  uint16_t payload_length;
  uint8_t payload[HIL_COMMAND_CACHE_PAYLOAD_MAX];
  uint8_t response_count;
  hil_cached_response_t responses[HIL_COMMAND_CACHE_RESPONSE_MAX];
} hil_command_cache_t;

hil_codec_status_t hil_command_cache_init(hil_command_cache_t *cache);

/*
 * Inspect a command without changing the cache. Only CONFIG_SET, ARM_TEST,
 * START_TEST and STOP_TEST are cacheable. STATUS_REQUEST always returns NEW
 * and never displaces the last accepted state-changing command.
 */
hil_codec_status_t
hil_command_cache_check(const hil_command_cache_t *cache, uint16_t sequence,
                        uint8_t message_type, const uint8_t *payload,
                        uint16_t payload_length, hil_command_result_t *result);

/*
 * Commit only after application validation and successful application. Store
 * zero, one, or two logical responses for duplicate replay. A status request
 * is a no-op. Rejected commands must not be committed.
 */
hil_codec_status_t hil_command_cache_commit(
    hil_command_cache_t *cache, uint16_t sequence, uint8_t message_type,
    const uint8_t *payload, uint16_t payload_length,
    const hil_response_message_t *responses, uint8_t response_count);

/* Copy one cached logical response; caller assigns a fresh frame sequence. */
hil_codec_status_t
hil_command_cache_get_response(const hil_command_cache_t *cache,
                               uint8_t response_index,
                               hil_cached_response_t *response);

#ifdef __cplusplus
}
#endif

#endif /* HIL_PROTOCOL_RELIABILITY_H */
