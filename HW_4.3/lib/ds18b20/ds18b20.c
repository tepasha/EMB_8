#include "ds18b20.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "DS18B20";

static void set_pin_output(void) {
    gpio_set_direction(DS18B20_GPIO, GPIO_MODE_OUTPUT_OD);
}
static void set_pin_input(void) {
    gpio_set_direction(DS18B20_GPIO, GPIO_MODE_INPUT);
}

static bool ow_reset(void)
{
    set_pin_output();
    gpio_set_level(DS18B20_GPIO, 0);
    esp_rom_delay_us(480);
    set_pin_input();
    esp_rom_delay_us(70);
    bool presence = !gpio_get_level(DS18B20_GPIO);
    esp_rom_delay_us(410);
    return presence;
}

static void ow_write_bit(bool bit)
{
    set_pin_output();
    gpio_set_level(DS18B20_GPIO, 0);
    esp_rom_delay_us(bit ? 6 : 60);
    gpio_set_level(DS18B20_GPIO, 1);
    esp_rom_delay_us(bit ? 64 : 10);
}

static bool ow_read_bit(void)
{
    set_pin_output();
    gpio_set_level(DS18B20_GPIO, 0);
    esp_rom_delay_us(3);
    set_pin_input();
    esp_rom_delay_us(10);
    bool bit = gpio_get_level(DS18B20_GPIO);
    esp_rom_delay_us(53);
    return bit;
}

static void ow_write_byte(uint8_t byte)
{
    for (int i = 0; i < 8; i++) {
        ow_write_bit(byte & 0x01);
        byte >>= 1;
    }
}

static uint8_t ow_read_byte(void)
{
    uint8_t byte = 0;
    for (int i = 0; i < 8; i++) {
        byte >>= 1;
        if (ow_read_bit()) byte |= 0x80;
    }
    return byte;
}

esp_err_t ds18b20_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << DS18B20_GPIO,
        .mode = GPIO_MODE_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io_conf);

    if (!ow_reset()) {
        ESP_LOGW(TAG, "DS18B20 not detected");
        return ESP_ERR_NOT_FOUND;
    }
    return ESP_OK;
}

esp_err_t ds18b20_read_temp(float *temp_c)
{
    if (!ow_reset()) return ESP_ERR_NOT_FOUND;
    ow_write_byte(0xCC); // Skip ROM
    ow_write_byte(0x44); // Convert T
    vTaskDelay(pdMS_TO_TICKS(750)); // час конверсії при 12-біт

    if (!ow_reset()) return ESP_ERR_NOT_FOUND;
    ow_write_byte(0xCC);
    ow_write_byte(0xBE); // Read Scratchpad

    uint8_t lsb = ow_read_byte();
    uint8_t msb = ow_read_byte();

    int16_t raw = (msb << 8) | lsb;
    *temp_c = raw / 16.0f;
    return ESP_OK;
}
