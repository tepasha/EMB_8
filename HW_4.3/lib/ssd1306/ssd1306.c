#include "ssd1306.h"
#include "i2c_bus.h"
#include "font5x7.h"
#include "esp_log.h"
#include <string.h>
#include <math.h>

static const char *TAG = "SSD1306";

static esp_err_t ssd1306_cmd(ssd1306_t *disp, uint8_t cmd)
{
    uint8_t buf[2] = {0x00, cmd}; // Co=0, D/C#=0 -> command
    return i2c_master_transmit(disp->dev, buf, sizeof(buf), 100);
}

static esp_err_t ssd1306_cmd_list(ssd1306_t *disp, const uint8_t *cmds, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        esp_err_t err = ssd1306_cmd(disp, cmds[i]);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}

esp_err_t ssd1306_init(ssd1306_t *disp, i2c_master_bus_handle_t bus_handle)
{
    esp_err_t err = i2c_bus_add_device(bus_handle, SSD1306_ADDR, &disp->dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add SSD1306 device: %s", esp_err_to_name(err));
        return err;
    }

    const uint8_t init_cmds[] = {
        0xAE,             // display off
        0xD5, 0x80,       // clock divide
        0xA8, 0x3F,       // multiplex ratio (64-1)
        0xD3, 0x00,       // display offset
        0x40,             // start line = 0
        0x8D, 0x14,       // charge pump enable
        0x20, 0x00,       // memory mode: horizontal addressing
        0xA1,             // segment remap
        0xC8,             // COM scan direction remapped
        0xDA, 0x12,       // COM pins config
        0x81, 0xCF,       // contrast
        0xD9, 0xF1,       // pre-charge
        0xDB, 0x40,       // VCOMH deselect level
        0xA4,             // resume RAM content display
        0xA6,             // normal (not inverted)
        0xAF              // display on
    };

    err = ssd1306_cmd_list(disp, init_cmds, sizeof(init_cmds));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SSD1306 init sequence failed: %s", esp_err_to_name(err));
        return err;
    }

    ssd1306_clear(disp);
    return ssd1306_flush(disp);
}

void ssd1306_clear(ssd1306_t *disp)
{
    memset(disp->buffer, 0, sizeof(disp->buffer));
}

esp_err_t ssd1306_flush(ssd1306_t *disp)
{
    const uint8_t addr_cmds[] = {
        0x21, 0x00, SSD1306_WIDTH - 1,   // column addr
        0x22, 0x00, SSD1306_PAGES - 1    // page addr
    };
    esp_err_t err = ssd1306_cmd_list(disp, addr_cmds, sizeof(addr_cmds));
    if (err != ESP_OK) return err;

    // Передаємо буфер частинами (перший байт = 0x40 data mode)
    uint8_t chunk[33];
    chunk[0] = 0x40;
    size_t total = sizeof(disp->buffer);
    size_t offset = 0;
    while (offset < total) {
        size_t n = total - offset > 32 ? 32 : total - offset;
        memcpy(&chunk[1], &disp->buffer[offset], n);
        err = i2c_master_transmit(disp->dev, chunk, n + 1, 100);
        if (err != ESP_OK) return err;
        offset += n;
    }
    return ESP_OK;
}

void ssd1306_set_pixel(ssd1306_t *disp, int x, int y, bool on)
{
    if (x < 0 || x >= SSD1306_WIDTH || y < 0 || y >= SSD1306_HEIGHT) return;
    int page = y / 8;
    int bit = y % 8;
    int idx = page * SSD1306_WIDTH + x;
    if (on) disp->buffer[idx] |= (1 << bit);
    else    disp->buffer[idx] &= ~(1 << bit);
}

void ssd1306_draw_hline(ssd1306_t *disp, int x, int y, int w, bool on)
{
    for (int i = 0; i < w; i++) ssd1306_set_pixel(disp, x + i, y, on);
}

void ssd1306_draw_rect(ssd1306_t *disp, int x, int y, int w, int h, bool on)
{
    for (int i = 0; i < w; i++) {
        ssd1306_set_pixel(disp, x + i, y, on);
        ssd1306_set_pixel(disp, x + i, y + h - 1, on);
    }
    for (int i = 0; i < h; i++) {
        ssd1306_set_pixel(disp, x, y + i, on);
        ssd1306_set_pixel(disp, x + w - 1, y + i, on);
    }
}

void ssd1306_fill_rect(ssd1306_t *disp, int x, int y, int w, int h, bool on)
{
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            ssd1306_set_pixel(disp, x + i, y + j, on);
}

void ssd1306_draw_circle(ssd1306_t *disp, int cx, int cy, int r, bool on)
{
    for (int angle = 0; angle < 360; angle += 4) {
        float rad = angle * M_PI / 180.0f;
        int x = cx + (int)(r * cosf(rad));
        int y = cy + (int)(r * sinf(rad));
        ssd1306_set_pixel(disp, x, y, on);
    }
}

void ssd1306_draw_char(ssd1306_t *disp, int x, int y, char c, int scale)
{
    if (c < 32 || c > 126) c = '?';
    const uint8_t *glyph = font5x7[c - 32];

    for (int col = 0; col < 5; col++) {
        uint8_t line = glyph[col];
        for (int row = 0; row < 7; row++) {
            bool on = line & (1 << row);
            if (scale == 1) {
                ssd1306_set_pixel(disp, x + col, y + row, on);
            } else {
                ssd1306_fill_rect(disp, x + col * scale, y + row * scale, scale, scale, on);
            }
        }
    }
}

void ssd1306_draw_string(ssd1306_t *disp, int x, int y, const char *str, int scale)
{
    int cursor = x;
    while (*str) {
        ssd1306_draw_char(disp, cursor, y, *str, scale);
        cursor += (5 * scale + 1 * scale); // символ + інтервал
        str++;
    }
}
