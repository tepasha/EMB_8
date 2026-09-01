#include <stdio.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "driver/pulse_cnt.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "MOTOR_PID";

/* ---------- Піни (підставте свої) ---------- */
#define MOTOR_PWM_GPIO      GPIO_NUM_4   // база NPN-транзистора через резистор
#define ENCODER_A_GPIO      GPIO_NUM_5
#define ENCODER_B_GPIO      GPIO_NUM_6
#define BUTTON_GPIO         GPIO_NUM_0   // BOOT
/* ---------- LEDC (PWM) ---------- */
#define LEDC_TIMER          LEDC_TIMER_0
#define LEDC_MODE           LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL        LEDC_CHANNEL_0
#define LEDC_DUTY_RES       LEDC_TIMER_10_BIT   // 0..1023
#define LEDC_FREQ_HZ        20000
#define PWM_MAX             1023
#define PWM_MIN_MOVE        250   // мінімальний duty, при якому двигун реально рушає
/* ---------- Енкодер / механіка ---------- */
#define ENCODER_PPR         11      // імпульсів на оберт вала енкодера (EC11)
#define ENCODER_X4          4       // квадратурний режим x4
#define GEAR_RATIO          1.0f    // передавальне число редуктора двигун->вихідний вал
#define COUNTS_PER_REV      ((float)(ENCODER_PPR * ENCODER_X4) * GEAR_RATIO)
#define STEP_DEGREES        90.0f
#define STEP_COUNTS         ((int32_t)(COUNTS_PER_REV * (STEP_DEGREES / 360.0f)))
#define PCNT_HIGH_LIMIT     30000
#define PCNT_LOW_LIMIT      (-30000)
/* ---------- PID (стартові значення — підлягають підбору!) ---------- */
static float PID_KP = 3.0f;
static float PID_KI = 0.05f;
static float PID_KD = 0.15f;

#define PID_DT_MS           10
#define PID_OUT_MIN         0.0f          // без реверсу — тільки вперед
#define PID_OUT_MAX         (float)PWM_MAX
#define PID_I_LIMIT         300.0f        // anti-windup
#define POSITION_DEADBAND   2             // імпульси, в межах яких вважаємо, що ціль досягнута

typedef struct {
    float kp, ki, kd;
    float integral;
    float prev_error;
    float out_min, out_max;
} pid_t;

static pid_t g_pid;
static pcnt_unit_handle_t g_pcnt_unit;
static volatile int32_t g_target_counts = 0;
static esp_err_t err;

static void pid_init(pid_t *pid, float kp, float ki, float kd, float out_min, float out_max) {
    pid->kp = kp;
    pid->ki = ki;
    pid->kd = kd;
    pid->integral = 0.0f;
    pid->prev_error = 0.0f;
    pid->out_min = out_min;
    pid->out_max = out_max;
}

static float pid_update(pid_t *pid, float error, float dt_s) {
    pid->integral += error * dt_s;
    if (pid->integral > PID_I_LIMIT) pid->integral = PID_I_LIMIT;
    if (pid->integral < -PID_I_LIMIT) pid->integral = -PID_I_LIMIT;

    float derivative = (error - pid->prev_error) / dt_s;
    pid->prev_error = error;

    float out = pid->kp * error + pid->ki * pid->integral + pid->kd * derivative;

    if (out > pid->out_max) out = pid->out_max;
    if (out < pid->out_min) out = pid->out_min;

    return out;
}

/* ---------- PWM ---------- */
static void ledc_init_pwm() {
    ledc_timer_config_t timer_conf = {
        .speed_mode = LEDC_MODE,
        .duty_resolution = LEDC_DUTY_RES,
        .timer_num = LEDC_TIMER,
        .freq_hz = LEDC_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    err = ledc_timer_config(&timer_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_timer_config: %s", esp_err_to_name(err));
    }

    ledc_channel_config_t ch_conf = {
        .gpio_num = MOTOR_PWM_GPIO,
        .speed_mode = LEDC_MODE,
        .channel = LEDC_CHANNEL,
        .timer_sel = LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    err = ledc_channel_config(&ch_conf);
    if (err != ESP_OK){
        ESP_LOGE(TAG, "ledc_channel_config: %s", esp_err_to_name(err));
    }
}

static void set_motor_duty(int duty) {
    if (duty < 0) duty = 0;
    if (duty > PWM_MAX) duty = PWM_MAX;
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, duty);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
}

/* ---------- Енкодер (PCNT x4) ---------- */
static void pcnt_init_encoder(void) {
    pcnt_unit_config_t unit_config = {
        .high_limit = PCNT_HIGH_LIMIT,
        .low_limit = PCNT_LOW_LIMIT,
    };
    err = pcnt_new_unit(&unit_config, &g_pcnt_unit);
    if (err != ESP_OK){
        ESP_LOGE(TAG, "pcnt_new_unit: %s", esp_err_to_name(err));
    }

    pcnt_glitch_filter_config_t filter_config = {.max_glitch_ns = 1000};
    err = pcnt_unit_set_glitch_filter(g_pcnt_unit, &filter_config);
    if (err != ESP_OK){
        ESP_LOGE(TAG, "pcnt_unit_set_glitch_filter: %s", esp_err_to_name(err));
    }

    pcnt_chan_config_t chan_a_config = {
        .edge_gpio_num = ENCODER_A_GPIO,
        .level_gpio_num = ENCODER_B_GPIO,
    };
    pcnt_channel_handle_t chan_a;
    err = pcnt_new_channel(g_pcnt_unit, &chan_a_config, &chan_a);
    if (err != ESP_OK){
        ESP_LOGE(TAG, "pcnt_new_channel: %s", esp_err_to_name(err));
    }

    pcnt_chan_config_t chan_b_config = {
        .edge_gpio_num = ENCODER_B_GPIO,
        .level_gpio_num = ENCODER_A_GPIO,
    };
    pcnt_channel_handle_t chan_b;
    err = pcnt_new_channel(g_pcnt_unit, &chan_b_config, &chan_b);
    if (err != ESP_OK){
        ESP_LOGE(TAG, "pcnt_new_channel: %s", esp_err_to_name(err));
    }

    /* Квадратура x4 */
    err = pcnt_channel_set_edge_action(chan_a,
        PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE);
    if (err != ESP_OK){
        ESP_LOGE(TAG, "pcnt_channel_set_edge_action: %s", esp_err_to_name(err));
    }

    err = pcnt_channel_set_level_action(chan_a,
        PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
    if (err != ESP_OK){
        ESP_LOGE(TAG, "pcnt_channel_set_level_action: %s", esp_err_to_name(err));
    }

    err = pcnt_channel_set_edge_action(chan_b,
        PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE);
    if (err != ESP_OK){
        ESP_LOGE(TAG, "pcnt_channel_set_edge_action: %s", esp_err_to_name(err));
    }

    err = pcnt_channel_set_level_action(chan_b,
        PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
    if (err != ESP_OK){
        ESP_LOGE(TAG, "pcnt_channel_set_level_action: %s", esp_err_to_name(err));
    }

    err = pcnt_unit_add_watch_point(g_pcnt_unit, PCNT_HIGH_LIMIT);
    if (err != ESP_OK){
        ESP_LOGE(TAG, "pcnt_unit_add_watch_point: %s", esp_err_to_name(err));
    }

    err = pcnt_unit_add_watch_point(g_pcnt_unit, PCNT_LOW_LIMIT);
    if (err != ESP_OK){
        ESP_LOGE(TAG, "pcnt_unit_add_watch_point: %s", esp_err_to_name(err));
    }

    err = pcnt_unit_enable(g_pcnt_unit);
    if (err != ESP_OK){
        ESP_LOGE(TAG, "pcnt_unit_enable: %s", esp_err_to_name(err));
    }

    err = pcnt_unit_clear_count(g_pcnt_unit);
    if (err != ESP_OK){
        ESP_LOGE(TAG, "pcnt_unit_clear_count: %s", esp_err_to_name(err));
    }

    err = pcnt_unit_start(g_pcnt_unit);
    if (err != ESP_OK){
        ESP_LOGE(TAG, "pcnt_unit_start: %s", esp_err_to_name(err));
    }
}

static int32_t get_position_counts() {
    int count = 0;
    pcnt_unit_get_count(g_pcnt_unit, &count);
    return (int32_t) count;
}

/* ---------- Кнопка BOOT ---------- */
static void gpio_init_button() {
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
}

/* Проста програмна антидребезгова перевірка кнопки в задачі */
static void button_task(void *arg) {
    bool prev_state = true; // pull-up: 1 = не натиснута
    while (1) {
        bool cur_state = gpio_get_level(BUTTON_GPIO);
        if (prev_state == true && cur_state == false) {
            vTaskDelay(pdMS_TO_TICKS(30)); // антидребезг
            if (gpio_get_level(BUTTON_GPIO) == false) {
                g_target_counts += STEP_COUNTS;
                ESP_LOGI(TAG, "Button pressed -> new target = %ld counts (%.1f deg)",
                         (long)g_target_counts, (g_target_counts / COUNTS_PER_REV) * 360.0f);
            }
        }
        prev_state = cur_state;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/* ---------- Задача PID-регулятора ---------- */
static void pid_task(void *arg) {
    const float dt_s = PID_DT_MS / 1000.0f;
    int64_t last_log_us = 0;

    while (1) {
        int32_t position = get_position_counts();
        int32_t error_counts = g_target_counts - position;

        float output;
        if (abs((int) error_counts) <= POSITION_DEADBAND) {
            /* в межах "мертвої зони" — повністю зупиняємо двигун */
            output = 0.0f;
            g_pid.integral = 0.0f; // скидаємо інтегральну складову, щоб не "тягнула" в дрейф
        } else {
            output = pid_update(&g_pid, (float) error_counts, dt_s);
            /* якщо вихід занадто малий, щоб двигун реально рушив — або 0, або мінімальний робочий duty */
            if (output > 0.0f && output < PWM_MIN_MOVE) {
                output = PWM_MIN_MOVE;
            }
        }

        set_motor_duty((int) output);

        int64_t now_us = esp_timer_get_time();
        if (now_us - last_log_us > 100000) {
            // лог кожні ~100 мс
            ESP_LOGI(TAG, "target=%ld pos=%ld err=%ld duty=%.0f",
                     (long)g_target_counts, (long)position, (long)error_counts, output);
            last_log_us = now_us;
        }

        vTaskDelay(pdMS_TO_TICKS(PID_DT_MS));
    }
}

void app_main() {
    ledc_init_pwm();
    pcnt_init_encoder();
    gpio_init_button();
    pid_init(&g_pid, PID_KP, PID_KI, PID_KD, PID_OUT_MIN, PID_OUT_MAX);

    ESP_LOGI(TAG, "Counts per revolution: %.1f, step (90 deg) = %ld counts",
             COUNTS_PER_REV, (long)STEP_COUNTS);

    xTaskCreate(button_task, "button_task", 2048, NULL, 5, NULL);
    xTaskCreate(pid_task, "pid_task", 4096, NULL, 6, NULL);
}
