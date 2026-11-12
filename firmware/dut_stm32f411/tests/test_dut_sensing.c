#include "dut_sensing.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>

int main(void) {
    static const struct { uint32_t raw; int32_t temperature; } cases[] = {
        {0U, -22051}, {68U, -20656}, {100U, -20000}, {1075U, 0},
        {2048U, 19959}, {2050U, 20000}, {3025U, 40000},
        {4000U, 60000}, {4032U, 60656}, {4095U, 61949}
    };
    uint32_t raw;
    unsigned i;
    int32_t temperature = 12345;
    for (i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        assert(dut_sensing_raw_to_temperature(cases[i].raw, &temperature));
        assert(temperature == cases[i].temperature);
    }
    /* Independent floating reference over every 12-bit input, including
       negative temperature rounding. Inverse mapping error <= half a mC. */
    for (raw = 0U; raw <= 4095U; ++raw) {
        double reference = -20000.0 + ((double)raw - 100.0) * 80000.0 / 3900.0;
        double error;
        assert(dut_sensing_raw_to_temperature(raw, &temperature));
        error = (double)temperature - reference;
        assert(error >= -0.5 && error <= 0.5);
        assert(dut_sensing_feedback_valid(raw, 1U, 1U) == (raw >= 68U && raw <= 4032U));
        assert(!dut_sensing_feedback_valid(raw, 0U, 1U));
        assert(!dut_sensing_feedback_valid(raw, 1U, 0U));
    }
    /* Historical endpoint observations are accepted, not fitted/calibrated. */
    assert(dut_sensing_feedback_valid(97U, 1U, 1U));
    assert(dut_sensing_feedback_valid(111U, 1U, 1U));
    assert(dut_sensing_feedback_valid(3971U, 1U, 1U));
    assert(dut_sensing_feedback_valid(3993U, 1U, 1U));
    temperature = 12345;
    assert(!dut_sensing_raw_to_temperature(4096U, &temperature));
    assert(!dut_sensing_raw_to_temperature(UINT32_MAX, &temperature));
    assert(temperature == 12345);
    assert(!dut_sensing_raw_to_temperature(2050U, NULL));
    assert(!dut_sensing_feedback_valid(UINT32_MAX, 1U, 1U));
    puts("DUT nominal conversion and plausibility tests passed");
    return 0;
}
