#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"

static const char *TAG = "SERVO";

// Конфіг
#define ADC_CHANNEL     ADC_CHANNEL_0   // GPIO1 — потенціометр
#define SERVO_PIN       GPIO_NUM_4      // сигнальний пін сервоприводу

#define SERVO_TIMER      LEDC_TIMER_0
#define SERVO_CHANNEL    LEDC_CHANNEL_0
#define SERVO_FREQ_HZ    50
#define SERVO_RES        LEDC_TIMER_14_BIT
#define SERVO_MAX_RAW    16383

#define SERVO_DUTY_MIN   819    // 1.0 мс → 0°
#define SERVO_DUTY_MAX   1638   // 2.0 мс → 180°

// Діапазон сервоприводу в градусах
#define SERVO_ANGLE_MIN  0
#define SERVO_ANGLE_MAX  180

// Діапазон потенціометра (ADC 12 біт)
#define POT_MIN          0
#define POT_MAX          4095

// Обрізання діапазону 
#define OVERLAP_ANGLE_MIN  0     // збіжний діапазон — початок
#define OVERLAP_ANGLE_MAX  180   // збіжний діапазон — кінець

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

  ESP_LOGI(TAG, "ADC init — калібрування: %s", g_cali_ok ? "YES" : "NO");
}

// LEDC (PWM для сервоприводу)
static void servo_init(void) {
  ledc_timer_config_t timer = {
    .speed_mode      = LEDC_LOW_SPEED_MODE,
    .duty_resolution = SERVO_RES,
    .timer_num       = SERVO_TIMER,
    .freq_hz         = SERVO_FREQ_HZ,
    .clk_cfg         = LEDC_AUTO_CLK,
  };
  ESP_ERROR_CHECK(ledc_timer_config(&timer));

  ledc_channel_config_t ch = {
    .gpio_num   = SERVO_PIN,
    .speed_mode = LEDC_LOW_SPEED_MODE,
    .channel    = SERVO_CHANNEL,
    .timer_sel  = SERVO_TIMER,
    .duty       = SERVO_DUTY_MIN,
    .hpoint     = 0,
  };
  ESP_ERROR_CHECK(ledc_channel_config(&ch));

  ESP_LOGI(TAG, "Servo init — GPIO%d @ %dHz", SERVO_PIN, SERVO_FREQ_HZ);
}

static void servo_set_angle(float angle) {
  // Перетворюємо кут → duty cycle
  uint32_t duty = (uint32_t)(
    SERVO_DUTY_MIN +
    (angle - SERVO_ANGLE_MIN) *
    (SERVO_DUTY_MAX - SERVO_DUTY_MIN) /
    (SERVO_ANGLE_MAX - SERVO_ANGLE_MIN)
  );
  ledc_set_duty(LEDC_LOW_SPEED_MODE, SERVO_CHANNEL, duty);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, SERVO_CHANNEL);
}

// Map float 
static float mapf(float x,
                  float in_min,  float in_max,
                  float out_min, float out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

// Головна задача 
static void task_servo(void *arg) {
  float prev_angle = -1.0f;

  printf("\n%-8s  %-10s  %-12s  %-12s\n",
         "RAW", "mV", "Angle(°)", "Offset(°)");
  printf("%-8s  %-10s  %-12s  %-12s\n",
         "--------", "----------", "------------", "------------");

  for (;;) {
    // Зчитуємо ADC
    int raw = 0;
    ESP_ERROR_CHECK(adc_oneshot_read(g_adc, ADC_CHANNEL, &raw));

    int mv = 0;
    if (g_cali_ok) {
      adc_cali_raw_to_voltage(g_cali, raw, &mv);
    } else {
      mv = raw * 3100 / 4095;
    }

    // RAW → кут потенціометра (0–180°, повний діапазон)
    float pot_angle = mapf((float)raw,
                           POT_MIN, POT_MAX,
                           SERVO_ANGLE_MIN, SERVO_ANGLE_MAX);

    // Обрізаємо до збіжного діапазону
    float clamped = pot_angle;
    if (clamped < OVERLAP_ANGLE_MIN) clamped = OVERLAP_ANGLE_MIN;
    if (clamped > OVERLAP_ANGLE_MAX) clamped = OVERLAP_ANGLE_MAX;

    // Ремапуємо збіжний діапазон на повний діапазон сервоприводу
    float servo_angle = mapf(clamped,
                             OVERLAP_ANGLE_MIN, OVERLAP_ANGLE_MAX,
                             SERVO_ANGLE_MIN,   SERVO_ANGLE_MAX);

    // Відхилення від крайнього лівого положення
    float offset = servo_angle - SERVO_ANGLE_MIN;

    // Оновлюємо серво тільки якщо кут змінився більше ніж на 0.5°
    if (servo_angle < prev_angle - 0.5f ||
        servo_angle > prev_angle + 0.5f) {

      servo_set_angle(servo_angle);
      prev_angle = servo_angle;

      printf("%-8d  %-10d  %-12.1f  %-12.1f\n",
             raw, mv, servo_angle, offset);
    }

    vTaskDelay(pdMS_TO_TICKS(20));   // 50 Гц оновлення
  }
}

// app_main
void app_main(void) {
  adc_init();
  servo_init();

  xTaskCreate(task_servo, "servo", 4096, NULL, 5, NULL);
}
