#ifndef DUT_SENSING_H
#define DUT_SENSING_H

#include <stdint.h>

#define DUT_ADC_MAX_RAW 4095U
/* Provisional plausibility margin, not a measured calibration tolerance. */
#define DUT_ADC_MARGIN_COUNTS 32U
#define DUT_ADC_MIN_VALID_RAW (100U - DUT_ADC_MARGIN_COUNTS)
#define DUT_ADC_MAX_VALID_RAW (4000U + DUT_ADC_MARGIN_COUNTS)

/* Nominal V_DAC_SUPPLY / VREF+ = 1. A future measured gain/offset correction
   belongs here, before inversion of the agreed DAC mapping. No clamping:
   margin samples can report slightly outside -20000..60000 mC. */
uint8_t dut_sensing_raw_to_temperature(uint32_t raw, int32_t *temperature_mC);
uint8_t dut_sensing_feedback_valid(uint32_t raw, uint8_t sample_ok,
                                   uint8_t plant_valid);

#endif
