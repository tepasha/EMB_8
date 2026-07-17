#include "ssd1306.h"
#include "font_5x7.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "ssd1306";

static i2c_master_dev_handle_t s_dev; // хендл пристрою на новому I2C-драйвері

// Буфер кадру: [сторінка][стовпець], кожен байт = 8 вертикальних пікселів
static uint8_t s_framebuf[SSD1306_PAGES][SSD1306_WIDTH];

static esp_err_t ssd1306_write(uint8_t control_byte, const uint8_t *data, size_t len)
{
    uint8_t buf[SSD1306_WIDTH + 1]; // з запасом на найбільшу передачу (128 байт даних + control)
    if (len + 1 > sizeof(buf)) {
        return ESP_ERR_INVALID_SIZE;
    }
    buf[0] = control_byte;
    if (len > 0) {
        memcpy(buf + 1, data, len);
    }
    return i2c_master_transmit(s_dev, buf, len + 1, pdMS_TO_TICKS(1000));
}

static esp_err_t ssd1306_cmd(uint8_t cmd)
{
    return ssd1306_write(0x00, &cmd, 1); // 0x00 = наступний байт це команда
}

static esp_err_t ssd1306_data(const uint8_t *data, size_t len)
{
    return ssd1306_write(0x40, data, len); // 0x40 = наступні байти це дані
}

esp_err_t ssd1306_init(i2c_port_t port, uint8_t i2c_addr7bit,
                        gpio_num_t sda_gpio, gpio_num_t scl_gpio)
{
    i2c_master_bus_config_t bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = port,
        .sda_io_num = sda_gpio,
        .scl_io_num = scl_gpio,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus_handle;
    esp_err_t err = i2c_new_master_bus(&bus_config, &bus_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus: %s", esp_err_to_name(err));
        return err;
    }

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = i2c_addr7bit,
        .scl_speed_hz = 400000,
    };
    err = i2c_master_bus_add_device(bus_handle, &dev_config, &s_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_bus_add_device: %s", esp_err_to_name(err));
        return err;
    }

    vTaskDelay(pdMS_TO_TICKS(100)); // час на стабілізацію живлення дисплея

    static const uint8_t init_seq[] = {
        0xAE,       // display OFF
        0xD5, 0x80, // clock divide ratio / oscillator freq
        0xA8, 0x3F, // multiplex ratio = 63 (для 128x64)
        0xD3, 0x00, // display offset = 0
        0x40,       // start line = 0
        0x8D, 0x14, // charge pump enable
        0x20, 0x00, // memory addressing mode = horizontal (не використовується,
                     // ми пишемо посторінково, але коректне значення не завадить)
        0xA1,       // segment remap (дзеркало по X)
        0xC8,       // COM output scan direction (дзеркало по Y)
        0xDA, 0x12, // COM pins hardware config
        0x81, 0xCF, // contrast
        0xD9, 0xF1, // pre-charge period
        0xDB, 0x40, // VCOMH deselect level
        0xA4,       // resume to RAM content display
        0xA6,       // normal (не інвертований) режим
        0xAF,       // display ON
    };

    for (size_t i = 0; i < sizeof(init_seq); i++) {
        err = ssd1306_cmd(init_seq[i]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Помилка ініціалізації дисплея (байт %d): %s",
                     (int)i, esp_err_to_name(err));
            return err;
        }
    }

    ssd1306_clear();
    ssd1306_flush();

    ESP_LOGI(TAG, "SSD1306 ініціалізовано (I2C addr 0x%02X, новий i2c_master драйвер)", i2c_addr7bit);
    return ESP_OK;
}

void ssd1306_clear(void)
{
    memset(s_framebuf, 0, sizeof(s_framebuf));
}

void ssd1306_draw_string(uint8_t page, uint8_t col, const char *text)
{
    if (page >= SSD1306_PAGES) {
        return;
    }

    while (*text != '\0' && col < SSD1306_WIDTH) {
        const uint8_t *glyph = font5x7_get_glyph(*text);

        for (int i = 0; i < 5; i++) {
            if (col + i >= SSD1306_WIDTH) {
                break;
            }
            s_framebuf[page][col + i] = glyph ? glyph[i] : 0x00;
        }
        // стовпець-проміжок між символами (col + 5) лишається 0x00

        col += 6; // 5px гліф + 1px проміжок
        text++;
    }
}

esp_err_t ssd1306_flush(void)
{
    for (uint8_t page = 0; page < SSD1306_PAGES; page++) {
        esp_err_t err;
        err = ssd1306_cmd(0xB0 + page); // встановити сторінку
        if (err != ESP_OK) return err;
        err = ssd1306_cmd(0x00); // молодший ніббл стовпця = 0
        if (err != ESP_OK) return err;
        err = ssd1306_cmd(0x10); // старший ніббл стовпця = 0
        if (err != ESP_OK) return err;

        err = ssd1306_data(s_framebuf[page], SSD1306_WIDTH);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}
