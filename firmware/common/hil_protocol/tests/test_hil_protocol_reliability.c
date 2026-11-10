#include "hil_protocol_messages.h"
#include "hil_protocol_reliability.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      (void)fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,            \
                    #condition);                                               \
      return 1;                                                                \
    }                                                                          \
  } while (0)

static int test_tx_sequence_counters(void) {
  hil_tx_sequence_t a, b;
  uint16_t sequence;

  CHECK(hil_tx_sequence_init(&a) == HIL_CODEC_OK);
  CHECK(hil_tx_sequence_init(&b) == HIL_CODEC_OK);
  CHECK(hil_tx_sequence_take(&a, &sequence) == HIL_CODEC_OK && sequence == 0U);
  CHECK(hil_tx_sequence_take(&a, &sequence) == HIL_CODEC_OK && sequence == 1U);
  CHECK(hil_tx_sequence_take(&b, &sequence) == HIL_CODEC_OK && sequence == 0U);
  a.next_sequence = 0xFFFFU;
  CHECK(hil_tx_sequence_take(&a, &sequence) == HIL_CODEC_OK &&
        sequence == 0xFFFFU);
  CHECK(a.next_sequence == 0U);
  CHECK(hil_tx_sequence_take(&a, &sequence) == HIL_CODEC_OK && sequence == 0U);
  CHECK(hil_tx_sequence_take(NULL, &sequence) == HIL_CODEC_NULL_ARGUMENT);
  CHECK(hil_tx_sequence_take(&a, NULL) == HIL_CODEC_NULL_ARGUMENT);
  return 0;
}

static int test_rx_sequence_monitor(void) {
  hil_rx_sequence_monitor_t monitor;
  hil_sequence_observation_t observation;

  CHECK(hil_rx_sequence_monitor_init(&monitor) == HIL_CODEC_OK);
  CHECK(hil_rx_sequence_observe(&monitor, 10U, &observation) == HIL_CODEC_OK);
  CHECK(observation.first_frame == 1U && observation.gap_detected == 0U);
  CHECK(observation.last_sequence == 10U);
  CHECK(hil_rx_sequence_observe(&monitor, 11U, &observation) == HIL_CODEC_OK);
  CHECK(observation.gap_detected == 0U && observation.last_sequence == 11U);
  CHECK(hil_rx_sequence_observe(&monitor, 14U, &observation) == HIL_CODEC_OK);
  CHECK(observation.gap_detected == 1U && observation.missing_count == 2U);
  CHECK(observation.last_sequence == 14U);
  CHECK(monitor.gap_events == 1U && monitor.missing_frames == 2U);
  CHECK(hil_rx_sequence_observe(&monitor, 14U, &observation) == HIL_CODEC_OK);
  CHECK(observation.duplicate_sequence == 1U && observation.gap_detected == 0U);
  CHECK(monitor.duplicate_frames == 1U && monitor.last_sequence == 14U);
  CHECK(hil_rx_sequence_observe(&monitor, 13U, &observation) == HIL_CODEC_OK);
  CHECK(observation.out_of_order == 1U && observation.gap_detected == 0U);
  CHECK(monitor.out_of_order_frames == 1U && monitor.last_sequence == 13U);
  CHECK(monitor.sequence_anchor == 14U);

  CHECK(hil_rx_sequence_monitor_init(&monitor) == HIL_CODEC_OK);
  CHECK(hil_rx_sequence_observe(&monitor, 0xFFFFU, &observation) ==
        HIL_CODEC_OK);
  CHECK(hil_rx_sequence_observe(&monitor, 0U, &observation) == HIL_CODEC_OK);
  CHECK(observation.gap_detected == 0U && observation.last_sequence == 0U);
  CHECK(hil_rx_sequence_observe(NULL, 0U, &observation) ==
        HIL_CODEC_NULL_ARGUMENT);
  CHECK(hil_rx_sequence_observe(&monitor, 0U, NULL) == HIL_CODEC_NULL_ARGUMENT);
  return 0;
}

static int test_command_cache_classification(void) {
  hil_command_cache_t cache;
  hil_command_result_t result;
  static const uint8_t p1[] = {0x10U, 0x20U, 0x30U};
  static const uint8_t p2[] = {0x10U, 0x20U, 0x31U};
  static const uint8_t p3[] = {0x10U, 0x20U};

  CHECK(hil_command_cache_init(&cache) == HIL_CODEC_OK);
  CHECK(hil_command_cache_check(&cache, 7U, HIL_MSG_CONFIG_SET, p1, sizeof(p1),
                                &result) == HIL_CODEC_OK);
  CHECK(result == HIL_COMMAND_NEW);
  CHECK(hil_command_cache_commit(&cache, 7U, HIL_MSG_CONFIG_SET, p1, sizeof(p1),
                                 NULL, 0U) == HIL_CODEC_OK);
  CHECK(hil_command_cache_check(&cache, 7U, HIL_MSG_CONFIG_SET, p1, sizeof(p1),
                                &result) == HIL_CODEC_OK);
  CHECK(result == HIL_COMMAND_DUPLICATE);
  CHECK(hil_command_cache_check(&cache, 7U, HIL_MSG_ARM_TEST, p1, sizeof(p1),
                                &result) == HIL_CODEC_OK);
  CHECK(result == HIL_COMMAND_SEQUENCE_CONFLICT);
  CHECK(hil_command_cache_check(&cache, 7U, HIL_MSG_CONFIG_SET, p2, sizeof(p2),
                                &result) == HIL_CODEC_OK);
  CHECK(result == HIL_COMMAND_SEQUENCE_CONFLICT);
  CHECK(hil_command_cache_check(&cache, 7U, HIL_MSG_CONFIG_SET, p3, sizeof(p3),
                                &result) == HIL_CODEC_OK);
  CHECK(result == HIL_COMMAND_SEQUENCE_CONFLICT);
  CHECK(hil_command_cache_check(&cache, 8U, HIL_MSG_START_TEST, p1, sizeof(p1),
                                &result) == HIL_CODEC_OK);
  CHECK(result == HIL_COMMAND_NEW);
  CHECK(hil_command_cache_commit(&cache, 8U, HIL_MSG_START_TEST, p1, sizeof(p1),
                                 NULL, 0U) == HIL_CODEC_OK);
  CHECK(hil_command_cache_check(&cache, 7U, HIL_MSG_CONFIG_SET, p1, sizeof(p1),
                                &result) == HIL_CODEC_OK);
  CHECK(result == HIL_COMMAND_NEW);
  CHECK(hil_command_cache_check(&cache, 9U, HIL_MSG_CONFIG_SET, NULL, 1U,
                                &result) == HIL_CODEC_NULL_ARGUMENT);
  CHECK(hil_command_cache_check(&cache, 9U, HIL_MSG_CONFIG_SET, p1, 65U,
                                &result) == HIL_CODEC_PAYLOAD_TOO_LARGE);
  CHECK(hil_command_cache_check(&cache, 9U, HIL_MSG_STATUS_REQUEST, NULL, 0U,
                                &result) == HIL_CODEC_OK);
  CHECK(result == HIL_COMMAND_NEW);
  CHECK(hil_command_cache_check(&cache, 9U, HIL_MSG_STATUS_REQUEST, p1, 1U,
                                &result) == HIL_CODEC_INVALID_LENGTH);
  return 0;
}

static int test_rejected_command_and_status_request_preserve_cache(void) {
  hil_command_cache_t cache;
  hil_command_result_t result;
  static const uint8_t accepted[] = {0xAAU, 0x55U};
  static const uint8_t rejected[] = {0x12U};

  CHECK(hil_command_cache_init(&cache) == HIL_CODEC_OK);
  CHECK(hil_command_cache_commit(&cache, 40U, HIL_MSG_CONFIG_SET, accepted,
                                 sizeof(accepted), NULL, 0U) == HIL_CODEC_OK);

  /* Application rejects sequence 41: it checks, but deliberately does not
   * commit. */
  CHECK(hil_command_cache_check(&cache, 41U, HIL_MSG_ARM_TEST, rejected,
                                sizeof(rejected), &result) == HIL_CODEC_OK);
  CHECK(result == HIL_COMMAND_NEW);
  CHECK(hil_command_cache_check(&cache, 40U, HIL_MSG_CONFIG_SET, accepted,
                                sizeof(accepted), &result) == HIL_CODEC_OK);
  CHECK(result == HIL_COMMAND_DUPLICATE);

  CHECK(hil_command_cache_check(&cache, 40U, HIL_MSG_STATUS_REQUEST, NULL, 0U,
                                &result) == HIL_CODEC_OK);
  CHECK(result == HIL_COMMAND_NEW);
  CHECK(hil_command_cache_commit(&cache, 40U, HIL_MSG_STATUS_REQUEST, NULL, 0U,
                                 NULL, 0U) == HIL_CODEC_OK);
  CHECK(hil_command_cache_check(&cache, 40U, HIL_MSG_CONFIG_SET, accepted,
                                sizeof(accepted), &result) == HIL_CODEC_OK);
  CHECK(result == HIL_COMMAND_DUPLICATE);
  return 0;
}

static int test_response_replay_and_fresh_frame_sequences(void) {
  hil_command_cache_t cache;
  hil_command_result_t result;
  hil_cached_response_t replay;
  hil_tx_sequence_t tx;
  hil_frame_t frame;
  uint8_t encoded[HIL_PROTOCOL_MAX_FRAME];
  size_t encoded_length;
  uint16_t sequence;
  uint8_t i;
  static const uint8_t arm_payload[] = {HIL_ROLE_DUT, 0x78U, 0x56U, 0x34U,
                                        0x12U};
  static const uint8_t ack_payload[] = {0x34U,        0x12U, HIL_MSG_ARM_TEST,
                                        HIL_ROLE_DUT, 0U,    0U};
  static const uint8_t armed_payload[] = {HIL_ROLE_DUT, 0x78U, 0x56U, 0x34U,
                                          0x12U,        0x1FU, 0U};
  static const uint8_t stop_payload[] = {HIL_ROLE_DUT, 1U, 0U, 0U, 0U, 0U};
  const hil_response_message_t single[] = {
      {HIL_MSG_ACK, sizeof(ack_payload), ack_payload}};
  const hil_response_message_t pair[] = {
      {HIL_MSG_ACK, sizeof(ack_payload), ack_payload},
      {HIL_MSG_ARMED, sizeof(armed_payload), armed_payload}};

  CHECK(hil_command_cache_init(&cache) == HIL_CODEC_OK);
  CHECK(hil_command_cache_commit(&cache, 100U, HIL_MSG_STOP_TEST, stop_payload,
                                 sizeof(stop_payload), single,
                                 1U) == HIL_CODEC_OK);
  CHECK(hil_command_cache_check(&cache, 100U, HIL_MSG_STOP_TEST, stop_payload,
                                sizeof(stop_payload), &result) == HIL_CODEC_OK);
  CHECK(result == HIL_COMMAND_DUPLICATE);
  CHECK(cache.response_count == 1U);
  CHECK(hil_command_cache_get_response(&cache, 0U, &replay) == HIL_CODEC_OK);
  CHECK(replay.message_type == HIL_MSG_ACK &&
        replay.payload_length == sizeof(ack_payload));
  CHECK(memcmp(replay.payload, ack_payload, sizeof(ack_payload)) == 0);

  CHECK(hil_command_cache_commit(&cache, 101U, HIL_MSG_ARM_TEST, arm_payload,
                                 sizeof(arm_payload), pair,
                                 2U) == HIL_CODEC_OK);
  CHECK(hil_command_cache_check(&cache, 101U, HIL_MSG_ARM_TEST, arm_payload,
                                sizeof(arm_payload), &result) == HIL_CODEC_OK);
  CHECK(result == HIL_COMMAND_DUPLICATE && cache.response_count == 2U);

  /* Every replayed logical response is wrapped in a newly sequenced frame. */
  CHECK(hil_tx_sequence_init(&tx) == HIL_CODEC_OK);
  for (i = 0U; i < 2U; ++i) {
    CHECK(hil_command_cache_get_response(&cache, i, &replay) == HIL_CODEC_OK);
    CHECK(hil_tx_sequence_take(&tx, &sequence) == HIL_CODEC_OK);
    frame.version = HIL_PROTOCOL_VERSION;
    frame.message_type = replay.message_type;
    frame.sequence = sequence;
    frame.payload_length = replay.payload_length;
    frame.payload = replay.payload;
    CHECK(hil_frame_encode(&frame, encoded, sizeof(encoded), &encoded_length) ==
          HIL_CODEC_OK);
    CHECK(encoded_length == HIL_PROTOCOL_OVERHEAD + replay.payload_length);
    CHECK(encoded[4] == (uint8_t)sequence &&
          encoded[5] == (uint8_t)(sequence >> 8U));
    CHECK(encoded[3] == replay.message_type);
  }
  /* A second identical duplicate replays both responses with fresh numbers. */
  CHECK(hil_command_cache_check(&cache, 101U, HIL_MSG_ARM_TEST, arm_payload,
                                sizeof(arm_payload), &result) == HIL_CODEC_OK);
  CHECK(result == HIL_COMMAND_DUPLICATE);
  for (i = 0U; i < 2U; ++i) {
    CHECK(hil_command_cache_get_response(&cache, i, &replay) == HIL_CODEC_OK);
    CHECK(hil_tx_sequence_take(&tx, &sequence) == HIL_CODEC_OK);
    CHECK(sequence == (uint16_t)(i + 2U));
    frame.version = HIL_PROTOCOL_VERSION;
    frame.message_type = replay.message_type;
    frame.sequence = sequence;
    frame.payload_length = replay.payload_length;
    frame.payload = replay.payload;
    CHECK(hil_frame_encode(&frame, encoded, sizeof(encoded), &encoded_length) ==
          HIL_CODEC_OK);
    CHECK(encoded[4] == (uint8_t)sequence &&
          encoded[5] == (uint8_t)(sequence >> 8U));
  }
  CHECK(hil_command_cache_get_response(&cache, 2U, &replay) ==
        HIL_CODEC_INVALID_VALUE);
  CHECK(hil_command_cache_commit(&cache, 102U, HIL_MSG_CONFIG_SET, NULL, 0U,
                                 pair, 3U) == HIL_CODEC_INVALID_VALUE);
  CHECK(hil_command_cache_check(&cache, 101U, HIL_MSG_ARM_TEST, arm_payload,
                                sizeof(arm_payload), &result) == HIL_CODEC_OK);
  CHECK(result == HIL_COMMAND_DUPLICATE && cache.response_count == 2U);
  return 0;
}

int main(void) {
  CHECK(test_tx_sequence_counters() == 0);
  CHECK(test_rx_sequence_monitor() == 0);
  CHECK(test_command_cache_classification() == 0);
  CHECK(test_rejected_command_and_status_request_preserve_cache() == 0);
  CHECK(test_response_replay_and_fresh_frame_sequences() == 0);
  (void)printf("PASS: reliability tests; TX=%u B RX=%u B cache=%u B\n",
               (unsigned)sizeof(hil_tx_sequence_t),
               (unsigned)sizeof(hil_rx_sequence_monitor_t),
               (unsigned)sizeof(hil_command_cache_t));
  return 0;
}
