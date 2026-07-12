#include <stdio.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "i2c_bus.h"
#include "ssd1306.h"
#include "ds1307.h"
#include "ds18b20.h"

static const char *TAG = "CLOCK_APP";

#define ENABLE_DS18B20 1

static i2c_master_bus_handle_t i2c_bus;
static i2c_master_dev_handle_t ds1307_dev;
static ssd1306_t display;

static const char *weekday_names[] = {
    "", "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};

// ---------- Ініціалізація периферії ----------
static esp_err_t app_hw_init(void)
{
    esp_err_t err = i2c_bus_init(&i2c_bus);
    if (err != ESP_OK) return err;

    err = ssd1306_init(&display, i2c_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SSD1306 init failed");
        return err;
    }

    err = ds1307_init(i2c_bus, &ds1307_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "DS1307 init failed");
        return err;
    }

#if ENABLE_DS18B20
    if (ds18b20_init() != ESP_OK) {
        ESP_LOGW(TAG, "DS18B20 not found, temperature disabled");
    }
#endif

    return ESP_OK;
}

// ---------- Зчитування часу ----------
static bool app_read_time(ds1307_time_t *t)
{
    esp_err_t err = ds1307_get_time(ds1307_dev, t);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read DS1307: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

// ---------- Анімація секунд: спінер у правому верхньому куті ----------
static void draw_seconds_spinner(ssd1306_t *disp, uint8_t sec)
{
    const int cx = 120, cy = 6, r = 5;
    int angle = (sec % 60) * 6; // 360/60
    float rad = angle * 3.14159f / 180.0f;
    int x2 = cx + (int)(r * cosf(rad - 1.5708f));
    int y2 = cy + (int)(r * sinf(rad - 1.5708f));
    ssd1306_draw_circle(disp, cx, cy, r, true);
    ssd1306_set_pixel(disp, x2, y2, true);
    ssd1306_set_pixel(disp, cx, cy, true);
}

// ---------- Прогрес-бар хвилин знизу екрана ----------
static void draw_minute_progress(ssd1306_t *disp, uint8_t sec)
{
    int width = (sec * 128) / 60;
    ssd1306_draw_hline(disp, 0, 63, 128, false);
    ssd1306_fill_rect(disp, 0, 62, width, 2, true);
}

// ---------- Оновлення дисплея ----------
static void app_update_display(const ds1307_time_t *t, float temp_c, bool temp_valid)
{
    char date_str[24];
    snprintf(date_str, sizeof(date_str), "%s %02d.%02d.%04d",
             weekday_names[t->day_of_week], t->date, t->month, t->year);

    ssd1306_clear(&display);

    // Години:Хвилини - жирним (масштаб 3), секунди - звичайним поруч (масштаб 1)
    char hm_str[8];
    char s_str[4] __attribute__((unused));
    char sec_str[4];
    snprintf(hm_str, sizeof(hm_str), "%02d:%02d", t->hour, t->min);
    snprintf(sec_str, sizeof(sec_str), "%02d", t->sec);

    ssd1306_draw_string(&display, 2, 10, hm_str, 3);
    ssd1306_draw_string(&display, 98, 10, s_str, 1); // маленькі секунди текстом

    // Дата звичайним шрифтом
    ssd1306_draw_string(&display, 2, 40, date_str, 1);

    // Температура (опційно)
    if (temp_valid) {
        char temp_str[16];
        snprintf(temp_str, sizeof(temp_str), "%.1f C", temp_c);
        ssd1306_draw_string(&display, 2, 52, temp_str, 1);
    }

    // Анімації
    draw_seconds_spinner(&display, t->sec);
    draw_minute_progress(&display, t->sec);

    ssd1306_flush(&display);
}

// Основний цикл
void app_main(void)
{
    ESP_LOGI(TAG, "Starting clock application");

    if (app_hw_init() != ESP_OK) {
        ESP_LOGE(TAG, "Hardware init failed, halting");
        return;
    }

    ds1307_time_t current_time;
    float temp_c = 0.0f;
    bool temp_valid = false;
    int temp_counter = 0;

    while (1) {
        if (app_read_time(&current_time)) {
#if ENABLE_DS18B20
            // Читаємо температуру рідше (раз на ~10 циклів), бо конверсія довга
            if (temp_counter == 0) {
                temp_valid = (ds18b20_read_temp(&temp_c) == ESP_OK);
            }
            temp_counter = (temp_counter + 1) % 10;
#endif
            app_update_display(&current_time, temp_c, temp_valid);
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
