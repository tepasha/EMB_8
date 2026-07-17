#include "ds18b20.h"
#include "esp_rom_sys.h"   // esp_rom_delay_us
#include "esp_log.h"

static const char *TAG = "ds18b20";

static inline void ow_low(gpio_num_t pin)     { gpio_set_level(pin, 0); }
static inline void ow_release(gpio_num_t pin) { gpio_set_level(pin, 1); }

void ds18b20_init(gpio_num_t pin)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_INPUT_OUTPUT_OD, // відкритий стік + можливість читати рівень
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    ow_release(pin);
    ESP_LOGI(TAG, "1-Wire ініціалізовано на GPIO%d", pin);
}

// Часові інтервали 1-Wire (мкс), стандартні значення з datasheet DS18B20.
static bool ow_reset(gpio_num_t pin)
{
    ow_low(pin);
    esp_rom_delay_us(480);
    ow_release(pin);
    esp_rom_delay_us(70);
    bool presence = (gpio_get_level(pin) == 0); // пристрій відповідає низьким рівнем
    esp_rom_delay_us(410);
    return presence;
}

static void ow_write_bit(gpio_num_t pin, int bit)
{
    ow_low(pin);
    if (bit) {
        esp_rom_delay_us(6);
        ow_release(pin);
        esp_rom_delay_us(64);
    } else {
        esp_rom_delay_us(60);
        ow_release(pin);
        esp_rom_delay_us(10);
    }
}

static int ow_read_bit(gpio_num_t pin)
{
    ow_low(pin);
    esp_rom_delay_us(3);
    ow_release(pin);
    esp_rom_delay_us(9);
    int bit = gpio_get_level(pin);
    esp_rom_delay_us(53);
    return bit;
}

static void ow_write_byte(gpio_num_t pin, uint8_t byte)
{
    for (int i = 0; i < 8; i++) {
        ow_write_bit(pin, byte & 0x01);
        byte >>= 1;
    }
}

static uint8_t ow_read_byte(gpio_num_t pin)
{
    uint8_t byte = 0;
    for (int i = 0; i < 8; i++) {
        byte |= (ow_read_bit(pin) << i);
    }
    return byte;
}

static uint8_t crc8_dallas(const uint8_t *data, int len)
{
    uint8_t crc = 0;
    for (int i = 0; i < len; i++) {
        uint8_t inbyte = data[i];
        for (int j = 0; j < 8; j++) {
            uint8_t mix = (crc ^ inbyte) & 0x01;
            crc >>= 1;
            if (mix) crc ^= 0x8C;
            inbyte >>= 1;
        }
    }
    return crc;
}

void ds18b20_start_conversion(gpio_num_t pin)
{
    if (!ow_reset(pin)) {
        ESP_LOGW(TAG, "DS18B20 не відповідає на reset");
        return;
    }
    ow_write_byte(pin, 0xCC); // Skip ROM (на шині лише один датчик)
    ow_write_byte(pin, 0x44); // Convert T
}

bool ds18b20_read_temperature(gpio_num_t pin, float *temperature_c)
{
    if (!ow_reset(pin)) {
        return false;
    }
    ow_write_byte(pin, 0xCC); // Skip ROM
    ow_write_byte(pin, 0xBE); // Read Scratchpad

    uint8_t data[9];
    for (int i = 0; i < 9; i++) {
        data[i] = ow_read_byte(pin);
    }

    if (crc8_dallas(data, 8) != data[8]) {
        ESP_LOGW(TAG, "DS18B20: помилка CRC");
        return false;
    }

    int16_t raw = (int16_t)((data[1] << 8) | data[0]);
    *temperature_c = raw / 16.0f; // 12-бітний формат за замовчуванням
    return true;
}
