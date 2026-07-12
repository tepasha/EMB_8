#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"

// lib/ components — resolved via EXTRA_COMPONENT_DIRS → lib/
#include "rtc_ds1307.h"
#include "ds18b20.h"
#include "display.h"

// Pin definitions
#define PIN_SDA       8
#define PIN_SCL       9
#define PIN_ONE_WIRE  3

// I2C bus + DS1307 device handles 

// Forward declarations
static uint8_t calc_dow(int y, int m, int d);
static void init_i2c(void);
static void init_rtc(void);
static void task_temperature(void *arg);
static void task_clock(void *arg);
void app_main(void);

static i2c_master_bus_handle_t  s_i2c_bus;
static i2c_master_dev_handle_t  s_rtc_dev;

//Shared temperature (float, mutex-protected)
static float             s_temp_c    = -127.0f;
static SemaphoreHandle_t s_temp_mutex;

static const char *TAG = "clock";

//  Helper: day-of-week via Zeller's congruence                        
//  Returns 1=Sun … 7=Sat  (DS1307 register convention)
static uint8_t calc_dow(int y, int m, int d)
{
    if (m < 3) { m += 12; y--; }
    int k = y % 100, j = y / 100;
    int h = (d + (13 * (m + 1)) / 5 + k + k / 4 + j / 4 + 5 * j) % 7;
    /* h: 0=Sat, 1=Sun, 2=Mon … 6=Fri  →  convert to 1=Sun … 7=Sat */
    return (uint8_t)((h + 6) % 7 + 1);
}

//  I2C bus init (ESP-IDF v5 new master driver)
static void init_i2c(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port            = I2C_NUM_0,
        .sda_io_num          = PIN_SDA,
        .scl_io_num          = PIN_SCL,
        .clk_source          = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt   = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &s_i2c_bus));

    i2c_device_config_t ds1307_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = DS1307_ADDR,
        .scl_speed_hz    = 100000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(s_i2c_bus, &ds1307_cfg, &s_rtc_dev));

    ESP_LOGI(TAG, "I2C ready  SDA=GPIO%d  SCL=GPIO%d", PIN_SDA, PIN_SCL);
}

//  RTC init — set compile-time if CH bit is set (lost power / new)
static void init_rtc(void)
{
    if (ds1307_lost_power(s_rtc_dev)) {
        ESP_LOGW(TAG, "DS1307 CH bit set — programming compile-time");

        /* Parse __DATE__ = "Mmm DD YYYY"  __TIME__ = "HH:MM:SS" */
        static const char *months = "JanFebMarAprMayJunJulAugSepOctNovDec";
        char mon_str[4];
        int  day_n, year_n, hr, mn, sc;
        sscanf(__DATE__, "%3s %d %d", mon_str, &day_n, &year_n);
        sscanf(__TIME__, "%d:%d:%d",  &hr, &mn, &sc);

        int month_n = 1;
        for (int i = 0; i < 12; i++) {
            if (strncmp(months + i * 3, mon_str, 3) == 0) {
                month_n = i + 1;
                break;
            }
        }

        ds1307_time_t t = {
            .second = (uint8_t)sc,
            .minute = (uint8_t)mn,
            .hour   = (uint8_t)hr,
            .dow    = calc_dow(year_n, month_n, day_n),
            .day    = (uint8_t)day_n,
            .month  = (uint8_t)month_n,
            .year   = (uint16_t)year_n,
        };
        ESP_ERROR_CHECK(ds1307_write(s_rtc_dev, &t));
    }
    ESP_LOGI(TAG, "DS1307 ready");
}

//  Task — temperature (async DS18B20, every 2 s)
static void task_temperature(void *arg)
{
    const gpio_num_t ow_pin = (gpio_num_t)PIN_ONE_WIRE;

    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << ow_pin,
        .mode         = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    while (true) {
        // Step 1 — start 12-bit conversion (~750 ms)
        bool present = ds18b20_start_conversion(ow_pin);
        if (!present) {
            ESP_LOGW(TAG, "DS18B20 not found on GPIO%d", ow_pin);
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        // Step 2 — wait for conversion to complete (≥750 ms)
        vTaskDelay(pdMS_TO_TICKS(800));

        // Step 3 — read scratchpad
        float temp = ds18b20_read_temp(ow_pin);
        if (xSemaphoreTake(s_temp_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            s_temp_c = temp;
            xSemaphoreGive(s_temp_mutex);
        }
        ESP_LOGD(TAG, "Temp: %.2f C", temp);

        // Step 4 — rest of 2 s interval
        vTaskDelay(pdMS_TO_TICKS(1200));
    }
}

//  Task — clock display (exact 1 s cadence via vTaskDelayUntil)
static void task_clock(void *arg)
{
    bool colon_on = true;

    // Let the splash screen show briefly
    vTaskDelay(pdMS_TO_TICKS(900));

    TickType_t last_wake = xTaskGetTickCount();

    while (true) {
        // Read RTC
        ds1307_time_t t;
        esp_err_t err = ds1307_read(s_rtc_dev, &t);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "RTC read error: %s", esp_err_to_name(err));
            display_show_error("RTC error!");
            vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(1000));
            continue;
        }

        // Snapshot temperature under mutex
        float temp_snap = -127.0f;
        if (xSemaphoreTake(s_temp_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            temp_snap = s_temp_c;
            xSemaphoreGive(s_temp_mutex);
        }

        // Render Casio-style frame
        display_update(&t, temp_snap, colon_on);
        colon_on = !colon_on;

        // Maintain exact 1 s period
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(1000));
    }
}

//  app_main
void app_main(void)
{
    ESP_LOGI(TAG, "=== Casio Clock / ESP32-S3 / ESP-IDF v5 ===");

    // Mutex for shared temperature float
    s_temp_mutex = xSemaphoreCreateMutex();
    configASSERT(s_temp_mutex);

    // Hardware init — order matters
    init_i2c();
    display_init(s_i2c_bus);   // shows splash; bus_handle kept for symmetry
    init_rtc();

    // FreeRTOS tasks
    xTaskCreate(task_temperature, "temp",  4096, NULL, 5, NULL);
    xTaskCreate(task_clock,       "clock", 4096, NULL, 5, NULL);
}
