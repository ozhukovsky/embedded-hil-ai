#include "hil_protocol_reliability.h"

#include <string.h>

static int is_cacheable_command(uint8_t message_type) {
  return (message_type == HIL_MSG_CONFIG_SET) ||
         (message_type == HIL_MSG_ARM_TEST) ||
         (message_type == HIL_MSG_START_TEST) ||
         (message_type == HIL_MSG_STOP_TEST);
}

static hil_codec_status_t validate_payload(const uint8_t *payload,
                                           uint16_t payload_length) {
  if (payload_length > HIL_PROTOCOL_MAX_PAYLOAD) {
    return HIL_CODEC_PAYLOAD_TOO_LARGE;
  }
  if ((payload == NULL) && (payload_length != 0U)) {
    return HIL_CODEC_NULL_ARGUMENT;
  }
  return HIL_CODEC_OK;
}

hil_codec_status_t hil_tx_sequence_init(hil_tx_sequence_t *counter) {
  if (counter == NULL) {
    return HIL_CODEC_NULL_ARGUMENT;
  }
  counter->next_sequence = 0U;
  return HIL_CODEC_OK;
}

hil_codec_status_t hil_tx_sequence_take(hil_tx_sequence_t *counter,
                                        uint16_t *sequence) {
  if ((counter == NULL) || (sequence == NULL)) {
    return HIL_CODEC_NULL_ARGUMENT;
  }
  *sequence = counter->next_sequence;
  counter->next_sequence = (uint16_t)(counter->next_sequence + 1U);
  return HIL_CODEC_OK;
}

hil_codec_status_t
hil_rx_sequence_monitor_init(hil_rx_sequence_monitor_t *monitor) {
  if (monitor == NULL) {
    return HIL_CODEC_NULL_ARGUMENT;
  }
  (void)memset(monitor, 0, sizeof(*monitor));
  return HIL_CODEC_OK;
}

hil_codec_status_t
hil_rx_sequence_observe(hil_rx_sequence_monitor_t *monitor, uint16_t sequence,
                        hil_sequence_observation_t *observation) {
  uint16_t distance;

  if ((monitor == NULL) || (observation == NULL)) {
    return HIL_CODEC_NULL_ARGUMENT;
  }
  (void)memset(observation, 0, sizeof(*observation));
  observation->sequence = sequence;
  ++monitor->frames_observed;
  monitor->last_sequence = sequence;

  if (monitor->initialized == 0U) {
    monitor->initialized = 1U;
    monitor->sequence_anchor = sequence;
    observation->first_frame = 1U;
    observation->last_sequence = sequence;
    return HIL_CODEC_OK;
  }

  distance = (uint16_t)(sequence - monitor->sequence_anchor);
  if (distance == 0U) {
    observation->duplicate_sequence = 1U;
    observation->last_sequence = monitor->last_sequence;
    ++monitor->duplicate_frames;
    return HIL_CODEC_OK;
  }

  /* Modular distances below half-range represent forward progress. */
  if (distance < 0x8000U) {
    if (distance > 1U) {
      observation->gap_detected = 1U;
      observation->missing_count = (uint16_t)(distance - 1U);
      ++monitor->gap_events;
      monitor->missing_frames += observation->missing_count;
    }
    monitor->sequence_anchor = sequence;
  } else {
    observation->out_of_order = 1U;
    ++monitor->out_of_order_frames;
  }

  observation->last_sequence = monitor->last_sequence;
  return HIL_CODEC_OK;
}

hil_codec_status_t hil_command_cache_init(hil_command_cache_t *cache) {
  if (cache == NULL) {
    return HIL_CODEC_NULL_ARGUMENT;
  }
  (void)memset(cache, 0, sizeof(*cache));
  return HIL_CODEC_OK;
}

hil_codec_status_t
hil_command_cache_check(const hil_command_cache_t *cache, uint16_t sequence,
                        uint8_t message_type, const uint8_t *payload,
                        uint16_t payload_length, hil_command_result_t *result) {
  hil_codec_status_t status;

  if ((cache == NULL) || (result == NULL)) {
    return HIL_CODEC_NULL_ARGUMENT;
  }
  status = validate_payload(payload, payload_length);
  if (status != HIL_CODEC_OK) {
    return status;
  }
  if (message_type == HIL_MSG_STATUS_REQUEST) {
    if (payload_length != 0U) {
      return HIL_CODEC_INVALID_LENGTH;
    }
    *result = HIL_COMMAND_NEW;
    return HIL_CODEC_OK;
  }
  if (!is_cacheable_command(message_type)) {
    return HIL_CODEC_INVALID_VALUE;
  }

  if ((cache->valid == 0U) || (cache->sequence != sequence)) {
    *result = HIL_COMMAND_NEW;
    return HIL_CODEC_OK;
  }

  if ((cache->message_type == message_type) &&
      (cache->payload_length == payload_length) &&
      ((payload_length == 0U) ||
       (memcmp(cache->payload, payload, payload_length) == 0))) {
    *result = HIL_COMMAND_DUPLICATE;
  } else {
    *result = HIL_COMMAND_SEQUENCE_CONFLICT;
  }
  return HIL_CODEC_OK;
}

hil_codec_status_t hil_command_cache_commit(
    hil_command_cache_t *cache, uint16_t sequence, uint8_t message_type,
    const uint8_t *payload, uint16_t payload_length,
    const hil_response_message_t *responses, uint8_t response_count) {
  hil_command_result_t result;
  hil_codec_status_t status;
  uint8_t index;

  if (cache == NULL) {
    return HIL_CODEC_NULL_ARGUMENT;
  }
  status = validate_payload(payload, payload_length);
  if (status != HIL_CODEC_OK) {
    return status;
  }
  if (message_type == HIL_MSG_STATUS_REQUEST) {
    return (payload_length == 0U) ? HIL_CODEC_OK : HIL_CODEC_INVALID_LENGTH;
  }
  status = hil_command_cache_check(cache, sequence, message_type, payload,
                                   payload_length, &result);
  if (status != HIL_CODEC_OK) {
    return status;
  }
  if (result != HIL_COMMAND_NEW) {
    return HIL_CODEC_INVALID_VALUE;
  }
  if (response_count > HIL_COMMAND_CACHE_RESPONSE_MAX) {
    return HIL_CODEC_INVALID_VALUE;
  }
  if ((response_count != 0U) && (responses == NULL)) {
    return HIL_CODEC_NULL_ARGUMENT;
  }
  for (index = 0U; index < response_count; ++index) {
    status = validate_payload(responses[index].payload,
                              responses[index].payload_length);
    if (status != HIL_CODEC_OK) {
      return status;
    }
  }

  /* All validation is complete. Mutate only after this point. */
  cache->valid = 0U;
  cache->sequence = sequence;
  cache->message_type = message_type;
  cache->payload_length = payload_length;
  if (payload_length != 0U) {
    (void)memmove(cache->payload, payload, payload_length);
  }
  cache->response_count = response_count;
  for (index = 0U; index < response_count; ++index) {
    cache->responses[index].message_type = responses[index].message_type;
    cache->responses[index].payload_length = responses[index].payload_length;
    if (responses[index].payload_length != 0U) {
      (void)memmove(cache->responses[index].payload, responses[index].payload,
                    responses[index].payload_length);
    }
  }
  cache->valid = 1U;
  return HIL_CODEC_OK;
}

hil_codec_status_t
hil_command_cache_get_response(const hil_command_cache_t *cache,
                               uint8_t response_index,
                               hil_cached_response_t *response) {
  if ((cache == NULL) || (response == NULL)) {
    return HIL_CODEC_NULL_ARGUMENT;
  }
  if ((cache->valid == 0U) || (response_index >= cache->response_count)) {
    return HIL_CODEC_INVALID_VALUE;
  }
  *response = cache->responses[response_index];
  return HIL_CODEC_OK;
}
