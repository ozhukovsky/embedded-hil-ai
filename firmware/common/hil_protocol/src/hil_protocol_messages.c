#include "hil_protocol_messages.h"

#include <string.h>

static uint16_t get_u16(const uint8_t *p) {
  return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8U));
}

static uint32_t get_u32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) | ((uint32_t)p[2] << 16U) |
         ((uint32_t)p[3] << 24U);
}

static int32_t get_i32(const uint8_t *p) {
  uint32_t bits = get_u32(p);
  int32_t value;
  (void)memcpy(&value, &bits, sizeof(value));
  return value;
}

static void put_u16(uint8_t *p, uint16_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8U);
}

static void put_u32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8U);
  p[2] = (uint8_t)(v >> 16U);
  p[3] = (uint8_t)(v >> 24U);
}

static void put_i32(uint8_t *p, int32_t v) {
  uint32_t bits;
  (void)memcpy(&bits, &v, sizeof(bits));
  put_u32(p, bits);
}

static int valid_role(uint8_t role) {
  return (role == HIL_ROLE_DUT) || (role == HIL_ROLE_PLANT);
}

static int valid_state(uint8_t state) { return state <= HIL_STATE_COMPLETE; }

static hil_codec_status_t output_check(uint8_t *out, size_t capacity,
                                       size_t required, size_t *written) {
  if (written == NULL)
    return HIL_CODEC_NULL_ARGUMENT;
  *written = 0U;
  if ((out == NULL) && (required != 0U))
    return HIL_CODEC_NULL_ARGUMENT;
  if (capacity < required)
    return HIL_CODEC_OUTPUT_TOO_SMALL;
  return HIL_CODEC_OK;
}

static hil_codec_status_t input_check(const uint8_t *in, size_t length,
                                      size_t expected, const void *message) {
  if ((message == NULL) || ((in == NULL) && (expected != 0U)))
    return HIL_CODEC_NULL_ARGUMENT;
  return (length == expected) ? HIL_CODEC_OK : HIL_CODEC_INVALID_LENGTH;
}

#define PREPARE_OUTPUT(out, cap, size, written)                                \
  do {                                                                         \
    hil_codec_status_t s_ = output_check((out), (cap), (size), (written));     \
    if (s_ != HIL_CODEC_OK)                                                    \
      return s_;                                                               \
  } while (0)
#define PREPARE_INPUT(in, len, size, msg)                                      \
  do {                                                                         \
    hil_codec_status_t s_ = input_check((in), (len), (size), (msg));           \
    if (s_ != HIL_CODEC_OK)                                                    \
      return s_;                                                               \
  } while (0)
#define REQUIRE(condition)                                                     \
  do {                                                                         \
    if (!(condition))                                                          \
      return HIL_CODEC_INVALID_VALUE;                                          \
  } while (0)

hil_codec_status_t hil_encode_status_request(const hil_status_request_t *m,
                                             uint8_t *o, size_t c, size_t *n) {
  if (m == NULL)
    return HIL_CODEC_NULL_ARGUMENT;
  PREPARE_OUTPUT(o, c, 0U, n);
  *n = 0U;
  return HIL_CODEC_OK;
}

hil_codec_status_t hil_decode_status_request(const uint8_t *b, size_t l,
                                             hil_status_request_t *m) {
  PREPARE_INPUT(b, l, 0U, m);
  m->unused = 0U;
  return HIL_CODEC_OK;
}

hil_codec_status_t hil_encode_boot_report(const hil_boot_report_t *m,
                                          uint8_t *o, size_t c, size_t *n) {
  if (m == NULL)
    return HIL_CODEC_NULL_ARGUMENT;
  REQUIRE(valid_role(m->role));
  REQUIRE((m->capabilities & 0xFFC0U) == 0U);
  PREPARE_OUTPUT(o, c, 12U, n);
  o[0] = m->role;
  o[1] = m->fw_major;
  o[2] = m->fw_minor;
  o[3] = m->fw_patch;
  put_u16(o + 4, m->reset_cause);
  put_u16(o + 6, m->capabilities);
  put_u32(o + 8, m->uptime_ms);
  *n = 12U;
  return HIL_CODEC_OK;
}

hil_codec_status_t hil_decode_boot_report(const uint8_t *b, size_t l,
                                          hil_boot_report_t *m) {
  PREPARE_INPUT(b, l, 12U, m);
  m->role = b[0];
  m->fw_major = b[1];
  m->fw_minor = b[2];
  m->fw_patch = b[3];
  m->reset_cause = get_u16(b + 4);
  m->capabilities = get_u16(b + 6);
  m->uptime_ms = get_u32(b + 8);
  REQUIRE(valid_role(m->role));
  REQUIRE((m->capabilities & 0xFFC0U) == 0U);
  return HIL_CODEC_OK;
}

static uint16_t status_mask(uint8_t role) {
  return (role == HIL_ROLE_DUT) ? 0x0037U : 0x003BU;
}

hil_codec_status_t hil_encode_status(const hil_status_message_t *m, uint8_t *o,
                                     size_t c, size_t *n) {
  if (m == NULL)
    return HIL_CODEC_NULL_ARGUMENT;
  REQUIRE(valid_role(m->role));
  REQUIRE(valid_state(m->state));
  REQUIRE(m->fault_code <= 7U);
  REQUIRE((m->status_flags & (uint16_t)~status_mask(m->role)) == 0U);
  PREPARE_OUTPUT(o, c, 14U, n);
  o[0] = m->role;
  o[1] = m->state;
  put_u16(o + 2, m->fault_code);
  put_u32(o + 4, m->test_id);
  put_u32(o + 8, m->uptime_ms);
  put_u16(o + 12, m->status_flags);
  *n = 14U;
  return HIL_CODEC_OK;
}

hil_codec_status_t hil_decode_status(const uint8_t *b, size_t l,
                                     hil_status_message_t *m) {
  PREPARE_INPUT(b, l, 14U, m);
  m->role = b[0];
  m->state = b[1];
  m->fault_code = get_u16(b + 2);
  m->test_id = get_u32(b + 4);
  m->uptime_ms = get_u32(b + 8);
  m->status_flags = get_u16(b + 12);
  REQUIRE(valid_role(m->role));
  REQUIRE(valid_state(m->state));
  REQUIRE(m->fault_code <= 7U);
  REQUIRE((m->status_flags & (uint16_t)~status_mask(m->role)) == 0U);
  return HIL_CODEC_OK;
}

hil_codec_status_t hil_encode_heartbeat(const hil_heartbeat_t *m, uint8_t *o,
                                        size_t c, size_t *n) {
  if (m == NULL)
    return HIL_CODEC_NULL_ARGUMENT;
  REQUIRE(valid_role(m->role));
  REQUIRE(valid_state(m->state));
  PREPARE_OUTPUT(o, c, 10U, n);
  o[0] = m->role;
  o[1] = m->state;
  put_u32(o + 2, m->test_id);
  put_u32(o + 6, m->uptime_ms);
  *n = 10U;
  return HIL_CODEC_OK;
}

hil_codec_status_t hil_decode_heartbeat(const uint8_t *b, size_t l,
                                        hil_heartbeat_t *m) {
  PREPARE_INPUT(b, l, 10U, m);
  m->role = b[0];
  m->state = b[1];
  m->test_id = get_u32(b + 2);
  m->uptime_ms = get_u32(b + 6);
  REQUIRE(valid_role(m->role));
  REQUIRE(valid_state(m->state));
  return HIL_CODEC_OK;
}

hil_codec_status_t hil_encode_dut_config(const hil_dut_config_t *m, uint8_t *o,
                                         size_t c, size_t *n) {
  if (m == NULL)
    return HIL_CODEC_NULL_ARGUMENT;
  REQUIRE(m->heat_on_threshold_mC < m->heat_off_threshold_mC);
  REQUIRE(m->control_period_ms == 1000U);
  REQUIRE(m->max_runtime_ms != 0U);
  PREPARE_OUTPUT(o, c, HIL_DUT_CONFIG_SIZE, n);
  o[0] = HIL_ROLE_DUT;
  put_u32(o + 1, m->test_id);
  put_i32(o + 5, m->setpoint_mC);
  put_i32(o + 9, m->heat_on_threshold_mC);
  put_i32(o + 13, m->heat_off_threshold_mC);
  put_u16(o + 17, m->control_period_ms);
  put_u16(o + 19, m->telemetry_period_ms);
  put_u32(o + 21, m->max_runtime_ms);
  *n = HIL_DUT_CONFIG_SIZE;
  return HIL_CODEC_OK;
}

hil_codec_status_t hil_decode_dut_config(const uint8_t *b, size_t l,
                                         hil_dut_config_t *m) {
  PREPARE_INPUT(b, l, HIL_DUT_CONFIG_SIZE, m);
  REQUIRE(b[0] == HIL_ROLE_DUT);
  m->test_id = get_u32(b + 1);
  m->setpoint_mC = get_i32(b + 5);
  m->heat_on_threshold_mC = get_i32(b + 9);
  m->heat_off_threshold_mC = get_i32(b + 13);
  m->control_period_ms = get_u16(b + 17);
  m->telemetry_period_ms = get_u16(b + 19);
  m->max_runtime_ms = get_u32(b + 21);
  REQUIRE(m->heat_on_threshold_mC < m->heat_off_threshold_mC);
  REQUIRE(m->control_period_ms == 1000U);
  REQUIRE(m->max_runtime_ms != 0U);
  return HIL_CODEC_OK;
}

hil_codec_status_t hil_encode_plant_config(const hil_plant_config_t *m,
                                           uint8_t *o, size_t c, size_t *n) {
  size_t i, length;
  if (m == NULL)
    return HIL_CODEC_NULL_ARGUMENT;
  REQUIRE(m->initial_temp_mC >= -20000 && m->initial_temp_mC <= 60000);
  REQUIRE(m->tau_s != 0U);
  REQUIRE(m->model_step_ms == 5000U);
  REQUIRE(m->max_runtime_ms != 0U);
  REQUIRE(m->disturbance_count <= HIL_PLANT_MAX_DISTURBANCES);
  for (i = 1U; i < m->disturbance_count; i++)
    REQUIRE(m->disturbances[i - 1U].offset_ms < m->disturbances[i].offset_ms);
  length = HIL_PLANT_CONFIG_BASE_SIZE +
           (size_t)m->disturbance_count * HIL_DISTURBANCE_SIZE;
  PREPARE_OUTPUT(o, c, length, n);
  o[0] = HIL_ROLE_PLANT;
  put_u32(o + 1, m->test_id);
  put_i32(o + 5, m->initial_temp_mC);
  put_i32(o + 9, m->initial_ambient_mC);
  put_u32(o + 13, m->tau_s);
  put_i32(o + 17, m->heater_gain_mC_per_s);
  put_u16(o + 21, m->model_step_ms);
  put_u16(o + 23, m->telemetry_period_ms);
  put_u32(o + 25, m->max_runtime_ms);
  o[29] = m->disturbance_count;
  for (i = 0U; i < m->disturbance_count; i++) {
    size_t p = 30U + i * 8U;
    put_u32(o + p, m->disturbances[i].offset_ms);
    put_i32(o + p + 4, m->disturbances[i].new_ambient_temp_mC);
  }
  *n = length;
  return HIL_CODEC_OK;
}

hil_codec_status_t hil_decode_plant_config(const uint8_t *b, size_t l,
                                           hil_plant_config_t *m) {
  size_t i, expected;
  if ((b == NULL) || (m == NULL))
    return HIL_CODEC_NULL_ARGUMENT;
  if (l < HIL_PLANT_CONFIG_BASE_SIZE)
    return HIL_CODEC_INVALID_LENGTH;
  if (b[0] != HIL_ROLE_PLANT)
    return HIL_CODEC_INVALID_VALUE;
  if (b[29] > HIL_PLANT_MAX_DISTURBANCES)
    return HIL_CODEC_INVALID_VALUE;
  expected = HIL_PLANT_CONFIG_BASE_SIZE + (size_t)b[29] * HIL_DISTURBANCE_SIZE;
  if (l != expected)
    return HIL_CODEC_INVALID_LENGTH;
  m->test_id = get_u32(b + 1);
  m->initial_temp_mC = get_i32(b + 5);
  m->initial_ambient_mC = get_i32(b + 9);
  m->tau_s = get_u32(b + 13);
  m->heater_gain_mC_per_s = get_i32(b + 17);
  m->model_step_ms = get_u16(b + 21);
  m->telemetry_period_ms = get_u16(b + 23);
  m->max_runtime_ms = get_u32(b + 25);
  m->disturbance_count = b[29];
  for (i = 0U; i < m->disturbance_count; i++) {
    size_t p = 30U + i * 8U;
    m->disturbances[i].offset_ms = get_u32(b + p);
    m->disturbances[i].new_ambient_temp_mC = get_i32(b + p + 4);
  }
  REQUIRE(m->initial_temp_mC >= -20000 && m->initial_temp_mC <= 60000);
  REQUIRE(m->tau_s != 0U);
  REQUIRE(m->model_step_ms == 5000U);
  REQUIRE(m->max_runtime_ms != 0U);
  for (i = 1U; i < m->disturbance_count; i++)
    REQUIRE(m->disturbances[i - 1U].offset_ms < m->disturbances[i].offset_ms);
  return HIL_CODEC_OK;
}

hil_codec_status_t hil_encode_test_command(const hil_test_command_t *m,
                                           uint8_t *o, size_t c, size_t *n) {
  if (m == NULL)
    return HIL_CODEC_NULL_ARGUMENT;
  REQUIRE(valid_role(m->target_role));
  PREPARE_OUTPUT(o, c, 5U, n);
  o[0] = m->target_role;
  put_u32(o + 1, m->test_id);
  *n = 5U;
  return HIL_CODEC_OK;
}
hil_codec_status_t hil_decode_test_command(const uint8_t *b, size_t l,
                                           hil_test_command_t *m) {
  PREPARE_INPUT(b, l, 5U, m);
  m->target_role = b[0];
  m->test_id = get_u32(b + 1);
  REQUIRE(valid_role(m->target_role));
  return HIL_CODEC_OK;
}
hil_codec_status_t hil_encode_arm_test(const hil_arm_test_t *m, uint8_t *o,
                                       size_t c, size_t *n) {
  return hil_encode_test_command(m, o, c, n);
}
hil_codec_status_t hil_decode_arm_test(const uint8_t *b, size_t l,
                                       hil_arm_test_t *m) {
  return hil_decode_test_command(b, l, m);
}
hil_codec_status_t hil_encode_start_test(const hil_start_test_t *m, uint8_t *o,
                                         size_t c, size_t *n) {
  return hil_encode_test_command(m, o, c, n);
}
hil_codec_status_t hil_decode_start_test(const uint8_t *b, size_t l,
                                         hil_start_test_t *m) {
  return hil_decode_test_command(b, l, m);
}

static hil_codec_status_t encode_ack_common(const hil_ack_t *m, uint8_t *o,
                                            size_t c, size_t *n, int nack) {
  if (m == NULL)
    return HIL_CODEC_NULL_ARGUMENT;
  if (!valid_role(m->responding_role) ||
      (nack ? (m->error_code < 1U || m->error_code > 9U)
            : (m->error_code != 0U)))
    return HIL_CODEC_INVALID_VALUE;
  PREPARE_OUTPUT(o, c, 6U, n);
  put_u16(o, m->acknowledged_sequence);
  o[2] = m->acknowledged_message_type;
  o[3] = m->responding_role;
  put_u16(o + 4, m->error_code);
  *n = 6U;
  return HIL_CODEC_OK;
}
static hil_codec_status_t decode_ack_common(const uint8_t *b, size_t l,
                                            hil_ack_t *m, int nack) {
  PREPARE_INPUT(b, l, 6U, m);
  m->acknowledged_sequence = get_u16(b);
  m->acknowledged_message_type = b[2];
  m->responding_role = b[3];
  m->error_code = get_u16(b + 4);
  REQUIRE(valid_role(m->responding_role));
  REQUIRE(nack ? (m->error_code >= 1U && m->error_code <= 9U)
               : (m->error_code == 0U));
  return HIL_CODEC_OK;
}
hil_codec_status_t hil_encode_ack(const hil_ack_t *m, uint8_t *o, size_t c,
                                  size_t *n) {
  return encode_ack_common(m, o, c, n, 0);
}
hil_codec_status_t hil_decode_ack(const uint8_t *b, size_t l, hil_ack_t *m) {
  return decode_ack_common(b, l, m, 0);
}
hil_codec_status_t hil_encode_nack(const hil_ack_t *m, uint8_t *o, size_t c,
                                   size_t *n) {
  return encode_ack_common(m, o, c, n, 1);
}
hil_codec_status_t hil_decode_nack(const uint8_t *b, size_t l, hil_ack_t *m) {
  return decode_ack_common(b, l, m, 1);
}

hil_codec_status_t hil_encode_armed(const hil_armed_t *m, uint8_t *o, size_t c,
                                    size_t *n) {
  uint16_t mask;
  if (m == NULL)
    return HIL_CODEC_NULL_ARGUMENT;
  REQUIRE(valid_role(m->role));
  mask = (m->role == HIL_ROLE_DUT) ? 0x001FU : 0x0017U;
  REQUIRE((m->readiness_flags & (uint16_t)~mask) == 0U);
  PREPARE_OUTPUT(o, c, 7U, n);
  o[0] = m->role;
  put_u32(o + 1, m->test_id);
  put_u16(o + 5, m->readiness_flags);
  *n = 7U;
  return HIL_CODEC_OK;
}
hil_codec_status_t hil_decode_armed(const uint8_t *b, size_t l,
                                    hil_armed_t *m) {
  uint16_t mask;
  PREPARE_INPUT(b, l, 7U, m);
  m->role = b[0];
  m->test_id = get_u32(b + 1);
  m->readiness_flags = get_u16(b + 5);
  REQUIRE(valid_role(m->role));
  mask = (m->role == HIL_ROLE_DUT) ? 0x001FU : 0x0017U;
  REQUIRE((m->readiness_flags & (uint16_t)~mask) == 0U);
  return HIL_CODEC_OK;
}

hil_codec_status_t hil_encode_stop_test(const hil_stop_test_t *m, uint8_t *o,
                                        size_t c, size_t *n) {
  if (m == NULL)
    return HIL_CODEC_NULL_ARGUMENT;
  REQUIRE(valid_role(m->target_role));
  REQUIRE(m->stop_reason <= HIL_STOP_HOST_SAFETY_ABORT);
  PREPARE_OUTPUT(o, c, 6U, n);
  o[0] = m->target_role;
  put_u32(o + 1, m->test_id);
  o[5] = m->stop_reason;
  *n = 6U;
  return HIL_CODEC_OK;
}
hil_codec_status_t hil_decode_stop_test(const uint8_t *b, size_t l,
                                        hil_stop_test_t *m) {
  PREPARE_INPUT(b, l, 6U, m);
  m->target_role = b[0];
  m->test_id = get_u32(b + 1);
  m->stop_reason = b[5];
  REQUIRE(valid_role(m->target_role));
  REQUIRE(m->stop_reason <= HIL_STOP_HOST_SAFETY_ABORT);
  return HIL_CODEC_OK;
}

hil_codec_status_t hil_encode_dut_telemetry(const hil_dut_telemetry_t *m,
                                            uint8_t *o, size_t c, size_t *n) {
  if (m == NULL)
    return HIL_CODEC_NULL_ARGUMENT;
  REQUIRE(m->role == HIL_ROLE_DUT);
  REQUIRE(m->adc_raw <= 4095U);
  REQUIRE(m->heater_cmd <= 1U);
  REQUIRE(m->controller_state <= 2U);
  REQUIRE(m->plant_valid <= 1U);
  PREPARE_OUTPUT(o, c, HIL_DUT_TELEMETRY_SIZE, n);
  o[0] = m->role;
  put_u32(o + 1, m->test_id);
  put_u32(o + 5, m->sample_index);
  put_u32(o + 9, m->uptime_ms);
  put_u16(o + 13, m->adc_raw);
  put_i32(o + 15, m->measured_temp_mC);
  o[19] = m->heater_cmd;
  o[20] = m->controller_state;
  o[21] = m->plant_valid;
  *n = HIL_DUT_TELEMETRY_SIZE;
  return HIL_CODEC_OK;
}
hil_codec_status_t hil_decode_dut_telemetry(const uint8_t *b, size_t l,
                                            hil_dut_telemetry_t *m) {
  PREPARE_INPUT(b, l, HIL_DUT_TELEMETRY_SIZE, m);
  m->role = b[0];
  m->test_id = get_u32(b + 1);
  m->sample_index = get_u32(b + 5);
  m->uptime_ms = get_u32(b + 9);
  m->adc_raw = get_u16(b + 13);
  m->measured_temp_mC = get_i32(b + 15);
  m->heater_cmd = b[19];
  m->controller_state = b[20];
  m->plant_valid = b[21];
  REQUIRE(m->role == HIL_ROLE_DUT);
  REQUIRE(m->adc_raw <= 4095U);
  REQUIRE(m->heater_cmd <= 1U);
  REQUIRE(m->controller_state <= 2U);
  REQUIRE(m->plant_valid <= 1U);
  return HIL_CODEC_OK;
}

hil_codec_status_t hil_encode_plant_telemetry(const hil_plant_telemetry_t *m,
                                              uint8_t *o, size_t c, size_t *n) {
  if (m == NULL)
    return HIL_CODEC_NULL_ARGUMENT;
  REQUIRE(m->role == HIL_ROLE_PLANT);
  REQUIRE(m->heater_input <= 1U);
  REQUIRE(m->dac_code <= 4095U);
  REQUIRE(m->plant_valid <= 1U);
  PREPARE_OUTPUT(o, c, HIL_PLANT_TELEMETRY_SIZE, n);
  o[0] = m->role;
  put_u32(o + 1, m->test_id);
  put_u32(o + 5, m->sample_index);
  put_u32(o + 9, m->uptime_ms);
  put_i32(o + 13, m->simulated_temp_mC);
  put_i32(o + 17, m->ambient_temp_mC);
  o[21] = m->heater_input;
  put_u16(o + 22, m->dac_code);
  o[24] = m->plant_valid;
  *n = HIL_PLANT_TELEMETRY_SIZE;
  return HIL_CODEC_OK;
}
hil_codec_status_t hil_decode_plant_telemetry(const uint8_t *b, size_t l,
                                              hil_plant_telemetry_t *m) {
  PREPARE_INPUT(b, l, HIL_PLANT_TELEMETRY_SIZE, m);
  m->role = b[0];
  m->test_id = get_u32(b + 1);
  m->sample_index = get_u32(b + 5);
  m->uptime_ms = get_u32(b + 9);
  m->simulated_temp_mC = get_i32(b + 13);
  m->ambient_temp_mC = get_i32(b + 17);
  m->heater_input = b[21];
  m->dac_code = get_u16(b + 22);
  m->plant_valid = b[24];
  REQUIRE(m->role == HIL_ROLE_PLANT);
  REQUIRE(m->heater_input <= 1U);
  REQUIRE(m->dac_code <= 4095U);
  REQUIRE(m->plant_valid <= 1U);
  return HIL_CODEC_OK;
}

hil_codec_status_t hil_encode_fault_report(const hil_fault_report_t *m,
                                           uint8_t *o, size_t c, size_t *n) {
  if (m == NULL)
    return HIL_CODEC_NULL_ARGUMENT;
  REQUIRE(valid_role(m->role));
  REQUIRE(m->fault_code >= 1U && m->fault_code <= 7U);
  PREPARE_OUTPUT(o, c, 13U, n);
  o[0] = m->role;
  put_u32(o + 1, m->test_id);
  put_u16(o + 5, m->fault_code);
  put_u16(o + 7, m->fault_detail);
  put_u32(o + 9, m->uptime_ms);
  *n = 13U;
  return HIL_CODEC_OK;
}
hil_codec_status_t hil_decode_fault_report(const uint8_t *b, size_t l,
                                           hil_fault_report_t *m) {
  PREPARE_INPUT(b, l, 13U, m);
  m->role = b[0];
  m->test_id = get_u32(b + 1);
  m->fault_code = get_u16(b + 5);
  m->fault_detail = get_u16(b + 7);
  m->uptime_ms = get_u32(b + 9);
  REQUIRE(valid_role(m->role));
  REQUIRE(m->fault_code >= 1U && m->fault_code <= 7U);
  return HIL_CODEC_OK;
}

hil_codec_status_t hil_encode_test_complete(const hil_test_complete_t *m,
                                            uint8_t *o, size_t c, size_t *n) {
  if (m == NULL)
    return HIL_CODEC_NULL_ARGUMENT;
  REQUIRE(valid_role(m->role));
  REQUIRE(m->outcome <= HIL_COMPLETE_FAULT_ABORT);
  PREPARE_OUTPUT(o, c, 10U, n);
  o[0] = m->role;
  put_u32(o + 1, m->test_id);
  o[5] = m->outcome;
  put_u32(o + 6, m->elapsed_ms);
  *n = 10U;
  return HIL_CODEC_OK;
}
hil_codec_status_t hil_decode_test_complete(const uint8_t *b, size_t l,
                                            hil_test_complete_t *m) {
  PREPARE_INPUT(b, l, 10U, m);
  m->role = b[0];
  m->test_id = get_u32(b + 1);
  m->outcome = b[5];
  m->elapsed_ms = get_u32(b + 6);
  REQUIRE(valid_role(m->role));
  REQUIRE(m->outcome <= HIL_COMPLETE_FAULT_ABORT);
  return HIL_CODEC_OK;
}
