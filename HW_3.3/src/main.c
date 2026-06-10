#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"

// Конфіг
#define ADC_CHANNEL     ADC_CHANNEL_0   // GPIO1 — потенціометр

// LED PWM
#define LED_PIN         GPIO_NUM_2
#define LED_TIMER       LEDC_TIMER_0
#define LED_CHANNEL     LEDC_CHANNEL_0
#define LED_FREQ_HZ     5000
#define LED_RESOLUTION  LEDC_TIMER_12_BIT   // 0–4095

// Двигун PWM (через драйвер L298N / DRV8833 / MOSFET)
#define MOTOR_PIN       GPIO_NUM_4
#define MOTOR_TIMER     LEDC_TIMER_1
#define MOTOR_CHANNEL   LEDC_CHANNEL_1
#define MOTOR_FREQ_HZ   1000
#define MOTOR_RESOLUTION LEDC_TIMER_12_BIT  // 0–4095

#define SAMPLE_MS       50   // читаємо ADC кожні 50 мс

static const char *TAG = "POT";

// ADC
static adc_oneshot_unit_handle_t g_adc;
static adc_cali_handle_t         g_cali;
static bool                      g_cali_ok = false;

static void adc_init(void) {
  adc_oneshot_unit_init_cfg_t unit_cfg = {
    .unit_id  = ADC_UNIT_1,
    .ulp_mode = ADC_ULP_MODE_DISABLE,
  };
  ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &g_adc));

  adc_oneshot_chan_cfg_t chan_cfg = {
    .atten    = ADC_ATTEN_DB_12,
    .bitwidth = ADC_BITWIDTH_12,
  };
  ESP_ERROR_CHECK(adc_oneshot_config_channel(g_adc, ADC_CHANNEL, &chan_cfg));

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
  adc_cali_curve_fitting_config_t cali_cfg = {
    .unit_id  = ADC_UNIT_1,
    .chan     = ADC_CHANNEL,
    .atten   = ADC_ATTEN_DB_12,
    .bitwidth = ADC_BITWIDTH_12,
  };
  g_cali_ok = (adc_cali_create_scheme_curve_fitting(&cali_cfg, &g_cali) == ESP_OK);
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
  if (!g_cali_ok) {
    adc_cali_line_fitting_config_t cali_cfg = {
      .unit_id  = ADC_UNIT_1,
      .atten    = ADC_ATTEN_DB_12,
      .bitwidth = ADC_BITWIDTH_12,
    };
    g_cali_ok = (adc_cali_create_scheme_line_fitting(&cali_cfg, &g_cali) == ESP_OK);
  }
#endif

  ESP_LOGI(TAG, "ADC init — calibration: %s", g_cali_ok ? "YES" : "NO");
}

// LEDC (PWM)
static void ledc_init(void) {
  // Таймер LED
  ledc_timer_config_t led_timer = {
    .speed_mode      = LEDC_LOW_SPEED_MODE,
    .duty_resolution = LED_RESOLUTION,
    .timer_num       = LED_TIMER,
    .freq_hz         = LED_FREQ_HZ,
    .clk_cfg         = LEDC_AUTO_CLK,
  };
  ESP_ERROR_CHECK(ledc_timer_config(&led_timer));

  // Канал LED
  ledc_channel_config_t led_ch = {
    .gpio_num   = LED_PIN,
    .speed_mode = LEDC_LOW_SPEED_MODE,
    .channel    = LED_CHANNEL,
    .timer_sel  = LED_TIMER,
    .duty       = 0,
    .hpoint     = 0,
  };
  ESP_ERROR_CHECK(ledc_channel_config(&led_ch));

  // Таймер двигуна (окремий таймер — незалежний від LED)
  ledc_timer_config_t motor_timer = {
    .speed_mode      = LEDC_LOW_SPEED_MODE,
    .duty_resolution = MOTOR_RESOLUTION,
    .timer_num       = MOTOR_TIMER,
    .freq_hz         = MOTOR_FREQ_HZ,
    .clk_cfg         = LEDC_AUTO_CLK,
  };
  ESP_ERROR_CHECK(ledc_timer_config(&motor_timer));

  // Канал двигуна
  ledc_channel_config_t motor_ch = {
    .gpio_num   = MOTOR_PIN,
    .speed_mode = LEDC_LOW_SPEED_MODE,
    .channel    = MOTOR_CHANNEL,
    .timer_sel  = MOTOR_TIMER,
    .duty       = 0,
    .hpoint     = 0,
  };
  ESP_ERROR_CHECK(ledc_channel_config(&motor_ch));

  ESP_LOGI(TAG, "LEDC init — LED GPIO%d @ %dHz, Motor GPIO%d @ %dHz",
           LED_PIN, LED_FREQ_HZ, MOTOR_PIN, MOTOR_FREQ_HZ);
}

// Встановити duty 
static void set_duty(ledc_channel_t ch, uint32_t duty) {
  ledc_set_duty(LEDC_LOW_SPEED_MODE, ch, duty);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, ch);
}

// Головна задача 
static void task_control(void *arg) {
  uint32_t prev_duty_led   = 0xFFFF;
  uint32_t prev_duty_motor = 0xFFFF;

  printf("\n%-6s  %-10s  %-12s  %-12s\n",
         "RAW", "mV", "LED duty", "Motor duty");
  printf("%-6s  %-10s  %-12s  %-12s\n",
         "------", "----------", "------------", "------------");

  for (;;) {
    // Зчитати ADC
    int raw = 0;
    ESP_ERROR_CHECK(adc_oneshot_read(g_adc, ADC_CHANNEL, &raw));

    int mv = 0;
    if (g_cali_ok) {
      adc_cali_raw_to_voltage(g_cali, raw, &mv);
    } else {
      mv = raw * 3100 / 4095;
    }

    // RAW 0–4095 → duty 0–4095 (12 біт)
    uint32_t duty = (uint32_t)raw;

    // Оновлюємо лише якщо змінилось (зменшуємо шум)
    if (duty != prev_duty_led || duty != prev_duty_motor) {
      set_duty(LED_CHANNEL,   duty);
      set_duty(MOTOR_CHANNEL, duty);

      prev_duty_led   = duty;
      prev_duty_motor = duty;

      printf("%-6d  %-10d  %-12lu  %-12lu\n", raw, mv, duty, duty);
    }

    vTaskDelay(pdMS_TO_TICKS(SAMPLE_MS));
  }
}

// app_main 
void app_main(void) {
  adc_init();
  ledc_init();

  // LED і двигун на різних таймерах — не впливають одне на одного
  xTaskCreate(task_control, "ctrl", 4096, NULL, 5, NULL);
}
