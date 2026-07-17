#pragma once

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include <stdint.h>

// Дескриптор пристрою BME280 (SPI режим)
typedef struct {
    spi_device_handle_t spi;

    // Калібрувальні коефіцієнти (читаються один раз при ініціалізації)
    uint16_t dig_T1;
    int16_t  dig_T2, dig_T3;

    uint16_t dig_P1;
    int16_t  dig_P2, dig_P3, dig_P4, dig_P5, dig_P6, dig_P7, dig_P8, dig_P9;

    uint8_t  dig_H1;
    int16_t  dig_H2;
    uint8_t  dig_H3;
    int16_t  dig_H4, dig_H5;
    int8_t   dig_H6;

    double t_fine; // проміжне значення, потрібне формулою компенсації тиску/вологості
} bme280_dev_t;

/**
 * Ініціалізація SPI-шини та самого датчика BME280.
 * Налаштовує сенсор у NORMAL mode (безперервні вимірювання у фоні),
 * тому подальші виклики bme280_read() не потребують очікування виміру.
 */
esp_err_t bme280_spi_init(bme280_dev_t *dev,
                           gpio_num_t mosi,
                           gpio_num_t miso,
                           gpio_num_t sclk,
                           gpio_num_t cs);

/**
 * Зчитує останній результат вимірювання (без запуску нового виміру —
 * сенсор у NORMAL mode оновлює регістри даних самостійно).
 */
esp_err_t bme280_read(bme280_dev_t *dev,
                       float *temperature_c,
                       float *pressure_pa,
                       float *humidity_rh);
