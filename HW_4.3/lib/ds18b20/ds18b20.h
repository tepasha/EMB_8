#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"
#include "onewire.h"

#define DS18B20_CMD_SKIP_ROM     0xCC
#define DS18B20_CMD_CONVERT_T    0x44
#define DS18B20_CMD_READ_SCRATCH 0xBE
#define DS18B20_DISCONNECTED     -127.0f

// Kick off a temperature conversion.
static inline bool ds18b20_start_conversion(gpio_num_t pin) {
    if (!ow_reset(pin)) return false;
    ow_write_byte(pin, DS18B20_CMD_SKIP_ROM);
    ow_write_byte(pin, DS18B20_CMD_CONVERT_T);
    return true;
}

// Read scratchpad and return temperature in °C.
static inline float ds18b20_read_temp(gpio_num_t pin) {
    if (!ow_reset(pin)) return DS18B20_DISCONNECTED;
    ow_write_byte(pin, DS18B20_CMD_SKIP_ROM);
    ow_write_byte(pin, DS18B20_CMD_READ_SCRATCH);

    uint8_t lo = ow_read_byte(pin);
    uint8_t hi = ow_read_byte(pin);
    // Skip the rest of the scratchpad
    for (int i = 2; i < 9; i++) ow_read_byte(pin);

    int16_t raw = (int16_t)((hi << 8) | lo);
    return raw / 16.0f;     /* 12-bit: LSB = 0.0625 °C */
}
