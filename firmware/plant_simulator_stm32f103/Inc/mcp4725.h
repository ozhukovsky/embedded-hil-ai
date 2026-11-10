#ifndef MCP4725_H
#define MCP4725_H
#include "main.h"
#include <stdint.h>

#define MCP4725_ADDRESS_7BIT 0x60U
#define MCP4725_ADDRESS_HAL (MCP4725_ADDRESS_7BIT << 1U)
#define MCP4725_IO_TIMEOUT_MS 20U

HAL_StatusTypeDef mcp4725_init(I2C_HandleTypeDef *i2c);
HAL_StatusTypeDef mcp4725_write_volatile(I2C_HandleTypeDef *i2c,
                                         uint16_t code);
HAL_StatusTypeDef mcp4725_read_volatile(I2C_HandleTypeDef *i2c,
                                        uint16_t *code);
HAL_StatusTypeDef mcp4725_write_verify(I2C_HandleTypeDef *i2c,
                                       uint16_t code);
#endif
