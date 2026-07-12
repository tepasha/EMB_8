#pragma once
#include "driver/i2c_master.h"
#include "esp_err.h"

#define I2C_SDA_PIN     8
#define I2C_SCL_PIN     9
#define I2C_FREQ_HZ     400000

esp_err_t i2c_bus_init(i2c_master_bus_handle_t *bus_handle);
esp_err_t i2c_bus_add_device(i2c_master_bus_handle_t bus_handle,
                              uint8_t addr,
                              i2c_master_dev_handle_t *dev_handle);
