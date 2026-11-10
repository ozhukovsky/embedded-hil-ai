#include "hil_plant_runtime.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static void test_low_high_low_sequence(void) {
    hil_plant_heater_diag_t diag = {0U};
    hil_plant_heater_diag_arm(&diag, 0U);
    assert(!hil_plant_heater_diag_complete(&diag));
    hil_plant_heater_diag_edge(&diag, 0U);
    assert(!hil_plant_heater_diag_complete(&diag));
    hil_plant_heater_diag_edge(&diag, 1U);
    assert(!hil_plant_heater_diag_complete(&diag));
    hil_plant_heater_diag_edge(&diag, 1U);
    assert(!hil_plant_heater_diag_complete(&diag));
    hil_plant_heater_diag_edge(&diag, 0U);
    assert(hil_plant_heater_diag_complete(&diag));
    hil_plant_heater_diag_edge(&diag, 1U);
    assert(hil_plant_heater_diag_complete(&diag));
}

static void test_high_baseline_requires_low(void) {
    hil_plant_heater_diag_t diag = {0U};
    hil_plant_heater_diag_arm(&diag, 1U);
    hil_plant_heater_diag_edge(&diag, 1U);
    assert(!hil_plant_heater_diag_complete(&diag));
    hil_plant_heater_diag_edge(&diag, 0U);
    assert(!hil_plant_heater_diag_complete(&diag));
    hil_plant_heater_diag_edge(&diag, 0U);
    assert(!hil_plant_heater_diag_complete(&diag));
    hil_plant_heater_diag_edge(&diag, 1U);
    assert(!hil_plant_heater_diag_complete(&diag));
    hil_plant_heater_diag_edge(&diag, 0U);
    assert(hil_plant_heater_diag_complete(&diag));
}

static void test_incomplete_sequences_and_disarmed_edges(void) {
    hil_plant_heater_diag_t diag = {0U};
    hil_plant_heater_diag_edge(&diag, 1U);
    hil_plant_heater_diag_edge(&diag, 0U);
    assert(!hil_plant_heater_diag_complete(&diag));

    hil_plant_heater_diag_arm(&diag, 0U);
    hil_plant_heater_diag_edge(&diag, 1U);
    assert(!hil_plant_heater_diag_complete(&diag));

    hil_plant_heater_diag_arm(&diag, 1U);
    hil_plant_heater_diag_edge(&diag, 0U);
    assert(!hil_plant_heater_diag_complete(&diag));
}

static void test_reset_clears_success(void) {
    hil_plant_heater_diag_t diag = {0U};
    hil_plant_heater_diag_arm(&diag, 0U);
    hil_plant_heater_diag_edge(&diag, 1U);
    hil_plant_heater_diag_edge(&diag, 0U);
    assert(hil_plant_heater_diag_complete(&diag));
    hil_plant_heater_diag_disarm(&diag);
    assert(!hil_plant_heater_diag_complete(&diag));
    hil_plant_heater_diag_edge(&diag, 1U);
    assert(!hil_plant_heater_diag_complete(&diag));
    hil_plant_heater_diag_arm(&diag, 1U);
    assert(!hil_plant_heater_diag_complete(&diag));
}

static void test_runtime_deadline_independent_of_model_period(void) {
    const uint32_t start = 1000U;
    assert(!hil_plant_runtime_deadline_expired(1U, start, start + 1999U, 2000U));
    assert(hil_plant_runtime_deadline_expired(1U, start, start + 2000U, 2000U));
    assert(hil_plant_runtime_deadline_expired(1U, start, start + 5000U, 2000U));
    assert(!hil_plant_runtime_deadline_expired(1U, start, start + 1999U, 5000U));
    assert(!hil_plant_runtime_deadline_expired(0U, start, start + 5000U, 2000U));
}

static void test_runtime_deadline_tick_wraparound(void) {
    const uint32_t start = UINT32_MAX - 999U;
    assert(!hil_plant_runtime_deadline_expired(1U, start, start + 1999U, 2000U));
    assert(hil_plant_runtime_deadline_expired(1U, start, start + 2000U, 2000U));
}

int main(void) {
    test_low_high_low_sequence();
    test_high_baseline_requires_low();
    test_incomplete_sequences_and_disarmed_edges();
    test_reset_clears_success();
    test_runtime_deadline_independent_of_model_period();
    test_runtime_deadline_tick_wraparound();
    puts("Plant runtime logic tests passed");
    return 0;
}
