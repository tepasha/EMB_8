#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"
#include <stdint.h>

#define SSD1306_WIDTH   128
#define SSD1306_HEIGHT  64
#define SSD1306_PAGES   (SSD1306_HEIGHT / 8) // 8 сторінок по 8 пікселів

/**
 * Ініціалізує I2C-шину та сам дисплей SSD1306.
 */
esp_err_t ssd1306_init(i2c_port_t port, uint8_t i2c_addr7bit,
                        gpio_num_t sda_gpio, gpio_num_t scl_gpio);

/** Очищає внутрішній буфер кадру (в оперативній пам'яті, ще не на екрані). */
void ssd1306_clear(void);

/**
 * Малює рядок тексту в буфер кадру шрифтом 5x7 (символ = 6px завширшки:
 * 5px гліф + 1px проміжок).
 * page: 0..7 (кожна сторінка = 8 пікселів по висоті)
 * col:  0..127 (стовпець пікселя, з якого починається текст)
 */
void ssd1306_draw_string(uint8_t page, uint8_t col, const char *text);

/** Надсилає вміст буфера кадру на дисплей по I2C. */
esp_err_t ssd1306_flush(void);
