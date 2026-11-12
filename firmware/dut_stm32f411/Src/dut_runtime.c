#include "dut_runtime.h"
#include "dut_sensing.h"
#include "hil_board_transport.h"
#include "main.h"
#include <string.h>

extern ADC_HandleTypeDef hadc1;
extern TIM_HandleTypeDef htim2;

#define DUT_CAPABILITIES ((1U << 0) | (1U << 2) | (1U << 4) | (1U << 5))

static hil_protocol_app_t protocol;
/* Retained volatile storage is inspectable as `d` in this translation unit,
   even if the unreferenced getter is removed by linker garbage collection. */
static volatile dut_runtime_snapshot_t d;

void dut_force_safe_output(void) {
    __HAL_RCC_GPIOB_CLK_ENABLE();
    HEATER_CMD_GPIO_Port->BSRR = (uint32_t)HEATER_CMD_Pin << 16U;
}

static uint8_t plant_high(void) {
    return (uint8_t)(HAL_GPIO_ReadPin(PLANT_VALID_GPIO_Port, PLANT_VALID_Pin) == GPIO_PIN_SET);
}

/* Called with TIM2 excluded (foreground critical section or the TIM2 ISR).
   Modular comparison assumes observations less than 2^31 ms apart. The fixed
   phase is advanced only by on-time callbacks, never rebased on a late one. */
static void check_event_deadline(uint32_t now) {
    uint32_t late = now - (d.next_event_ms + DUT_EVENT_JITTER_MS);
    if (d.timer_running && late != 0U && late < 0x80000000U) d.event_late = 1U;
}

static void sync_status(uint32_t now) {
    uint16_t flags = 0U;
    hil_transport_t *t = dut_transport_instance();
    d.plant_valid = plant_high();
    /* LOW or observed expiry permanently removes this sample's qualification.
       A later tick wrap cannot revive it; only a new acquisition can. */
    if (!d.plant_valid || (uint32_t)(now - d.last_sample_ms) > DUT_SAMPLE_MAX_AGE_MS)
        d.sample_plant_valid = 0U;
    d.heater_high = (uint8_t)(HAL_GPIO_ReadPin(HEATER_CMD_GPIO_Port, HEATER_CMD_Pin) == GPIO_PIN_SET);
    d.feedback_valid = (uint8_t)(d.fault_code == 0U && d.sample_plant_valid &&
        (uint32_t)(now - d.last_sample_ms) <= DUT_SAMPLE_MAX_AGE_MS &&
        dut_sensing_feedback_valid(d.adc_raw, d.sample_ok, d.plant_valid));
    if (d.plant_valid) flags |= 1U;
    if (d.heater_high) flags |= 2U;
    if (d.feedback_valid) flags |= 4U;
    if (d.fault_code == 5U) flags |= 16U;
    if (t->tx_control_full || t->tx_telemetry_drops || t->tx_driver_start_errors) flags |= 32U;
    hil_protocol_app_set_status(&protocol, d.fault_code ? HIL_STATE_FAULT : HIL_STATE_WAIT_CONFIG,
                                d.fault_code, 0U, flags);
}

static void latch_fault(uint16_t code, uint16_t detail) {
    uint32_t mask;
    uint8_t payload[13];
    size_t length = 0U;
    hil_fault_report_t report;
    dut_force_safe_output();
    if (d.fault_code) return;
    d.fault_code = code;
    d.fault_detail = detail;
    mask = __get_PRIMASK();
    __disable_irq();
    if (d.timer_running) (void)HAL_TIM_Base_Stop_IT(&htim2);
    d.timer_running = 0U;
    d.pending = 0U;
    d.acquiring = 0U;
    __set_PRIMASK(mask);
    sync_status(HAL_GetTick());
    report.role = HIL_ROLE_DUT;
    report.test_id = 0U;
    report.fault_code = code;
    report.fault_detail = detail;
    report.uptime_ms = HAL_GetTick();
    if (hil_encode_fault_report(&report, payload, sizeof(payload), &length) == HIL_CODEC_OK)
        (void)hil_protocol_app_send_message(&protocol, HIL_MSG_FAULT_REPORT, payload,
                                           (uint16_t)length, HIL_TRANSPORT_CONTROL);
    /* STATUS remains authoritative if the bounded TX queue was full. No test
       is active in 7A, so no TEST_COMPLETE is emitted. Reset clears this fault. */
}

static void acquire(void) {
    HAL_StatusTypeDef start, poll = HAL_ERROR, stop;
    uint32_t raw = 0U, adc_error;
    int32_t temperature;
    uint8_t valid_before = plant_high();
    uint32_t began = HAL_GetTick();
    ++d.sample_attempts;
    d.last_attempt_ms = began;
    d.sample_ok = 0U;
    d.sample_plant_valid = 0U;
    d.start_status = (uint8_t)HAL_ERROR;
    d.poll_status = (uint8_t)HAL_ERROR;
    d.stop_status = (uint8_t)HAL_ERROR;

    start = HAL_ADC_Start(&hadc1);
    if (start == HAL_OK) {
        poll = HAL_ADC_PollForConversion(&hadc1, DUT_ADC_TIMEOUT_MS);
        if (poll == HAL_OK) raw = HAL_ADC_GetValue(&hadc1);
    }
    adc_error = HAL_ADC_GetError(&hadc1);
    /* Cleanup is attempted on every path, including start/poll failure. */
    stop = HAL_ADC_Stop(&hadc1);
    d.start_status = (uint8_t)start;
    d.poll_status = (uint8_t)poll;
    d.stop_status = (uint8_t)stop;
    d.adc_error = adc_error | HAL_ADC_GetError(&hadc1);
    if (start != HAL_OK) d.sample_result = DUT_SAMPLE_START_ERROR;
    else if (poll == HAL_TIMEOUT) d.sample_result = DUT_SAMPLE_TIMEOUT;
    else if (poll != HAL_OK) d.sample_result = DUT_SAMPLE_POLL_ERROR;
    else if (d.adc_error != HAL_ADC_ERROR_NONE) d.sample_result = DUT_SAMPLE_ADC_ERROR;
    else if (stop != HAL_OK) d.sample_result = DUT_SAMPLE_STOP_ERROR;
    else if (!dut_sensing_raw_to_temperature(raw, &temperature)) d.sample_result = DUT_SAMPLE_RAW_ERROR;
    else {
        d.adc_raw = raw;
        d.temperature_mC = temperature;
        d.last_sample_ms = HAL_GetTick();
        ++d.samples_acquired;
        d.sample_ok = 1U;
        d.sample_plant_valid = (uint8_t)(valid_before && plant_high());
        if (dut_sensing_feedback_valid(raw, 1U, 1U)) d.sample_result = DUT_SAMPLE_OK;
        else { d.sample_result = DUT_SAMPLE_OUT_OF_RANGE; ++d.range_errors; }
    }
    if (!d.sample_ok) ++d.acquisition_errors;
    d.last_acquisition_ms = HAL_GetTick() - began;
    if (d.last_acquisition_ms > d.max_acquisition_ms) d.max_acquisition_ms = d.last_acquisition_ms;
}

void dut_runtime_init(void) {
    uint32_t mask;
    dut_force_safe_output();
    memset((void *)&d, 0, sizeof(d));
    if (hil_protocol_app_init(&protocol, dut_transport_instance(), HIL_ROLE_DUT,
                              (uint16_t)DUT_CAPABILITIES, HAL_GetTick()) != HIL_CODEC_OK) {
        Error_Handler();
        return;
    }
    hil_protocol_app_set_firmware_version(&protocol, 0U, 7U, 0U);
    sync_status(HAL_GetTick());
    mask = __get_PRIMASK();
    __disable_irq();
    __HAL_TIM_SET_COUNTER(&htim2, 0U);
    __HAL_TIM_CLEAR_IT(&htim2, TIM_IT_UPDATE);
    HAL_NVIC_ClearPendingIRQ(TIM2_IRQn);
    d.next_event_ms = HAL_GetTick() + DUT_ACQUISITION_PERIOD_MS;
    if (HAL_TIM_Base_Start_IT(&htim2) == HAL_OK) d.timer_running = 1U;
    __set_PRIMASK(mask);
    if (!d.timer_running) latch_fault(6U, 1U);
}

void dut_runtime_service(void) {
    uint32_t mask;
    uint8_t take_sample = 0U, overlap, event_late;
    dut_force_safe_output();
    /* Claim pending -> acquiring atomically with respect to TIM2. */
    mask = __get_PRIMASK();
    __disable_irq();
    check_event_deadline(HAL_GetTick());
    event_late = d.event_late;
    overlap = d.overlap;
    if (!d.fault_code && !event_late && !overlap && d.pending) {
        d.pending = 0U;
        d.acquiring = 1U;
        take_sample = 1U;
    }
    __set_PRIMASK(mask);
    if (event_late) latch_fault(5U, 3U);
    else if (overlap) latch_fault(5U, 1U);
    if (take_sample) {
        acquire();
        mask = __get_PRIMASK();
        __disable_irq();
        d.acquiring = 0U;
        check_event_deadline(HAL_GetTick());
        event_late = d.event_late;
        overlap = d.overlap;
        __set_PRIMASK(mask);
        if (event_late) latch_fault(5U, 3U);
        else if (overlap) latch_fault(5U, 1U);
        else if (d.last_acquisition_ms > DUT_ACQUISITION_BUDGET_MS) latch_fault(5U, 2U);
    }
    sync_status(HAL_GetTick());
    hil_protocol_app_set_transport_ready(&protocol, dut_transport_ready());
    hil_protocol_app_service(&protocol, HAL_GetTick());
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *timer) {
    if (timer->Instance != TIM2 || !d.timer_running) return;
    ++d.timer_events;
    check_event_deadline(HAL_GetTick());
    if (d.event_late) return;
    d.next_event_ms += DUT_ACQUISITION_PERIOD_MS;
    if (d.pending || d.acquiring) {
        ++d.overlap_events;
        d.overlap = 1U;
    } else d.pending = 1U;
}

const hil_protocol_app_t *dut_runtime_protocol(void) { return &protocol; }
const volatile dut_runtime_snapshot_t *dut_runtime_snapshot(void) { return &d; }
