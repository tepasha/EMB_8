#pragma once
#include "driver/i2c_master.h"
#include "esp_err.h"
#include <stdint.h>

#define DS1307_ADDR 0x68

typedef struct {
    uint8_t sec;
    uint8_t min;
    uint8_t hour;
    uint8_t day_of_week; // 1=Sunday .. 7=Saturday
    uint8_t date;
    uint8_t month;
    uint16_t year; // повний рік, наприклад 2026
} ds1307_time_t;

esp_err_t ds1307_init(i2c_master_bus_handle_t bus_handle, i2c_master_dev_handle_t *dev);
esp_err_t ds1307_get_time(i2c_master_dev_handle_t dev, ds1307_time_t *t);
esp_err_t ds1307_set_time(i2c_master_dev_handle_t dev, const ds1307_time_t *t);
