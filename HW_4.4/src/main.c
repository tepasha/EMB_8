#include <stdio.h>
#include <string.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"

#include "ssd1306.h"
#include "bme280_spi.h"
#include "ds18b20.h"

static const char *TAG = "DZ_SENSORS";

// НАЛАШТУВАННЯ ПІНІВ 
#define PIN_I2C_SDA   GPIO_NUM_8
#define PIN_I2C_SCL   GPIO_NUM_9
#define OLED_I2C_ADDR 0x3C
#define I2C_PORT      I2C_NUM_0

// BME280 підключений через SPI
#define PIN_SPI_MOSI  GPIO_NUM_11
#define PIN_SPI_MISO  GPIO_NUM_13
#define PIN_SPI_SCLK  GPIO_NUM_12
#define PIN_SPI_CS    GPIO_NUM_10

// DS18B20 (1-Wire)
#define PIN_DS18B20   GPIO_NUM_4

// Мінімальний період оновлення даних (1 раз на секунду)
#define UPDATE_PERIOD_US (1000 * 1000)

static bme280_dev_t bme280;

void app_main(void)
{
    ESP_LOGI(TAG, "=== ДЗ: BME280 (SPI) + DS18B20 (1-Wire) + SSD1306 (I2C, власний драйвер) ===");

    // Ініціалізація периферії
    esp_err_t oled_err = ssd1306_init(I2C_PORT, OLED_I2C_ADDR, PIN_I2C_SDA, PIN_I2C_SCL);
    if (oled_err != ESP_OK) {
        ESP_LOGE(TAG, "Не вдалося ініціалізувати дисплей: %s", esp_err_to_name(oled_err));
    }

    esp_err_t bme_err = bme280_spi_init(&bme280, PIN_SPI_MOSI, PIN_SPI_MISO,
                                         PIN_SPI_SCLK, PIN_SPI_CS);
    if (bme_err != ESP_OK) {
        ESP_LOGE(TAG, "Не вдалося ініціалізувати BME280: %s", esp_err_to_name(bme_err));
    }

    ds18b20_init(PIN_DS18B20);
    ds18b20_start_conversion(PIN_DS18B20); // перший запуск конвертації "у фоні"

    int64_t last_update_us = esp_timer_get_time();

    while (1) {
        int64_t now_us = esp_timer_get_time();

        if (now_us - last_update_us >= UPDATE_PERIOD_US) {
            last_update_us = now_us;

            // BME280: сенсор працює в NORMAL mode
            float temp_c = 0.0f, hum_rh = 0.0f, press_pa = 0.0f;
            esp_err_t err = bme280_read(&bme280, &temp_c, &press_pa, &hum_rh);
            float press_hpa = press_pa / 100.0f;

            // DS18B20: читаємо результат конвертації
            float temp_ds18b20 = NAN;
            bool ds_ok = ds18b20_read_temperature(PIN_DS18B20, &temp_ds18b20);
            ds18b20_start_conversion(PIN_DS18B20);

            // Вивід на екран
            ssd1306_clear();

            char line_bme[32];
            char line_p[32];
            char line_ds[32];

            snprintf(line_bme, sizeof(line_bme), "T:%.1f%cC RH:%.0f%%", temp_c, 0xB0, hum_rh);
            snprintf(line_p, sizeof(line_p), "P: %.0f hPa", press_hpa);

            if (ds_ok) {
                snprintf(line_ds, sizeof(line_ds), "DS18B20: %.2f%cC", temp_ds18b20, 0xB0);
            } else {
                snprintf(line_ds, sizeof(line_ds), "DS18B20: --- ");
            }

            ssd1306_draw_string(3, 0, line_bme); // сторінка 3 (~рядок 24-31px)
            ssd1306_draw_string(4, 0, line_p);   // сторінка 4
            ssd1306_draw_string(5, 0, line_ds);  // сторінка 5

            ssd1306_flush();

            // Дублювання в лог
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "BME280: T=%.1f C, RH=%.0f %%, P=%.0f hPa",
                         temp_c, hum_rh, press_hpa);
            } else {
                ESP_LOGW(TAG, "BME280: помилка читання (%s)", esp_err_to_name(err));
            }

            if (ds_ok) {
                ESP_LOGI(TAG, "DS18B20: T=%.2f C", temp_ds18b20);
            } else {
                ESP_LOGW(TAG, "DS18B20: датчик не відповідає / помилка CRC");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
