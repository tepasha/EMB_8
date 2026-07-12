#pragma once
#include "esp_err.h"
#include <stdint.h>

#define DS18B20_GPIO 4

esp_err_t ds18b20_init(void);
esp_err_t ds18b20_read_temp(float *temp_c);
