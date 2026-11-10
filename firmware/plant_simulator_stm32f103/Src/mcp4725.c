#include "mcp4725.h"

HAL_StatusTypeDef mcp4725_init(I2C_HandleTypeDef *i2c) {
    if (i2c == NULL) return HAL_ERROR;
    return HAL_I2C_IsDeviceReady(i2c, MCP4725_ADDRESS_HAL, 2U, MCP4725_IO_TIMEOUT_MS);
}

HAL_StatusTypeDef mcp4725_write_volatile(I2C_HandleTypeDef *i2c, uint16_t code) {
    uint8_t bytes[3];
    if (i2c == NULL || code > 4095U) return HAL_ERROR;
    /* Write DAC register only (C2:C0=010), normal-power mode; EEPROM untouched. */
    bytes[0] = 0x40U;
    bytes[1] = (uint8_t)(code >> 4U);
    bytes[2] = (uint8_t)((code & 0x0FU) << 4U);
    return HAL_I2C_Master_Transmit(i2c, MCP4725_ADDRESS_HAL, bytes, sizeof(bytes),
                                   MCP4725_IO_TIMEOUT_MS);
}

HAL_StatusTypeDef mcp4725_read_volatile(I2C_HandleTypeDef *i2c, uint16_t *code) {
    uint8_t bytes[6];
    HAL_StatusTypeDef result;
    if (i2c == NULL || code == NULL) return HAL_ERROR;
    result = HAL_I2C_Master_Receive(i2c, MCP4725_ADDRESS_HAL, bytes, sizeof(bytes),
                                   MCP4725_IO_TIMEOUT_MS);
    if (result != HAL_OK) return result;
    /* Read mode: byte 2 is status; bytes 3-4 contain live D11:D0. */
    *code = (uint16_t)(((uint16_t)bytes[1] << 4U) | (bytes[2] >> 4U));
    return HAL_OK;
}

HAL_StatusTypeDef mcp4725_write_verify(I2C_HandleTypeDef *i2c, uint16_t code) {
    uint16_t actual;
    HAL_StatusTypeDef result = mcp4725_write_volatile(i2c, code);
    if (result != HAL_OK) return result;
    result = mcp4725_read_volatile(i2c, &actual);
    if (result != HAL_OK) return result;
    return actual == code ? HAL_OK : HAL_ERROR;
}
