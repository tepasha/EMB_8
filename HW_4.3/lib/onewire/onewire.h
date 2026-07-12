#pragma once

#include "driver/gpio.h"
#include "rom/ets_sys.h"

// Low-level 1-Wire primitives
static inline void ow_drive_low(gpio_num_t pin) {
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    gpio_set_level(pin, 0);
}

static inline void ow_release(gpio_num_t pin) {
    gpio_set_direction(pin, GPIO_MODE_INPUT);
}

static inline int ow_read(gpio_num_t pin) {
    return gpio_get_level(pin);
}

//1-Wire reset — returns 1 if a device is present, 0 otherwise.
static inline int ow_reset(gpio_num_t pin) {
    ow_drive_low(pin);
    ets_delay_us(480);
    ow_release(pin);
    ets_delay_us(70);
    int present = !ow_read(pin);   /* device pulls low = present */
    ets_delay_us(410);
    return present;
}

// Write one bit on the 1-Wire bus.
static inline void ow_write_bit(gpio_num_t pin, int bit) {
    ow_drive_low(pin);
    if (bit) {
        ets_delay_us(6);
        ow_release(pin);
        ets_delay_us(64);
    } else {
        ets_delay_us(60);
        ow_release(pin);
        ets_delay_us(10);
    }
}

// Read one bit from the 1-Wire bus.
static inline int ow_read_bit(gpio_num_t pin) {
    ow_drive_low(pin);
    ets_delay_us(6);
    ow_release(pin);
    ets_delay_us(9);
    int bit = ow_read(pin);
    ets_delay_us(55);
    return bit;
}

// Write one byte on the 1-Wire bus (LSB first).
static inline void ow_write_byte(gpio_num_t pin, uint8_t byte) {
    for (int i = 0; i < 8; i++) {
        ow_write_bit(pin, byte & 0x01);
        byte >>= 1;
    }
}

// Read one byte from the 1-Wire bus (LSB first).
static inline uint8_t ow_read_byte(gpio_num_t pin) {
    uint8_t byte = 0;
    for (int i = 0; i < 8; i++) {
        byte |= (ow_read_bit(pin) << i);
    }
    return byte;
}
