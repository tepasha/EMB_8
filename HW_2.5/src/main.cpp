#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"

// Конфіг
#define FAN_PIN         GPIO_NUM_11
#define LED_FAN_PIN     GPIO_NUM_12   // LED дублює стан вентилятора
#define LED_IDLE_PIN    GPIO_NUM_13   // LED горить коли вентилятор вимкнений

// Час у мікросекундах (esp_timer працює в мкс)
// #define PERIOD_US       (60ULL * 60 * 1000 * 1000)   // 1 година
// #define ON_TIME_US      (15ULL * 60 * 1000 * 1000)   // 15 хвилин

#define PERIOD_US    (10ULL * 1000 * 1000)   // 10 секунд
#define ON_TIME_US   ( 3ULL * 1000 * 1000)   //  3 секунди

#define WDT_TIMEOUT_S   30   // watchdog таймаут

static const char *TAG = "FAN";

//Стан 
typedef enum {
  STATE_IDLE,     // вентилятор вимкнений, чекаємо наступного циклу
  STATE_RUNNING,  // вентилятор працює
} fan_state_t;

static volatile fan_state_t g_state     = STATE_IDLE;
static volatile bool        g_protected = false;   // захист від повторного запуску

static esp_timer_handle_t g_timer_period;   // таймер запуску (щогодини)
static esp_timer_handle_t g_timer_on;       // таймер вимкнення (через 15 хв)

// GPIO 
static void fan_set(bool on) {
  gpio_set_level(FAN_PIN,     on ? 1 : 0);
  gpio_set_level(LED_FAN_PIN, on ? 1 : 0);
  gpio_set_level(LED_IDLE_PIN, on ? 0 : 1);
}

// Callback: вимкнути вентилятор після ON_TIME_US ─
static void cb_fan_off(void *arg) {
  if (g_state != STATE_RUNNING) return;

  g_state     = STATE_IDLE;
  g_protected = false;
  fan_set(false);

  int64_t elapsed = ON_TIME_US / 1000;   // мс для логу
  ESP_LOGI(TAG, "Fan OFF — ran for %lld ms. Waiting for next cycle.", elapsed);
}

// Callback: ввімкнути вентилятор кожен PERIOD_US 
// Цей callback викликається апаратним esp_timer — незалежно від loop/task
static void cb_fan_on(void *arg) {
  // Захист від повторного запуску
  if (g_protected) {
    ESP_LOGW(TAG, "Fan already running — skipping cycle.");
    return;
  }

  g_state     = STATE_RUNNING;
  g_protected = true;
  fan_set(true);

  ESP_LOGI(TAG, "Fan ON — will run for %llu ms.", ON_TIME_US / 1000ULL);

  // Запускаємо одноразовий таймер вимкнення
  esp_timer_start_once(g_timer_on, ON_TIME_US);
}

// Watchdog task 
// Єдина задача — скидати WDT і перевіряти стан
static void task_watchdog(void *arg) {
  esp_task_wdt_add(NULL);   // реєструємо поточну задачу у WDT

  int64_t last_log = 0;

  for (;;) {
    esp_task_wdt_reset();   // скидаємо watchdog

    // Логуємо стан раз на 5 секунд
    int64_t now = esp_timer_get_time();
    if (now - last_log >= 5000000) {
      last_log = now;
      ESP_LOGI(TAG, "State: %s | uptime: %lld s",
               g_state == STATE_RUNNING ? "RUNNING" : "IDLE",
               now / 1000000LL);
    }

    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

// app_main
extern "C" void app_main(void) {

  // GPIO ініціалізація
  gpio_config_t out = {
    .pin_bit_mask = (1ULL << FAN_PIN)     |
                    (1ULL << LED_FAN_PIN)  |
                    (1ULL << LED_IDLE_PIN),
    .mode         = GPIO_MODE_OUTPUT,
    .pull_up_en   = GPIO_PULLUP_DISABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type    = GPIO_INTR_DISABLE,
  };
  gpio_config(&out);
  fan_set(false);   // початковий стан — вимкнено

  // Watchdog
  esp_task_wdt_config_t wdt_cfg = {
    .timeout_ms    = WDT_TIMEOUT_S * 1000,
    .idle_core_mask = 0,
    .trigger_panic  = true,
  };
  esp_task_wdt_reconfigure(&wdt_cfg);

  // Таймер: вимкнення вентилятора (одноразовий, запускається з cb_fan_on)
  esp_timer_create_args_t args_off = {
    .callback = cb_fan_off,
    .arg      = NULL,
    .dispatch_method       = ESP_TIMER_TASK,
    .name     = "fan_off",
    .skip_unhandled_events = false,
  };
  esp_timer_create(&args_off, &g_timer_on);

  // Таймер: запуск вентилятора кожен PERIOD_US (циклічний — сам себе повторює)
  esp_timer_create_args_t args_on = {
    .callback = cb_fan_on,
    .arg      = NULL,
    .dispatch_method       = ESP_TIMER_TASK,
    .name     = "fan_on",
    .skip_unhandled_events = false,
  };
  esp_timer_create(&args_on, &g_timer_period);

  // Перший запуск одразу
  cb_fan_on(NULL);

  // Після першого запуску — запускаємо циклічний таймер
  esp_timer_start_periodic(g_timer_period, PERIOD_US);

  ESP_LOGI(TAG, "System started. Period=%llu s, On-time=%llu s.",
           PERIOD_US / 1000000ULL, ON_TIME_US / 1000000ULL);

  // Watchdog задача (єдина задача в системі)
  xTaskCreate(task_watchdog, "wdt", 2048, NULL, 5, NULL);
}
