#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "driver/i2c_master.h"
#include "esp_err.h"

#define DS1307_ADDR   0x68   /* 7-bit I2C address */

typedef struct {
    uint8_t second;   /* 0-59  */
    uint8_t minute;   /* 0-59  */
    uint8_t hour;     /* 0-23  */
    uint8_t dow;      /* 1-7, 1=Sunday */
    uint8_t day;      /* 1-31  */
    uint8_t month;    /* 1-12  */
    uint16_t year;    /* e.g. 2026 */
} ds1307_time_t;

static inline uint8_t bcd2dec(uint8_t b) { return (b >> 4) * 10 + (b & 0x0F); }
static inline uint8_t dec2bcd(uint8_t d) { return ((d / 10) << 4) | (d % 10); }

// Read current time from DS1307.
static inline esp_err_t ds1307_read(i2c_master_dev_handle_t dev, ds1307_time_t *t) {
    uint8_t reg = 0x00;
    uint8_t buf[7];
    esp_err_t err;

    err = i2c_master_transmit(dev, &reg, 1, 100);
    if (err != ESP_OK) return err;

    err = i2c_master_receive(dev, buf, 7, 100);
    if (err != ESP_OK) return err;

    t->second = bcd2dec(buf[0] & 0x7F);   /* mask CH bit */
    t->minute = bcd2dec(buf[1] & 0x7F);
    t->hour   = bcd2dec(buf[2] & 0x3F);   /* 24-h mode   */
    t->dow    = bcd2dec(buf[3] & 0x07);   /* 1=Sun       */
    t->day    = bcd2dec(buf[4] & 0x3F);
    t->month  = bcd2dec(buf[5] & 0x1F);
    t->year   = 2000 + bcd2dec(buf[6]);
    return ESP_OK;
}

// Set the DS1307 time. Also clears the CH (clock-halt) bit.
static inline esp_err_t ds1307_write(i2c_master_dev_handle_t dev, const ds1307_time_t *t) {
    uint8_t buf[8];
    buf[0] = 0x00;                          /* register pointer */
    buf[1] = dec2bcd(t->second) & 0x7F;    /* CH=0 → run       */
    buf[2] = dec2bcd(t->minute);
    buf[3] = dec2bcd(t->hour)   & 0x3F;    /* 24-h mode        */
    buf[4] = dec2bcd(t->dow);
    buf[5] = dec2bcd(t->day);
    buf[6] = dec2bcd(t->month);
    buf[7] = dec2bcd(t->year - 2000);
    return i2c_master_transmit(dev, buf, 8, 100);
}

// Check whether the DS1307 clock-halt bit is set (= has lost power).
static inline bool ds1307_lost_power(i2c_master_dev_handle_t dev) {
    uint8_t reg = 0x00, byte0;
    if (i2c_master_transmit(dev, &reg, 1, 100) != ESP_OK) return true;
    if (i2c_master_receive(dev, &byte0, 1, 100) != ESP_OK) return true;
    return (byte0 & 0x80) != 0;   /* CH bit */
}
