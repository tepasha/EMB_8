#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

// Конфіг
#define BUTTON_PIN   GPIO_NUM_0
#define LED_PIN      GPIO_NUM_2
#define ACTIVE_TASK  1           // змінити на 1,2,3,4,5

static const char *TAG = "BTN";


// ЗАВДАННЯ 1: Базова реалізація — без debounce

#if ACTIVE_TASK == 1

static volatile uint32_t t1_counter = 0;

static void IRAM_ATTR t1_isr(void *arg) {
  t1_counter = t1_counter + 1;
}

static void t1_task(void *arg) {
  uint32_t last = 0;
  for (;;) {
    uint32_t c = t1_counter;
    if (c != last) {
      last = c;
      ESP_LOGI(TAG, "[T1] Counter: %lu", c);
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

static void task_setup(void) {
  gpio_config_t cfg = {
    .pin_bit_mask = (1ULL << BUTTON_PIN),
    .mode         = GPIO_MODE_INPUT,
    .pull_up_en   = GPIO_PULLUP_ENABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type    = GPIO_INTR_NEGEDGE,   // FALLING
  };
  gpio_config(&cfg);
  gpio_install_isr_service(0);
  gpio_isr_handler_add(BUTTON_PIN, t1_isr, NULL);

  xTaskCreatePinnedToCore(t1_task, "t1", 2048, NULL, 5, NULL, 1);
  ESP_LOGI(TAG, "[T1] Базова реалізація без debounce");
}


// ЗАВДАННЯ 2: Software debounce — time-based (50 мс)

#elif ACTIVE_TASK == 2

static volatile uint32_t t2_counter     = 0;
static volatile int64_t  t2_lastIrqUs   = 0;
#define T2_DEBOUNCE_US  (50 * 1000)   // 50 мс в мікросекундах

static void IRAM_ATTR t2_isr(void *arg) {
  int64_t now = esp_timer_get_time();   // мкс, безпечно в ISR
  if (now - t2_lastIrqUs >= T2_DEBOUNCE_US) {
    t2_lastIrqUs = now;
    t2_counter++;
  }
}

static void t2_task(void *arg) {
  uint32_t last = 0;
  for (;;) {
    uint32_t c = t2_counter;
    if (c != last) {
      last = c;
      ESP_LOGI(TAG, "[T2] Counter: %lu", c);
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

static void task_setup(void) {
  gpio_config_t cfg = {
    .pin_bit_mask = (1ULL << BUTTON_PIN),
    .mode         = GPIO_MODE_INPUT,
    .pull_up_en   = GPIO_PULLUP_ENABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type    = GPIO_INTR_NEGEDGE,
  };
  gpio_config(&cfg);
  gpio_install_isr_service(0);
  gpio_isr_handler_add(BUTTON_PIN, t2_isr, NULL);

  xTaskCreatePinnedToCore(t2_task, "t2", 2048, NULL, 5, NULL, 1);
  ESP_LOGI(TAG, "[T2] Time-based debounce 50ms");
}


// ЗАВДАННЯ 3: State-based debounce — перевірка рівня поза ISR

#elif ACTIVE_TASK == 3

static QueueHandle_t t3_queue;
static uint32_t      t3_counter = 0;

static void IRAM_ATTR t3_isr(void *arg) {
  uint8_t val = 1;
  xQueueSendFromISR(t3_queue, &val, NULL);  // лише сигналізуємо
}

static void t3_task(void *arg) {
  uint8_t event;
  for (;;) {
    if (xQueueReceive(t3_queue, &event, portMAX_DELAY)) {
      // Приймаємо лише якщо кнопка досі натиснута
      if (gpio_get_level(BUTTON_PIN) == 0) {
        t3_counter++;
        ESP_LOGI(TAG, "[T3] Counter: %lu  (кнопка натиснута)", t3_counter);
      } else {
        ESP_LOGI(TAG, "[T3] Ігнорую — кнопка вже відпущена");
      }
    }
  }
}

static void task_setup(void) {
  t3_queue = xQueueCreate(8, sizeof(uint8_t));

  gpio_config_t cfg = {
    .pin_bit_mask = (1ULL << BUTTON_PIN),
    .mode         = GPIO_MODE_INPUT,
    .pull_up_en   = GPIO_PULLUP_ENABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type    = GPIO_INTR_NEGEDGE,
  };
  gpio_config(&cfg);
  gpio_install_isr_service(0);
  gpio_isr_handler_add(BUTTON_PIN, t3_isr, NULL);

  xTaskCreatePinnedToCore(t3_task, "t3", 2048, NULL, 5, NULL, 1);
  ESP_LOGI(TAG, "[T3] State-based debounce");
}


// ЗАВДАННЯ 4: Polling + state machine debounce (без interrupts)

#elif ACTIVE_TASK == 4

typedef enum { IDLE, PRESSED, HELD, RELEASED } btn_state_t;

static void t4_task(void *arg) {
  btn_state_t state        = IDLE;
  int64_t     stateEnterUs = 0;
  uint32_t    counter      = 0;
  const int64_t DEBOUNCE_US = 50 * 1000;  // 50 мс

  for (;;) {
    bool    pressed = (gpio_get_level(BUTTON_PIN) == 0);
    int64_t now     = esp_timer_get_time();

    switch (state) {
      case IDLE:
        if (pressed) {
          state        = PRESSED;
          stateEnterUs = now;
        }
        break;

      case PRESSED:
        if (!pressed) {
          state = IDLE;   // шум — повертаємось
        } else if (now - stateEnterUs >= DEBOUNCE_US) {
          state = HELD;
          counter++;
          ESP_LOGI(TAG, "[T4] Counter: %lu", counter);
        }
        break;

      case HELD:
        if (!pressed) {
          state        = RELEASED;
          stateEnterUs = now;
        }
        break;

      case RELEASED:
        if (pressed) {
          state        = PRESSED;
          stateEnterUs = now;
        } else if (now - stateEnterUs >= DEBOUNCE_US) {
          state = IDLE;
        }
        break;
    }

    vTaskDelay(pdMS_TO_TICKS(10));  // опитуємо кожні 10 мс
  }
}

static void task_setup(void) {
  gpio_config_t cfg = {
    .pin_bit_mask = (1ULL << BUTTON_PIN),
    .mode         = GPIO_MODE_INPUT,
    .pull_up_en   = GPIO_PULLUP_ENABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type    = GPIO_INTR_DISABLE,   // interrupt не потрібен
  };
  gpio_config(&cfg);

  xTaskCreatePinnedToCore(t4_task, "t4", 2048, NULL, 5, NULL, 1);
  ESP_LOGI(TAG, "[T4] Polling + state machine debounce");
}


// ЗАВДАННЯ 5: Hardware RC debounce + порівняння всіх методів

#elif ACTIVE_TASK == 5

// Схема RC фільтра:
//
//   3.3V
//    │
//   10kΩ  ← підтяг (зовнішній, INPUT без PULLUP)
//    │
//    ├──────── GPIO
//    │
//   100Ω
//    │
//   100nF → GND
//    │
//  [BTN]
//    │
//   GND

static volatile uint32_t t5_c1       = 0;   // T1 без debounce
static volatile uint32_t t5_c2       = 0;   // T2 time-based
static volatile int64_t  t5_lastUs   = 0;
static QueueHandle_t     t5_queue;           // T3 state-based
static uint32_t          t5_c3       = 0;
static uint32_t          t5_c4       = 0;   // T4 polling

// ISR — T1 + T2 + T3 сигнал
static void IRAM_ATTR t5_isr(void *arg) {
  // T1: завжди інкрементуємо
  t5_c1++;

  // T2: з debounce 10 мс (з RC можна менше)
  int64_t now = esp_timer_get_time();
  if (now - t5_lastUs >= 10000) {
    t5_lastUs = now;
    t5_c2++;
    uint8_t val = 1;
    xQueueSendFromISR(t5_queue, &val, NULL);  // T3 сигнал
  }
}

typedef enum { IDLE, PRESSED, HELD, RELEASED } btn_state_t;

static void t5_task(void *arg) {
  btn_state_t state        = IDLE;
  int64_t     stateEnterUs = 0;
  uint8_t     event;
  int64_t     lastPrint    = 0;
  const int64_t DEBOUNCE_US = 10000;  // 10 мс (з RC достатньо)

  for (;;) {
    int64_t now = esp_timer_get_time();

    // T3: state-based з черги
    if (xQueueReceive(t5_queue, &event, 0) == pdTRUE) {
      if (gpio_get_level(BUTTON_PIN) == 0) {
        t5_c3++;
      }
    }

    // T4: polling state machine
    bool pressed = (gpio_get_level(BUTTON_PIN) == 0);
    switch (state) {
      case IDLE:
        if (pressed) { state = PRESSED; stateEnterUs = now; }
        break;
      case PRESSED:
        if (!pressed) { state = IDLE; }
        else if (now - stateEnterUs >= DEBOUNCE_US) {
          state = HELD;
          t5_c4++;
        }
        break;
      case HELD:
        if (!pressed) { state = RELEASED; stateEnterUs = now; }
        break;
      case RELEASED:
        if (pressed) { state = PRESSED; stateEnterUs = now; }
        else if (now - stateEnterUs >= DEBOUNCE_US) { state = IDLE; }
        break;
    }

    // Порівняльний лог кожну секунду
    if (now - lastPrint >= 1000000) {
      lastPrint = now;
      ESP_LOGI(TAG, "────────────────────────────────");
      ESP_LOGI(TAG, "[T5] T1 без debounce:    %lu", t5_c1);
      ESP_LOGI(TAG, "[T5] T2 time-based 10ms: %lu", t5_c2);
      ESP_LOGI(TAG, "[T5] T3 state-based:     %lu", t5_c3);
      ESP_LOGI(TAG, "[T5] T4 polling SM:      %lu", t5_c4);
    }

    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

static void task_setup(void) {
  t5_queue = xQueueCreate(8, sizeof(uint8_t));

  gpio_config_t cfg = {
    .pin_bit_mask = (1ULL << BUTTON_PIN),
    .mode         = GPIO_MODE_INPUT,
    .pull_up_en   = GPIO_PULLUP_DISABLE,  // зовнішній підтяг 10кОм
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type    = GPIO_INTR_NEGEDGE,
  };
  gpio_config(&cfg);
  gpio_install_isr_service(0);
  gpio_isr_handler_add(BUTTON_PIN, t5_isr, NULL);

  xTaskCreatePinnedToCore(t5_task, "t5", 4096, NULL, 5, NULL, 1);
  ESP_LOGI(TAG, "[T5] Hardware RC debounce + порівняння");
}

#endif


// app_main

extern "C" void app_main(void) {
  gpio_config_t led_cfg = {
    .pin_bit_mask = (1ULL << LED_PIN),
    .mode         = GPIO_MODE_OUTPUT,
    .pull_up_en   = GPIO_PULLUP_DISABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type    = GPIO_INTR_DISABLE,
  };
  gpio_config(&led_cfg);

  task_setup();
}
