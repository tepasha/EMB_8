#include <stdio.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"

static const char *TAG = "SERVO_LDR";

#define ADC_CHANNEL     ADC_CHANNEL_8   // Pin 9
#define ADC_UNIT        ADC_UNIT_1
#define ADC_ATTEN       ADC_ATTEN_DB_12
#define ADC_BITWIDTH    ADC_BITWIDTH_12

#define SERVO_PIN           14
#define SERVO_FREQ_HZ       50
#define SERVO_PERIOD_US     (1000000 / SERVO_FREQ_HZ)   // 20000 us
#define SERVO_RESOLUTION    LEDC_TIMER_12_BIT
#define SERVO_MAX_DUTY      ((1 << 12) - 1)              // 4095
#define SERVO_TIMER         LEDC_TIMER_0
#define SERVO_CHANNEL       LEDC_CHANNEL_0

#define SERVO_DEG_MIN   10
#define SERVO_DEG_MAX   170
#define SERVO_US_MIN    500
#define SERVO_US_MAX    2500
#define LIGHT_MV_MIN    50
#define LIGHT_MV_MAX    3000

#define LOOP_DELAY_MS   100

// Handles
static adc_oneshot_unit_handle_t g_adc_handle;
static adc_cali_handle_t         g_cali_handle;

// Utilities
static int clamp(int value, int lo, int hi) {
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

static int map_range(int value, int in_min, int in_max,
                                int out_min, int out_max) {
    if (in_min == in_max) return out_max;
    value = clamp(value, in_min, in_max);
    return out_min + (value - in_min) * (out_max - out_min)
                                      / (in_max - in_min);
}

// Ініціалізація
static void ldr_init(void) {
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = ADC_UNIT,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &g_adc_handle));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten    = ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(
        g_adc_handle, ADC_CHANNEL, &chan_cfg));

    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id  = ADC_UNIT,
        .chan     = ADC_CHANNEL,
        .atten   = ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH,
    };
    ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(
        &cali_cfg, &g_cali_handle));

    ESP_LOGI(TAG, "LDR init OK (CH%d)", ADC_CHANNEL);
}

static void servo_init(void) {
    ledc_timer_config_t timer_cfg = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .timer_num       = SERVO_TIMER,
        .duty_resolution = SERVO_RESOLUTION,
        .freq_hz         = SERVO_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    ledc_channel_config_t chan_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = SERVO_CHANNEL,
        .timer_sel  = SERVO_TIMER,
        .intr_type  = LEDC_INTR_DISABLE,
        .gpio_num   = SERVO_PIN,
        .duty       = 0,
        .hpoint     = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&chan_cfg));

    ESP_LOGI(TAG, "Servo init OK (GPIO%d @ %dHz)", SERVO_PIN, SERVO_FREQ_HZ);
}

// LDR read
static int ldr_read_mv(void) {
    int raw = 0;
    int mv  = 0;
    ESP_ERROR_CHECK(adc_oneshot_read(g_adc_handle, ADC_CHANNEL, &raw));
    ESP_ERROR_CHECK(adc_cali_raw_to_voltage(g_cali_handle, raw, &mv));
    return mv;
}

// Servo control
static uint32_t angle_to_duty(int deg) {
    int us = map_range(deg, SERVO_DEG_MIN, SERVO_DEG_MAX,
                            SERVO_US_MIN,  SERVO_US_MAX);
    return (uint32_t)us * SERVO_MAX_DUTY / SERVO_PERIOD_US;
}

static void servo_set_mv(int mv) {
    mv      = clamp(mv, LIGHT_MV_MIN, LIGHT_MV_MAX);
    int deg = map_range(mv,  LIGHT_MV_MIN,  LIGHT_MV_MAX,
                             SERVO_DEG_MIN, SERVO_DEG_MAX);
    uint32_t duty = angle_to_duty(deg);

    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, SERVO_CHANNEL, duty));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, SERVO_CHANNEL));

    ESP_LOGI(TAG, "LDR %4d mV -> %3d deg -> duty %lu", mv, deg, duty);
}

void app_main(void) {
    ldr_init();
    servo_init();

    while (1) {
        int mv = ldr_read_mv();
        servo_set_mv(mv);
        vTaskDelay(pdMS_TO_TICKS(LOOP_DELAY_MS));
    }
}
