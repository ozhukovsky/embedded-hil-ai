#include "dut_sensing.h"

uint8_t dut_sensing_raw_to_temperature(uint32_t raw, int32_t *temperature_mC) {
    int64_t numerator;
    if (temperature_mC == 0 || raw > DUT_ADC_MAX_RAW) return 0U;
    /* ADC_raw ~= 4096 * Vin / VREF+; Vin = V_DAC_SUPPLY * D / 4096.
       Thus raw ~= D at the nominal reference ratio; never scale by 4095.
       T_mC = -20000 + (D - 100) * 80000 / 3900. Round to nearest mC,
       with exact half values away from zero, using signed intermediates. */
    numerator = ((int64_t)raw - 100) * 80000 - (int64_t)20000 * 3900;
    numerator += numerator >= 0 ? 1950 : -1950;
    *temperature_mC = (int32_t)(numerator / 3900);
    return 1U;
}

uint8_t dut_sensing_feedback_valid(uint32_t raw, uint8_t sample_ok,
                                   uint8_t plant_valid) {
    return (uint8_t)(sample_ok != 0U && plant_valid != 0U &&
                     raw >= DUT_ADC_MIN_VALID_RAW && raw <= DUT_ADC_MAX_VALID_RAW);
}
