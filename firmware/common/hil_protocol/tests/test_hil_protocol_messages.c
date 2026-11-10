#include "hil_protocol_messages.h"

#include <stdio.h>
#include <string.h>

#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      (void)fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x);       \
      return 1;                                                                \
    }                                                                          \
  } while (0)
#define EQ(a, b, f) CHECK((a).f == (b).f)

static int test_boot_and_status_family(void) {
  uint8_t b[64];
  size_t n = 0U;
  hil_boot_report_t boot = {HIL_ROLE_DUT, 1U,      2U,         3U,
                            0x1234U,      0x002DU, 0x78563412U},
                    boot2 = {0};
  hil_status_message_t status = {HIL_ROLE_PLANT, HIL_STATE_RUNNING, 7U,
                                 0x12345678U,    0xABCDEF01U,       0x0039U},
                       status2 = {0};
  hil_heartbeat_t beat = {HIL_ROLE_DUT, HIL_STATE_READY, 0xA1B2C3D4U,
                          0x01020304U},
                  beat2 = {0};
  hil_status_request_t req = {0}, req2 = {0};
  static const uint8_t boot_golden[12] = {1,    1, 2,    3,    0x34, 0x12,
                                          0x2D, 0, 0x12, 0x34, 0x56, 0x78};
  CHECK(hil_encode_boot_report(&boot, b, sizeof(b), &n) == HIL_CODEC_OK &&
        n == 12U);
  CHECK(memcmp(b, boot_golden, sizeof(boot_golden)) == 0);
  CHECK(hil_decode_boot_report(b, n, &boot2) == HIL_CODEC_OK);
  EQ(boot, boot2, role);
  EQ(boot, boot2, fw_major);
  EQ(boot, boot2, fw_minor);
  EQ(boot, boot2, fw_patch);
  EQ(boot, boot2, reset_cause);
  EQ(boot, boot2, capabilities);
  EQ(boot, boot2, uptime_ms);
  CHECK(hil_encode_status(&status, b, sizeof(b), &n) == HIL_CODEC_OK &&
        n == 14U);
  CHECK(hil_decode_status(b, n, &status2) == HIL_CODEC_OK);
  EQ(status, status2, role);
  EQ(status, status2, state);
  EQ(status, status2, fault_code);
  EQ(status, status2, test_id);
  EQ(status, status2, uptime_ms);
  EQ(status, status2, status_flags);
  CHECK(hil_encode_heartbeat(&beat, b, sizeof(b), &n) == HIL_CODEC_OK &&
        n == 10U);
  CHECK(hil_decode_heartbeat(b, n, &beat2) == HIL_CODEC_OK);
  EQ(beat, beat2, role);
  EQ(beat, beat2, state);
  EQ(beat, beat2, test_id);
  EQ(beat, beat2, uptime_ms);
  CHECK(hil_encode_status_request(&req, b, sizeof(b), &n) == HIL_CODEC_OK &&
        n == 0U);
  CHECK(hil_decode_status_request(NULL, 0U, &req2) == HIL_CODEC_OK);
  return 0;
}

static int test_configs(void) {
  uint8_t b[64];
  size_t n = 0U;
  unsigned i;
  hil_dut_config_t d = {0x11223344U, 22000, -5000, 25000, 1000U, 1000U, 90000U},
                   d2 = {0};
  static const uint8_t dut_golden[25] = {
      1,    0x44, 0x33, 0x22, 0x11, 0xF0, 0x55, 0, 0,    0x78, 0xEC, 0xFF, 0xFF,
      0xA8, 0x61, 0,    0,    0xE8, 3,    0xE8, 3, 0x90, 0x5F, 1,    0};
  hil_plant_config_t p = {0};
  hil_plant_config_t p2 = {0};
  CHECK(hil_encode_dut_config(&d, b, sizeof(b), &n) == HIL_CODEC_OK &&
        n == HIL_DUT_CONFIG_SIZE);
  CHECK(memcmp(b, dut_golden, sizeof(dut_golden)) == 0);
  CHECK(hil_decode_dut_config(b, n, &d2) == HIL_CODEC_OK);
  EQ(d, d2, test_id);
  EQ(d, d2, setpoint_mC);
  EQ(d, d2, heat_on_threshold_mC);
  EQ(d, d2, heat_off_threshold_mC);
  EQ(d, d2, control_period_ms);
  EQ(d, d2, telemetry_period_ms);
  EQ(d, d2, max_runtime_ms);

  p.test_id = 0x01020304U;
  p.initial_temp_mC = -20000;
  p.initial_ambient_mC = 20000;
  p.tau_s = 300U;
  p.heater_gain_mC_per_s = 1000;
  p.model_step_ms = 5000U;
  p.telemetry_period_ms = 5000U;
  p.max_runtime_ms = 0x10203040U;
  p.disturbance_count = 4U;
  for (i = 0U; i < 4U; i++) {
    p.disturbances[i].offset_ms = (i + 1U) * 5000U;
    p.disturbances[i].new_ambient_temp_mC = (int32_t)(21000U + i * 1000U);
  }
  CHECK(hil_encode_plant_config(&p, b, sizeof(b), &n) == HIL_CODEC_OK &&
        n == 62U);
  CHECK(b[0] == HIL_ROLE_PLANT && b[29] == 4U && b[30] == 0x88U &&
        b[31] == 0x13U);
  CHECK(hil_decode_plant_config(b, n, &p2) == HIL_CODEC_OK);
  EQ(p, p2, test_id);
  EQ(p, p2, initial_temp_mC);
  EQ(p, p2, initial_ambient_mC);
  EQ(p, p2, tau_s);
  EQ(p, p2, heater_gain_mC_per_s);
  EQ(p, p2, model_step_ms);
  EQ(p, p2, telemetry_period_ms);
  EQ(p, p2, max_runtime_ms);
  EQ(p, p2, disturbance_count);
  for (i = 0U; i < 4U; i++) {
    CHECK(p.disturbances[i].offset_ms == p2.disturbances[i].offset_ms);
    CHECK(p.disturbances[i].new_ambient_temp_mC ==
          p2.disturbances[i].new_ambient_temp_mC);
  }
  p.disturbance_count = 0U;
  CHECK(hil_encode_plant_config(&p, b, sizeof(b), &n) == HIL_CODEC_OK &&
        n == 30U);
  CHECK(hil_decode_plant_config(b, n, &p2) == HIL_CODEC_OK &&
        p2.disturbance_count == 0U);
  return 0;
}

static int test_commands_and_responses(void) {
  uint8_t b[64];
  size_t n = 0U;
  hil_test_command_t cmd = {HIL_ROLE_PLANT, 0xDEADBEEFU}, cmd2 = {0};
  hil_arm_test_t arm = {HIL_ROLE_DUT, 0x10203040U}, arm2 = {0};
  hil_start_test_t start = {HIL_ROLE_PLANT, 0x50607080U}, start2 = {0};
  hil_ack_t ack = {0x1234U, HIL_MSG_START_TEST, HIL_ROLE_DUT, 0U}, ack2 = {0};
  hil_ack_t nack = {0x4321U, HIL_MSG_CONFIG_SET, HIL_ROLE_PLANT,
                    HIL_NACK_INVALID_CONFIGURATION},
            nack2 = {0};
  hil_armed_t armed = {HIL_ROLE_DUT, 0xAABBCCDDU, 0x001FU}, armed2 = {0};
  hil_stop_test_t stop = {HIL_ROLE_PLANT, 0x12345678U,
                          HIL_STOP_HOST_SAFETY_ABORT},
                  stop2 = {0};
  CHECK(hil_encode_test_command(&cmd, b, sizeof(b), &n) == HIL_CODEC_OK &&
        n == 5U);
  CHECK(hil_decode_test_command(b, n, &cmd2) == HIL_CODEC_OK);
  EQ(cmd, cmd2, target_role);
  EQ(cmd, cmd2, test_id);
  CHECK(hil_encode_arm_test(&arm, b, sizeof(b), &n) == HIL_CODEC_OK && n == 5U);
  CHECK(hil_decode_arm_test(b, n, &arm2) == HIL_CODEC_OK);
  EQ(arm, arm2, target_role);
  EQ(arm, arm2, test_id);
  CHECK(hil_encode_start_test(&start, b, sizeof(b), &n) == HIL_CODEC_OK &&
        n == 5U);
  CHECK(hil_decode_start_test(b, n, &start2) == HIL_CODEC_OK);
  EQ(start, start2, target_role);
  EQ(start, start2, test_id);
  CHECK(hil_encode_ack(&ack, b, sizeof(b), &n) == HIL_CODEC_OK && n == 6U);
  CHECK(hil_decode_ack(b, n, &ack2) == HIL_CODEC_OK);
  EQ(ack, ack2, acknowledged_sequence);
  EQ(ack, ack2, acknowledged_message_type);
  EQ(ack, ack2, responding_role);
  EQ(ack, ack2, error_code);
  CHECK(hil_encode_nack(&nack, b, sizeof(b), &n) == HIL_CODEC_OK && n == 6U);
  CHECK(hil_decode_nack(b, n, &nack2) == HIL_CODEC_OK);
  EQ(nack, nack2, acknowledged_sequence);
  EQ(nack, nack2, acknowledged_message_type);
  EQ(nack, nack2, responding_role);
  EQ(nack, nack2, error_code);
  CHECK(hil_encode_armed(&armed, b, sizeof(b), &n) == HIL_CODEC_OK && n == 7U);
  CHECK(hil_decode_armed(b, n, &armed2) == HIL_CODEC_OK);
  EQ(armed, armed2, role);
  EQ(armed, armed2, test_id);
  EQ(armed, armed2, readiness_flags);
  CHECK(hil_encode_stop_test(&stop, b, sizeof(b), &n) == HIL_CODEC_OK &&
        n == 6U);
  CHECK(hil_decode_stop_test(b, n, &stop2) == HIL_CODEC_OK);
  EQ(stop, stop2, target_role);
  EQ(stop, stop2, test_id);
  EQ(stop, stop2, stop_reason);
  return 0;
}

static int test_telemetry_and_completion(void) {
  uint8_t b[64];
  size_t n = 0U;
  hil_dut_telemetry_t d = {HIL_ROLE_DUT, 1U, 2U, 3U, 4095U, -12345, 1U, 2U, 0U},
                      d2 = {0};
  hil_plant_telemetry_t p = {HIL_ROLE_PLANT, 4U, 5U,    6U, -20000,
                             60000,          1U, 4000U, 1U},
                        p2 = {0};
  hil_fault_report_t f = {HIL_ROLE_PLANT, 7U, 2U, 0x1234U, 8U}, f2 = {0};
  hil_test_complete_t t = {HIL_ROLE_DUT, 9U, HIL_COMPLETE_FAULT_ABORT, 10U},
                      t2 = {0};
  CHECK(hil_encode_dut_telemetry(&d, b, sizeof(b), &n) == HIL_CODEC_OK &&
        n == 22U);
  CHECK(hil_decode_dut_telemetry(b, n, &d2) == HIL_CODEC_OK);
  EQ(d, d2, role);
  EQ(d, d2, test_id);
  EQ(d, d2, sample_index);
  EQ(d, d2, uptime_ms);
  EQ(d, d2, adc_raw);
  EQ(d, d2, measured_temp_mC);
  EQ(d, d2, heater_cmd);
  EQ(d, d2, controller_state);
  EQ(d, d2, plant_valid);
  CHECK(hil_encode_plant_telemetry(&p, b, sizeof(b), &n) == HIL_CODEC_OK &&
        n == 25U);
  CHECK(hil_decode_plant_telemetry(b, n, &p2) == HIL_CODEC_OK);
  EQ(p, p2, role);
  EQ(p, p2, test_id);
  EQ(p, p2, sample_index);
  EQ(p, p2, uptime_ms);
  EQ(p, p2, simulated_temp_mC);
  EQ(p, p2, ambient_temp_mC);
  EQ(p, p2, heater_input);
  EQ(p, p2, dac_code);
  EQ(p, p2, plant_valid);
  CHECK(hil_encode_fault_report(&f, b, sizeof(b), &n) == HIL_CODEC_OK &&
        n == 13U);
  CHECK(hil_decode_fault_report(b, n, &f2) == HIL_CODEC_OK);
  EQ(f, f2, role);
  EQ(f, f2, test_id);
  EQ(f, f2, fault_code);
  EQ(f, f2, fault_detail);
  EQ(f, f2, uptime_ms);
  CHECK(hil_encode_test_complete(&t, b, sizeof(b), &n) == HIL_CODEC_OK &&
        n == 10U);
  CHECK(hil_decode_test_complete(b, n, &t2) == HIL_CODEC_OK);
  EQ(t, t2, role);
  EQ(t, t2, test_id);
  EQ(t, t2, outcome);
  EQ(t, t2, elapsed_ms);
  return 0;
}

static int test_rejections(void) {
  uint8_t b[64] = {0};
  size_t n = 99U;
  hil_boot_report_t boot = {0};
  hil_status_message_t st = {0};
  hil_dut_config_t d = {1U, 0, 10, 5, 1000U, 1000U, 1U};
  hil_plant_config_t p = {0};
  hil_ack_t a = {0U, HIL_MSG_CONFIG_SET, HIL_ROLE_DUT, 10U};
  hil_stop_test_t stop = {HIL_ROLE_DUT, 1U, 3U};
  hil_test_complete_t tc = {HIL_ROLE_DUT, 1U, 3U, 0U};
  hil_dut_telemetry_t dt = {HIL_ROLE_DUT, 0, 0, 0, 0, 0, 2U, 0, 0};
  hil_plant_telemetry_t pt = {HIL_ROLE_PLANT, 0, 0, 0, 0, 0, 2U, 0, 0};
  hil_armed_t ar = {HIL_ROLE_PLANT, 0U, 0x0008U};
  CHECK(hil_decode_boot_report(b, 11U, &boot) == HIL_CODEC_INVALID_LENGTH);
  b[0] = HIL_ROLE_DUT;
  b[6] = 0x40U;
  CHECK(hil_decode_boot_report(b, 12U, &boot) == HIL_CODEC_INVALID_VALUE);
  memset(b, 0, sizeof(b));
  b[0] = HIL_ROLE_DUT;
  b[1] = 8U;
  CHECK(hil_decode_status(b, 14U, &st) == HIL_CODEC_INVALID_VALUE);
  memset(b, 0, sizeof(b));
  b[0] = HIL_ROLE_PLANT;
  b[13] = 0x04U;
  CHECK(hil_decode_status(b, 14U, &st) == HIL_CODEC_INVALID_VALUE);
  CHECK(hil_encode_dut_config(&d, b, sizeof(b), &n) == HIL_CODEC_INVALID_VALUE);
  d.heat_on_threshold_mC = 0;
  d.heat_off_threshold_mC = 10;
  d.control_period_ms = 999U;
  CHECK(hil_encode_dut_config(&d, b, sizeof(b), &n) == HIL_CODEC_INVALID_VALUE);
  d.control_period_ms = 1000U;
  d.max_runtime_ms = 0U;
  CHECK(hil_encode_dut_config(&d, b, sizeof(b), &n) == HIL_CODEC_INVALID_VALUE);
  p.initial_temp_mC = -20001;
  CHECK(hil_encode_plant_config(&p, b, sizeof(b), &n) ==
        HIL_CODEC_INVALID_VALUE);
  p.initial_temp_mC = 60001;
  CHECK(hil_encode_plant_config(&p, b, sizeof(b), &n) ==
        HIL_CODEC_INVALID_VALUE);
  p.initial_temp_mC = 0;
  p.initial_ambient_mC = -1000000000;
  p.tau_s = 1U;
  p.model_step_ms = 5000U;
  p.max_runtime_ms = 0U;
  CHECK(hil_encode_plant_config(&p, b, sizeof(b), &n) ==
        HIL_CODEC_INVALID_VALUE);
  p.max_runtime_ms = 1U;
  CHECK(hil_encode_plant_config(&p, b, sizeof(b), &n) == HIL_CODEC_OK);
  p.initial_ambient_mC = 20000;
  p.disturbance_count = 5U;
  CHECK(hil_encode_plant_config(&p, b, sizeof(b), &n) ==
        HIL_CODEC_INVALID_VALUE);
  p.disturbance_count = 0U;
  p.tau_s = 0U;
  CHECK(hil_encode_plant_config(&p, b, sizeof(b), &n) ==
        HIL_CODEC_INVALID_VALUE);
  p.tau_s = 1U;
  p.model_step_ms = 4999U;
  CHECK(hil_encode_plant_config(&p, b, sizeof(b), &n) ==
        HIL_CODEC_INVALID_VALUE);
  p.model_step_ms = 5000U;
  p.disturbance_count = 2U;
  p.disturbances[0].offset_ms = 20U;
  p.disturbances[1].offset_ms = 10U;
  CHECK(hil_encode_plant_config(&p, b, sizeof(b), &n) ==
        HIL_CODEC_INVALID_VALUE);
  CHECK(hil_decode_plant_config((const uint8_t[30]){2U}, 30U, &p) ==
        HIL_CODEC_INVALID_VALUE);
  CHECK(hil_decode_plant_config(
            (const uint8_t[30]){2U, 0, 0,    0,    0, 0, 0, 0, 0, 0,
                                0,  0, 0,    0,    0, 0, 0, 0, 0, 0,
                                0,  0, 0x88, 0x13, 0, 0, 1, 0, 0, 0},
            31U, &p) == HIL_CODEC_INVALID_LENGTH);
  CHECK(hil_encode_nack(&a, b, sizeof(b), &n) == HIL_CODEC_INVALID_VALUE);
  CHECK(hil_encode_stop_test(&stop, b, sizeof(b), &n) ==
        HIL_CODEC_INVALID_VALUE);
  CHECK(hil_encode_test_complete(&tc, b, sizeof(b), &n) ==
        HIL_CODEC_INVALID_VALUE);
  CHECK(hil_encode_dut_telemetry(&dt, b, sizeof(b), &n) ==
        HIL_CODEC_INVALID_VALUE);
  pt.heater_input = 0U;
  pt.plant_valid = 2U;
  CHECK(hil_encode_plant_telemetry(&pt, b, sizeof(b), &n) ==
        HIL_CODEC_INVALID_VALUE);
  pt.plant_valid = 0U;
  pt.heater_input = 2U;
  CHECK(hil_encode_plant_telemetry(&pt, b, sizeof(b), &n) ==
        HIL_CODEC_INVALID_VALUE);
  pt.heater_input = 0U;
  pt.dac_code = 4096U;
  CHECK(hil_encode_plant_telemetry(&pt, b, sizeof(b), &n) ==
        HIL_CODEC_INVALID_VALUE);
  dt.heater_cmd = 0U;
  dt.plant_valid = 2U;
  CHECK(hil_encode_dut_telemetry(&dt, b, sizeof(b), &n) ==
        HIL_CODEC_INVALID_VALUE);
  dt.plant_valid = 0U;
  dt.controller_state = 3U;
  CHECK(hil_encode_dut_telemetry(&dt, b, sizeof(b), &n) ==
        HIL_CODEC_INVALID_VALUE);
  CHECK(hil_encode_armed(&ar, b, sizeof(b), &n) == HIL_CODEC_INVALID_VALUE);
  CHECK(hil_decode_dut_telemetry(b, 21U, &dt) == HIL_CODEC_INVALID_LENGTH);
  CHECK(hil_encode_dut_config(NULL, b, sizeof(b), &n) ==
        HIL_CODEC_NULL_ARGUMENT);
  CHECK(hil_decode_status(NULL, 14U, &st) == HIL_CODEC_NULL_ARGUMENT);
  boot.role = HIL_ROLE_DUT;
  boot.capabilities = 0U;
  CHECK(hil_encode_boot_report(&boot, b, sizeof(b), NULL) ==
        HIL_CODEC_NULL_ARGUMENT);
  return 0;
}

int main(void) {
  CHECK(test_boot_and_status_family() == 0);
  CHECK(test_configs() == 0);
  CHECK(test_commands_and_responses() == 0);
  CHECK(test_telemetry_and_completion() == 0);
  CHECK(test_rejections() == 0);
  (void)puts("PASS: all HIL Protocol V1.0 message codec tests");
  return 0;
}
