#ifndef DUT_RUNTIME_H
#define DUT_RUNTIME_H

#include "hil_protocol_app.h"
#include <stdint.h>

#define DUT_ADC_TIMEOUT_MS 5U
#define DUT_ACQUISITION_PERIOD_MS 1000U
/* Ten HAL ticks accommodate delivery/quantization jitter, not a task overrun.
   Provisional until measured on hardware; distinct from the 100 ms budget. */
#define DUT_EVENT_JITTER_MS 10U
#define DUT_ACQUISITION_BUDGET_MS 100U
#define DUT_SAMPLE_MAX_AGE_MS (DUT_ACQUISITION_PERIOD_MS + DUT_ACQUISITION_BUDGET_MS)

typedef enum {
    DUT_SAMPLE_NONE = 0,
    DUT_SAMPLE_OK,
    DUT_SAMPLE_START_ERROR,
    DUT_SAMPLE_TIMEOUT,
    DUT_SAMPLE_POLL_ERROR,
    DUT_SAMPLE_ADC_ERROR,
    DUT_SAMPLE_STOP_ERROR,
    DUT_SAMPLE_RAW_ERROR,
    DUT_SAMPLE_OUT_OF_RANGE
} dut_sample_result_t;

/* RAM-only debugger snapshot. last_sample_ms/raw/temperature describe the
   last error-free acquisition, even when out of the plausibility range.
   sample_ok clears on any acquisition failure; retained values are then stale.
   Halt after the operation to inspect a coherent multi-field snapshot. */
typedef struct {
    uint32_t timer_events, overlap_events;
    uint32_t next_event_ms;
    uint32_t sample_attempts, samples_acquired, acquisition_errors, range_errors;
    uint32_t last_attempt_ms, last_sample_ms, adc_raw;
    int32_t temperature_mC;
    uint32_t last_acquisition_ms, max_acquisition_ms;
    uint32_t adc_error;
    uint8_t start_status, poll_status, stop_status;
    uint8_t sample_result, sample_ok, sample_plant_valid;
    uint8_t plant_valid, heater_high, feedback_valid;
    uint8_t timer_running, pending, acquiring, overlap, event_late;
    uint16_t fault_code, fault_detail;
} dut_runtime_snapshot_t;

/* Best effort, including from exception handlers; no HAL tick or RAM state
   required. Cannot guarantee safety after CPU/bus/GPIO/power hardware failure. */
void dut_force_safe_output(void);
void dut_runtime_init(void);
void dut_runtime_service(void);
const hil_protocol_app_t *dut_runtime_protocol(void);
const volatile dut_runtime_snapshot_t *dut_runtime_snapshot(void);

#endif
