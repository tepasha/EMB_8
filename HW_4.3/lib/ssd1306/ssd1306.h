#pragma once
#include "driver/i2c_master.h"
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#define SSD1306_ADDR    0x3C
#define SSD1306_WIDTH   128
#define SSD1306_HEIGHT  64
#define SSD1306_PAGES   (SSD1306_HEIGHT / 8)

typedef struct {
    i2c_master_dev_handle_t dev;
    uint8_t buffer[SSD1306_WIDTH * SSD1306_PAGES];
} ssd1306_t;

esp_err_t ssd1306_init(ssd1306_t *disp, i2c_master_bus_handle_t bus_handle);
void ssd1306_clear(ssd1306_t *disp);
esp_err_t ssd1306_flush(ssd1306_t *disp);
void ssd1306_set_pixel(ssd1306_t *disp, int x, int y, bool on);
void ssd1306_draw_char(ssd1306_t *disp, int x, int y, char c, int scale);
void ssd1306_draw_string(ssd1306_t *disp, int x, int y, const char *str, int scale);
void ssd1306_draw_hline(ssd1306_t *disp, int x, int y, int w, bool on);
void ssd1306_draw_rect(ssd1306_t *disp, int x, int y, int w, int h, bool on);
void ssd1306_fill_rect(ssd1306_t *disp, int x, int y, int w, int h, bool on);
void ssd1306_draw_circle(ssd1306_t *disp, int cx, int cy, int r, bool on);
