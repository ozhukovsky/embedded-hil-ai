#ifndef HIL_PROTOCOL_MESSAGES_H
#define HIL_PROTOCOL_MESSAGES_H

#include "hil_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HIL_ROLE_DUT 1U
#define HIL_ROLE_PLANT 2U

#define HIL_STATE_BOOT 0U
#define HIL_STATE_INITIALIZING 1U
#define HIL_STATE_WAIT_CONFIG 2U
#define HIL_STATE_READY 3U
#define HIL_STATE_ARMED 4U
#define HIL_STATE_RUNNING 5U
#define HIL_STATE_FAULT 6U
#define HIL_STATE_COMPLETE 7U

#define HIL_NACK_UNSUPPORTED_MESSAGE 1U
#define HIL_NACK_WRONG_TARGET_ROLE 2U
#define HIL_NACK_INVALID_STATE 3U
#define HIL_NACK_INVALID_PAYLOAD_LENGTH 4U
#define HIL_NACK_INVALID_CONFIGURATION 5U
#define HIL_NACK_TEST_ID_MISMATCH 6U
#define HIL_NACK_HARDWARE_NOT_READY 7U
#define HIL_NACK_UNSUPPORTED_VERSION 8U
#define HIL_NACK_SEQUENCE_CONFLICT 9U

#define HIL_STOP_NORMAL 0U
#define HIL_STOP_USER_ABORT 1U
#define HIL_STOP_HOST_SAFETY_ABORT 2U

#define HIL_COMPLETE_NORMAL 0U
#define HIL_COMPLETE_HOST_ABORT 1U
#define HIL_COMPLETE_FAULT_ABORT 2U

#define HIL_DUT_CONFIG_SIZE 25U
#define HIL_PLANT_CONFIG_BASE_SIZE 30U
#define HIL_DISTURBANCE_SIZE 8U
#define HIL_PLANT_MAX_DISTURBANCES 4U
#define HIL_PLANT_CONFIG_MAX_SIZE 62U
#define HIL_DUT_TELEMETRY_SIZE 22U
#define HIL_PLANT_TELEMETRY_SIZE 25U

typedef struct {
  uint8_t unused;
} hil_status_request_t;
typedef struct {
  uint8_t role, fw_major, fw_minor, fw_patch;
  uint16_t reset_cause, capabilities;
  uint32_t uptime_ms;
} hil_boot_report_t;
typedef struct {
  uint8_t role, state;
  uint16_t fault_code;
  uint32_t test_id, uptime_ms;
  uint16_t status_flags;
} hil_status_message_t;
typedef struct {
  uint8_t role, state;
  uint32_t test_id, uptime_ms;
} hil_heartbeat_t;
typedef struct {
  uint8_t target_role;
  uint32_t test_id;
} hil_test_command_t;
typedef hil_test_command_t hil_arm_test_t;
typedef hil_test_command_t hil_start_test_t;
typedef struct {
  uint32_t test_id;
  int32_t setpoint_mC, heat_on_threshold_mC, heat_off_threshold_mC;
  uint16_t control_period_ms, telemetry_period_ms;
  uint32_t max_runtime_ms;
} hil_dut_config_t;
typedef struct {
  uint32_t offset_ms;
  int32_t new_ambient_temp_mC;
} hil_disturbance_t;
typedef struct {
  uint32_t test_id;
  int32_t initial_temp_mC, initial_ambient_mC;
  uint32_t tau_s;
  int32_t heater_gain_mC_per_s;
  uint16_t model_step_ms, telemetry_period_ms;
  uint32_t max_runtime_ms;
  uint8_t disturbance_count;
  hil_disturbance_t disturbances[HIL_PLANT_MAX_DISTURBANCES];
} hil_plant_config_t;
typedef struct {
  uint16_t acknowledged_sequence;
  uint8_t acknowledged_message_type, responding_role;
  uint16_t error_code;
} hil_ack_t;
typedef struct {
  uint8_t role;
  uint32_t test_id;
  uint16_t readiness_flags;
} hil_armed_t;
typedef struct {
  uint8_t target_role;
  uint32_t test_id;
  uint8_t stop_reason;
} hil_stop_test_t;
typedef struct {
  uint8_t role;
  uint32_t test_id, sample_index, uptime_ms;
  uint16_t adc_raw;
  int32_t measured_temp_mC;
  uint8_t heater_cmd, controller_state, plant_valid;
} hil_dut_telemetry_t;
typedef struct {
  uint8_t role;
  uint32_t test_id, sample_index, uptime_ms;
  int32_t simulated_temp_mC, ambient_temp_mC;
  uint8_t heater_input;
  uint16_t dac_code;
  uint8_t plant_valid;
} hil_plant_telemetry_t;
typedef struct {
  uint8_t role;
  uint32_t test_id;
  uint16_t fault_code, fault_detail;
  uint32_t uptime_ms;
} hil_fault_report_t;
typedef struct {
  uint8_t role;
  uint32_t test_id;
  uint8_t outcome;
  uint32_t elapsed_ms;
} hil_test_complete_t;

hil_codec_status_t hil_encode_status_request(const hil_status_request_t *,
                                             uint8_t *, size_t, size_t *);
hil_codec_status_t hil_decode_status_request(const uint8_t *, size_t,
                                             hil_status_request_t *);
hil_codec_status_t hil_encode_boot_report(const hil_boot_report_t *, uint8_t *,
                                          size_t, size_t *);
hil_codec_status_t hil_decode_boot_report(const uint8_t *, size_t,
                                          hil_boot_report_t *);
hil_codec_status_t hil_encode_status(const hil_status_message_t *, uint8_t *,
                                     size_t, size_t *);
hil_codec_status_t hil_decode_status(const uint8_t *, size_t,
                                     hil_status_message_t *);
hil_codec_status_t hil_encode_heartbeat(const hil_heartbeat_t *, uint8_t *,
                                        size_t, size_t *);
hil_codec_status_t hil_decode_heartbeat(const uint8_t *, size_t,
                                        hil_heartbeat_t *);
hil_codec_status_t hil_encode_dut_config(const hil_dut_config_t *, uint8_t *,
                                         size_t, size_t *);
hil_codec_status_t hil_decode_dut_config(const uint8_t *, size_t,
                                         hil_dut_config_t *);
hil_codec_status_t hil_encode_plant_config(const hil_plant_config_t *,
                                           uint8_t *, size_t, size_t *);
hil_codec_status_t hil_decode_plant_config(const uint8_t *, size_t,
                                           hil_plant_config_t *);
hil_codec_status_t hil_encode_test_command(const hil_test_command_t *,
                                           uint8_t *, size_t, size_t *);
hil_codec_status_t hil_decode_test_command(const uint8_t *, size_t,
                                           hil_test_command_t *);
hil_codec_status_t hil_encode_arm_test(const hil_arm_test_t *, uint8_t *,
                                       size_t, size_t *);
hil_codec_status_t hil_decode_arm_test(const uint8_t *, size_t,
                                       hil_arm_test_t *);
hil_codec_status_t hil_encode_start_test(const hil_start_test_t *, uint8_t *,
                                         size_t, size_t *);
hil_codec_status_t hil_decode_start_test(const uint8_t *, size_t,
                                         hil_start_test_t *);
hil_codec_status_t hil_encode_ack(const hil_ack_t *, uint8_t *, size_t,
                                  size_t *);
hil_codec_status_t hil_decode_ack(const uint8_t *, size_t, hil_ack_t *);
hil_codec_status_t hil_encode_nack(const hil_ack_t *, uint8_t *, size_t,
                                   size_t *);
hil_codec_status_t hil_decode_nack(const uint8_t *, size_t, hil_ack_t *);
hil_codec_status_t hil_encode_armed(const hil_armed_t *, uint8_t *, size_t,
                                    size_t *);
hil_codec_status_t hil_decode_armed(const uint8_t *, size_t, hil_armed_t *);
hil_codec_status_t hil_encode_stop_test(const hil_stop_test_t *, uint8_t *,
                                        size_t, size_t *);
hil_codec_status_t hil_decode_stop_test(const uint8_t *, size_t,
                                        hil_stop_test_t *);
hil_codec_status_t hil_encode_dut_telemetry(const hil_dut_telemetry_t *,
                                            uint8_t *, size_t, size_t *);
hil_codec_status_t hil_decode_dut_telemetry(const uint8_t *, size_t,
                                            hil_dut_telemetry_t *);
hil_codec_status_t hil_encode_plant_telemetry(const hil_plant_telemetry_t *,
                                              uint8_t *, size_t, size_t *);
hil_codec_status_t hil_decode_plant_telemetry(const uint8_t *, size_t,
                                              hil_plant_telemetry_t *);
hil_codec_status_t hil_encode_fault_report(const hil_fault_report_t *,
                                           uint8_t *, size_t, size_t *);
hil_codec_status_t hil_decode_fault_report(const uint8_t *, size_t,
                                           hil_fault_report_t *);
hil_codec_status_t hil_encode_test_complete(const hil_test_complete_t *,
                                            uint8_t *, size_t, size_t *);
hil_codec_status_t hil_decode_test_complete(const uint8_t *, size_t,
                                            hil_test_complete_t *);

#ifdef __cplusplus
}
#endif

#endif
