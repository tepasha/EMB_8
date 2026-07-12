#include "ds1307.h"
#include "i2c_bus.h"
#include "esp_log.h"

static const char *TAG = "DS1307";

static uint8_t bcd2dec(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
static uint8_t dec2bcd(uint8_t v) { return ((v / 10) << 4) | (v % 10); }

esp_err_t ds1307_init(i2c_master_bus_handle_t bus_handle, i2c_master_dev_handle_t *dev)
{
    esp_err_t err = i2c_bus_add_device(bus_handle, DS1307_ADDR, dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add DS1307 device: %s", esp_err_to_name(err));
        return err;
    }

    // Переконатись, що генератор увімкнено (біт CH = 0 у регістрі секунд)
    uint8_t reg = 0x00;
    uint8_t sec_val;
    esp_err_t r = i2c_master_transmit_receive(*dev, &reg, 1, &sec_val, 1, 100);
    if (r == ESP_OK && (sec_val & 0x80)) {
        sec_val &= 0x7F; // очистити CH біт
        uint8_t buf[2] = {0x00, sec_val};
        i2c_master_transmit(*dev, buf, 2, 100);
        ESP_LOGI(TAG, "Oscillator was halted, restarted.");
    }
    return ESP_OK;
}

esp_err_t ds1307_get_time(i2c_master_dev_handle_t dev, ds1307_time_t *t)
{
    uint8_t reg = 0x00;
    uint8_t raw[7];

    esp_err_t err = i2c_master_transmit_receive(dev, &reg, 1, raw, 7, 100);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read time: %s", esp_err_to_name(err));
        return err;
    }

    t->sec         = bcd2dec(raw[0] & 0x7F);
    t->min         = bcd2dec(raw[1]);
    t->hour        = bcd2dec(raw[2] & 0x3F); // 24h mode
    t->day_of_week = bcd2dec(raw[3]);
    t->date        = bcd2dec(raw[4]);
    t->month       = bcd2dec(raw[5]);
    t->year        = 2000 + bcd2dec(raw[6]);

    return ESP_OK;
}

esp_err_t ds1307_set_time(i2c_master_dev_handle_t dev, const ds1307_time_t *t)
{
    uint8_t buf[8];
    buf[0] = 0x00; // start register
    buf[1] = dec2bcd(t->sec);
    buf[2] = dec2bcd(t->min);
    buf[3] = dec2bcd(t->hour);
    buf[4] = dec2bcd(t->day_of_week);
    buf[5] = dec2bcd(t->date);
    buf[6] = dec2bcd(t->month);
    buf[7] = dec2bcd(t->year % 100);

    return i2c_master_transmit(dev, buf, sizeof(buf), 100);
}
