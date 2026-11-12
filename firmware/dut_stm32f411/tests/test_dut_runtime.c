#include "dut_runtime.h"
#include "hil_board_transport.h"
#include "main.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

GPIO_TypeDef test_gpio;
uint32_t test_timer_counter, test_timer_cleared;
int test_timer_instance;
ADC_HandleTypeDef hadc1;
TIM_HandleTypeDef htim2 = {TIM2};
static hil_transport_t transport;
static uint32_t now, raw, adc_error, irq_mask, poll_delay;
static uint8_t plant, heater, interrupt_during_poll, plant_loss_during_poll;
static uint8_t usb_ready;
static unsigned stop_calls;
static HAL_StatusTypeDef start_result, poll_result, stop_result, timer_result;

hil_transport_t *dut_transport_instance(void) { return &transport; }
uint8_t dut_transport_ready(void) { return usb_ready; }
uint32_t __get_PRIMASK(void) { return irq_mask; }
void __disable_irq(void) { irq_mask = 1U; }
void __set_PRIMASK(uint32_t value) { irq_mask = value; }
uint32_t HAL_GetTick(void) { return now; }
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin) {
    assert(port == &test_gpio);
    return (pin == PLANT_VALID_Pin ? plant : heater) ? GPIO_PIN_SET : GPIO_PIN_RESET;
}
HAL_StatusTypeDef HAL_ADC_Start(ADC_HandleTypeDef *adc) { assert(adc == &hadc1); return start_result; }
HAL_StatusTypeDef HAL_ADC_PollForConversion(ADC_HandleTypeDef *adc, uint32_t timeout) {
    assert(adc == &hadc1 && timeout == 5U && irq_mask == 0U);
    now += poll_delay;
    if (plant_loss_during_poll) plant = 0U;
    if (interrupt_during_poll) HAL_TIM_PeriodElapsedCallback(&htim2);
    return poll_result;
}
uint32_t HAL_ADC_GetValue(ADC_HandleTypeDef *adc) { assert(adc == &hadc1); return raw; }
uint32_t HAL_ADC_GetError(ADC_HandleTypeDef *adc) { assert(adc == &hadc1); return adc_error; }
HAL_StatusTypeDef HAL_ADC_Stop(ADC_HandleTypeDef *adc) { assert(adc == &hadc1); ++stop_calls; return stop_result; }
HAL_StatusTypeDef HAL_TIM_Base_Start_IT(TIM_HandleTypeDef *timer) { assert(timer == &htim2); return timer_result; }
HAL_StatusTypeDef HAL_TIM_Base_Stop_IT(TIM_HandleTypeDef *timer) { assert(timer == &htim2); return HAL_OK; }
void HAL_NVIC_ClearPendingIRQ(int irq) { assert(irq == TIM2_IRQn); }
void Error_Handler(void) { assert(!"unexpected initialization failure"); }

static const volatile dut_runtime_snapshot_t *snapshot(void) { return dut_runtime_snapshot(); }
static void service(void) {
    /* Model completed USB transfers between calls; a blocked transport is
       exercised separately rather than inadvertently filling the TX queue. */
    while (hil_transport_tx_pop(&transport) == HIL_TRANSPORT_OK) { }
    dut_runtime_service();
}
static void reset_at(uint32_t start_ms) {
    hil_transport_init(&transport);
    memset(&test_gpio, 0, sizeof(test_gpio));
    now = start_ms; raw = 2050U; adc_error = 0U; irq_mask = 0U; poll_delay = 0U;
    plant = 0U; heater = 0U; interrupt_during_poll = 0U; plant_loss_during_poll = 0U; stop_calls = 0U;
    usb_ready = 1U;
    start_result = HAL_OK; poll_result = HAL_OK; stop_result = HAL_OK; timer_result = HAL_OK;
    dut_runtime_init();
    assert(snapshot()->timer_running && test_timer_counter == 0U && test_timer_cleared == TIM_IT_UPDATE);
    assert(test_gpio.BSRR == (HEATER_CMD_Pin << 16U));
    assert(dut_runtime_protocol()->state == HIL_STATE_WAIT_CONFIG);
    assert(dut_runtime_protocol()->capabilities == 0x0035U);
}
static void reset(void) { reset_at(0U); }
static void sample(void) {
    now += 1000U;
    HAL_TIM_PeriodElapsedCallback(&htim2);
    service();
    assert(irq_mask == 0U);
}
static void test_status_and_validity(void) {
    reset();
    sample();
    assert(snapshot()->sample_attempts == 1U && snapshot()->samples_acquired == 1U);
    assert(snapshot()->adc_raw == 2050U && snapshot()->temperature_mC == 20000);
    assert(!snapshot()->feedback_valid && !dut_runtime_protocol()->status_flags);
    plant = 1U;
    service();
    assert(dut_runtime_protocol()->status_flags == 1U); /* no retroactive validity */
    sample();
    assert(dut_runtime_protocol()->status_flags == 5U);
    plant = 0U;
    service();
    assert(!snapshot()->feedback_valid && dut_runtime_protocol()->state == HIL_STATE_WAIT_CONFIG);
    plant = 1U;
    service();
    assert(dut_runtime_protocol()->status_flags == 1U);
    sample();
    now += 1101U;
    service();
    assert(!snapshot()->feedback_valid && snapshot()->fault_detail == 3U);
    heater = 1U; /* observed line HIGH must be reported despite the LOW request */
    service();
    assert(dut_runtime_protocol()->status_flags == 19U); /* live GPIO plus timing latch */
    reset(); plant = 1U; plant_loss_during_poll = 1U; sample();
    assert(!snapshot()->sample_plant_valid && !snapshot()->feedback_valid);
}
static void test_adc_failures_and_retry(void) {
    reset(); plant = 1U;
    sample();
    poll_result = HAL_TIMEOUT;
    sample();
    assert(snapshot()->sample_result == DUT_SAMPLE_TIMEOUT && !snapshot()->feedback_valid);
    assert(snapshot()->adc_raw == 2050U && snapshot()->samples_acquired == 1U);
    assert(snapshot()->acquisition_errors == 1U && stop_calls == 2U);
    poll_result = HAL_ERROR; sample();
    assert(snapshot()->sample_result == DUT_SAMPLE_POLL_ERROR);
    poll_result = HAL_OK; start_result = HAL_BUSY; sample();
    assert(snapshot()->sample_result == DUT_SAMPLE_START_ERROR && stop_calls == 4U);
    start_result = HAL_OK; stop_result = HAL_ERROR; sample();
    assert(snapshot()->sample_result == DUT_SAMPLE_STOP_ERROR);
    stop_result = HAL_OK; adc_error = 1U; sample();
    assert(snapshot()->sample_result == DUT_SAMPLE_ADC_ERROR && snapshot()->adc_error == 1U);
    adc_error = 0U; raw = 4096U; sample();
    assert(snapshot()->sample_result == DUT_SAMPLE_RAW_ERROR);
    raw = 0U; sample();
    assert(snapshot()->sample_result == DUT_SAMPLE_OUT_OF_RANGE && snapshot()->range_errors == 1U);
    assert(!snapshot()->feedback_valid);
    raw = 97U; sample();
    assert(snapshot()->feedback_valid && snapshot()->fault_code == 0U);
    assert(dut_runtime_protocol()->state == HIL_STATE_WAIT_CONFIG);
}
static void test_scheduler_faults(void) {
    reset();
    HAL_TIM_PeriodElapsedCallback(&htim2);
    HAL_TIM_PeriodElapsedCallback(&htim2);
    service();
    assert(snapshot()->timer_events == 2U && snapshot()->overlap_events == 1U);
    assert(snapshot()->sample_attempts == 0U && !snapshot()->timer_running);
    assert(snapshot()->fault_code == 5U && snapshot()->fault_detail == 1U);
    assert(dut_runtime_protocol()->state == HIL_STATE_FAULT);
    assert(dut_runtime_protocol()->status_flags == 16U);
    service();
    assert(snapshot()->sample_attempts == 0U); /* no catch-up */
    reset(); interrupt_during_poll = 1U; sample();
    assert(snapshot()->fault_code == 5U && !snapshot()->feedback_valid);
    reset(); poll_delay = 101U; sample();
    assert(snapshot()->fault_code == 5U && snapshot()->fault_detail == 2U);
    assert(snapshot()->max_acquisition_ms == 101U);
    reset(); timer_result = HAL_ERROR; dut_runtime_init();
    assert(snapshot()->fault_code == 6U && !snapshot()->timer_running);
}
static void test_wrap_and_status_wire(void) {
    hil_transport_frame_t request = {HIL_PROTOCOL_VERSION, HIL_MSG_STATUS_REQUEST, 0U, 0U, {0U}};
    hil_transport_wire_frame_t wire;
    hil_transport_priority_t priority;
    hil_status_message_t status;
    reset_at(UINT32_MAX - 1499U);
    plant = 1U; sample(); /* genuinely fresh sample shortly before wrap */
    now += 1000U; service();
    assert(snapshot()->feedback_valid);
    while (hil_transport_tx_pop(&transport) == HIL_TRANSPORT_OK) { }
    transport.rx_frames[0] = request; transport.rx_frame_count = 1U; transport.rx_frame_head = 0U;
    service();
    assert(hil_transport_tx_peek(&transport, &wire, &priority) == HIL_TRANSPORT_OK);
    assert(wire.bytes[3] == HIL_MSG_STATUS);
    assert(hil_decode_status(wire.bytes + 8, 14U, &status) == HIL_CODEC_OK);
    assert(status.role == HIL_ROLE_DUT && status.state == HIL_STATE_WAIT_CONFIG);
    assert(status.status_flags == 5U && status.test_id == 0U && status.fault_code == 0U);
}
static void test_blocked_tx_does_not_block_sensing(void) {
    reset(); plant = 1U;
    now = 1000U;
    HAL_TIM_PeriodElapsedCallback(&htim2); dut_runtime_service(); /* boot + heartbeat fill TX */
    now = 2000U;
    HAL_TIM_PeriodElapsedCallback(&htim2); dut_runtime_service(); /* heartbeat enqueue fails */
    now = 2001U; dut_runtime_service();
    assert(snapshot()->samples_acquired == 2U && snapshot()->feedback_valid);
    assert(transport.tx_control_full != 0U);
    assert(dut_runtime_protocol()->status_flags == 37U);
    assert(!snapshot()->fault_code && test_gpio.BSRR == (HEATER_CMD_Pin << 16U));
}
static void inject_status_request(uint16_t sequence) {
    uint8_t bytes[HIL_PROTOCOL_MAX_FRAME];
    size_t length = 0U;
    hil_frame_t frame = {HIL_PROTOCOL_VERSION, HIL_MSG_STATUS_REQUEST, sequence, 0U, NULL};
    assert(hil_frame_encode(&frame, bytes, sizeof(bytes), &length) == HIL_CODEC_OK);
    assert(hil_transport_rx_push_isr(&transport, bytes, (uint16_t)length) == length);
    assert(hil_transport_rx_service(&transport, HIL_TRANSPORT_RX_BUDGET) == length);
}
static void test_missing_later_event(void) {
    uint32_t attempts;
    reset(); plant = 1U; sample();
    attempts = snapshot()->sample_attempts;
    now = 5101U; service();
    printf("missing later event: fault=%u, qualified=%u\n",
           (unsigned)snapshot()->fault_code, (unsigned)snapshot()->sample_plant_valid);
    fflush(stdout);
    assert(snapshot()->fault_code == 5U);
    assert(snapshot()->fault_detail == 3U);
    HAL_TIM_PeriodElapsedCallback(&htim2); service();
    assert(snapshot()->sample_attempts == attempts && !snapshot()->timer_running);
}
static void test_missing_first_event(void) {
    reset();
    now = 1010U; service(); /* inclusive delivery tolerance */
    assert(!snapshot()->fault_code && snapshot()->sample_attempts == 0U);
    now = 1011U; service();
    assert(snapshot()->fault_code == 5U && snapshot()->fault_detail == 3U);
    assert(snapshot()->event_late && snapshot()->timer_events == 0U);
    assert(snapshot()->next_event_ms == 1000U && !snapshot()->timer_running);
    assert(test_gpio.BSRR == (HEATER_CMD_Pin << 16U));
}
static void test_late_callback_cannot_rebase_deadline(void) {
    reset();
    now = 1011U; HAL_TIM_PeriodElapsedCallback(&htim2); service();
    assert(snapshot()->fault_code == 5U && snapshot()->fault_detail == 3U);
    assert(snapshot()->next_event_ms == 1000U && snapshot()->sample_attempts == 0U);
    reset(); sample();
    now = 2011U; HAL_TIM_PeriodElapsedCallback(&htim2); service();
    assert(snapshot()->fault_code == 5U && snapshot()->fault_detail == 3U);
    assert(snapshot()->next_event_ms == 2000U && snapshot()->sample_attempts == 1U);
}
static void test_event_jitter_preserves_phase(void) {
    static const uint32_t arrivals[] = {1007U, 1999U, 3010U, 3993U};
    unsigned i;
    reset();
    for (i = 0U; i < sizeof(arrivals) / sizeof(arrivals[0]); ++i) {
        now = arrivals[i]; HAL_TIM_PeriodElapsedCallback(&htim2); service();
        assert(snapshot()->fault_code == 0U && snapshot()->sample_attempts == i + 1U);
        assert(snapshot()->next_event_ms == (i + 2U) * 1000U);
    }
}
static void test_event_deadline_wrap(void) {
    reset_at(UINT32_MAX - 499U); /* first expected event is at tick 500 */
    now = 510U; service();
    assert(!snapshot()->fault_code);
    now = 511U; service();
    assert(snapshot()->fault_code == 5U && snapshot()->fault_detail == 3U);
    reset_at(UINT32_MAX - 1499U); sample();
    now = 511U; HAL_TIM_PeriodElapsedCallback(&htim2); service();
    assert(snapshot()->fault_code == 5U && snapshot()->next_event_ms == 500U);
}
static void test_status_before_first_tx_completion_with_readiness(uint8_t ready) {
    unsigned i, status_count = 0U;
    uint16_t expected_sequence = 0U;
    hil_transport_wire_frame_t frame;
    hil_transport_priority_t priority;
    reset(); usb_ready = ready;
    dut_runtime_service();
    now = 1000U; HAL_TIM_PeriodElapsedCallback(&htim2); dut_runtime_service();
    inject_status_request(42U);
    now = 1001U; dut_runtime_service(); /* no TX completion has occurred */
    assert(dut_runtime_protocol()->status_pending == 1U);
    if (!ready) {
        assert(transport.control.count == 0U && dut_runtime_protocol()->boot_pending);
        assert(dut_runtime_protocol()->tx_sequence.next_sequence == 0U);
    } else assert(transport.control.count == HIL_TRANSPORT_TX_CONTROL_CAPACITY);
    usb_ready = 1U;
    for (i = 0U; i < 12U; ++i) {
        ++now; dut_runtime_service();
        if (hil_transport_tx_peek(&transport, &frame, &priority) != HIL_TRANSPORT_OK) continue;
        assert((uint16_t)(frame.bytes[4] | ((uint16_t)frame.bytes[5] << 8U)) == expected_sequence++);
        if (frame.bytes[3] == HIL_MSG_STATUS) {
            hil_status_message_t status;
            ++status_count;
            assert(hil_decode_status(frame.bytes + 8, 14U, &status) == HIL_CODEC_OK);
            assert(status.uptime_ms >= 1002U && status.state == HIL_STATE_WAIT_CONFIG);
        }
        assert(hil_transport_tx_pop_priority(&transport, priority) == HIL_TRANSPORT_OK);
    }
    printf("STATUS before first completion (initial readiness=%u): responses=%u\n",
           (unsigned)ready, status_count);
    fflush(stdout);
    assert(status_count == 1U);
    assert(!dut_runtime_protocol()->status_pending);
}
static void test_status_before_first_tx_completion(void) {
    test_status_before_first_tx_completion_with_readiness(0U);
    /* Configured CDC does not imply a reading host; retain the response even
       when boot/heartbeat really do fill the control queue before completion. */
    test_status_before_first_tx_completion_with_readiness(1U);
}
static void test_expiration_survives_full_tick_cycle(void) {
    reset(); plant = 1U; sample();
    now = 2101U; service();
    assert(!snapshot()->feedback_valid);
    assert(!snapshot()->sample_plant_valid); /* independent of fault gating */
    now = 1001U; service(); /* +2^32 ms relative to the original sample epoch */
    printf("expired sample after full tick cycle: valid=%u, qualified=%u\n",
           (unsigned)snapshot()->feedback_valid, (unsigned)snapshot()->sample_plant_valid);
    fflush(stdout);
    assert(!snapshot()->feedback_valid);
    assert(!snapshot()->sample_plant_valid);
    assert(snapshot()->adc_raw == 2050U && snapshot()->last_sample_ms == 1000U);
    /* Missing-event faults require reset; then a fresh qualified sample works. */
    reset(); plant = 1U; sample();
    assert(snapshot()->feedback_valid);
}
int main(int argc, char **argv) {
    if (argc == 2) {
        if (strcmp(argv[1], "audit-a") == 0) {
            test_missing_first_event();
            test_missing_later_event();
            test_late_callback_cannot_rebase_deadline();
            test_event_jitter_preserves_phase();
            test_event_deadline_wrap();
        }
        else if (strcmp(argv[1], "audit-b") == 0) test_status_before_first_tx_completion();
        else if (strcmp(argv[1], "audit-c") == 0) test_expiration_survives_full_tick_cycle();
        else return 2;
        return 0;
    }
    test_status_and_validity();
    test_adc_failures_and_retry();
    test_scheduler_faults();
    test_wrap_and_status_wire();
    test_blocked_tx_does_not_block_sensing();
    test_missing_first_event();
    test_missing_later_event();
    test_late_callback_cannot_rebase_deadline();
    test_event_jitter_preserves_phase();
    test_event_deadline_wrap();
    test_status_before_first_tx_completion();
    test_expiration_survives_full_tick_cycle();
    puts("DUT runtime host tests passed (mock HAL, no hardware verification)");
    return 0;
}
