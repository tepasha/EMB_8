#include <stdio.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "ENC";

// Піни
#define ENC_A       GPIO_NUM_6    // енкодер A
#define ENC_B       GPIO_NUM_7    // енкодер B
#define ENC_BTN     GPIO_NUM_8    // кнопка енкодера
#define SERVO_PIN   GPIO_NUM_4    // сервопривід
#define BUZZER_PIN  GPIO_NUM_5    // зумер

// Сервопривід
#define SERVO_TIMER     LEDC_TIMER_0
#define SERVO_CHANNEL   LEDC_CHANNEL_0
#define SERVO_FREQ_HZ   50
#define SERVO_RES       LEDC_TIMER_14_BIT
#define SERVO_DUTY_MIN  819    // 1.0 мс → 0°
#define SERVO_DUTY_MAX  1638   // 2.0 мс → 180°
#define SERVO_MIN       0.0f
#define SERVO_MAX       180.0f
#define SERVO_CENTER    90.0f

// Зумер 
#define BUZZER_TIMER    LEDC_TIMER_1
#define BUZZER_CHANNEL  LEDC_CHANNEL_1
#define BUZZER_RES      LEDC_TIMER_10_BIT

// Енкодер події 
typedef enum {
  ENC_CW   = 1,
  ENC_CCW  = -1,
  ENC_PRESS,
  ENC_LONG,
} enc_event_t;

static QueueHandle_t g_enc_queue;

// Активний режим
typedef enum {
  MODE_SERVO = 0,
  MODE_SAFE,
  MODE_CALC,
} app_mode_t;

static volatile app_mode_t g_mode = MODE_SERVO;
static volatile int64_t g_btn_press_us = 0;
static volatile bool    g_btn_down     = false;
static volatile uint8_t g_enc_state    = 0;

// Таблиця переходів для квадратурного енкодера
static const int8_t ENC_TABLE[16] = {
   0, -1,  1,  0,
   1,  0,  0, -1,
  -1,  0,  0,  1,
   0,  1, -1,  0,
};

static void IRAM_ATTR enc_isr(void *arg) {
  uint8_t a = gpio_get_level(ENC_A);
  uint8_t b = gpio_get_level(ENC_B);
  g_enc_state = ((g_enc_state << 2) | (a << 1) | b) & 0x0F;
  int8_t dir  = ENC_TABLE[g_enc_state];
  if (dir != 0) {
    enc_event_t ev = (dir > 0) ? ENC_CW : ENC_CCW;
    xQueueSendFromISR(g_enc_queue, &ev, NULL);
  }
}

static void IRAM_ATTR btn_isr(void *arg) {
  bool pressed = (gpio_get_level(ENC_BTN) == 0);
  if (pressed) {
    g_btn_press_us = esp_timer_get_time();
    g_btn_down     = true;
  } else if (g_btn_down) {
    g_btn_down = false;
    int64_t held = esp_timer_get_time() - g_btn_press_us;
    enc_event_t ev = (held > 1000000LL) ? ENC_LONG : ENC_PRESS;
    xQueueSendFromISR(g_enc_queue, &ev, NULL);
  }
}

static void gpio_init(void) {
  // Енкодер A, B
  gpio_config_t enc_cfg = {
    .pin_bit_mask = (1ULL << ENC_A) | (1ULL << ENC_B),
    .mode         = GPIO_MODE_INPUT,
    .pull_up_en   = GPIO_PULLUP_ENABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type    = GPIO_INTR_ANYEDGE,
  };
  gpio_config(&enc_cfg);

  // Кнопка
  gpio_config_t btn_cfg = {
    .pin_bit_mask = (1ULL << ENC_BTN),
    .mode         = GPIO_MODE_INPUT,
    .pull_up_en   = GPIO_PULLUP_ENABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type    = GPIO_INTR_ANYEDGE,
  };
  gpio_config(&btn_cfg);

  gpio_install_isr_service(0);
  gpio_isr_handler_add(ENC_A,   enc_isr, NULL);
  gpio_isr_handler_add(ENC_B,   enc_isr, NULL);
  gpio_isr_handler_add(ENC_BTN, btn_isr, NULL);
}

static void ledc_init(void) {
  // Серво таймер
  ledc_timer_config_t st = {
    .speed_mode      = LEDC_LOW_SPEED_MODE,
    .duty_resolution = SERVO_RES,
    .timer_num       = SERVO_TIMER,
    .freq_hz         = SERVO_FREQ_HZ,
    .clk_cfg         = LEDC_AUTO_CLK,
  };
  ledc_timer_config(&st);

  ledc_channel_config_t sc = {
    .gpio_num   = SERVO_PIN,
    .speed_mode = LEDC_LOW_SPEED_MODE,
    .channel    = SERVO_CHANNEL,
    .timer_sel  = SERVO_TIMER,
    .duty       = SERVO_DUTY_MIN,
    .hpoint     = 0,
  };
  ledc_channel_config(&sc);

  // Зумер таймер
  ledc_timer_config_t bt = {
    .speed_mode      = LEDC_LOW_SPEED_MODE,
    .duty_resolution = BUZZER_RES,
    .timer_num       = BUZZER_TIMER,
    .freq_hz         = 1000,
    .clk_cfg         = LEDC_AUTO_CLK,
  };
  ledc_timer_config(&bt);

  ledc_channel_config_t bc = {
    .gpio_num   = BUZZER_PIN,
    .speed_mode = LEDC_LOW_SPEED_MODE,
    .channel    = BUZZER_CHANNEL,
    .timer_sel  = BUZZER_TIMER,
    .duty       = 0,
    .hpoint     = 0,
  };
  ledc_channel_config(&bc);
}

static void servo_set(float angle) {
  if (angle < SERVO_MIN) angle = SERVO_MIN;
  if (angle > SERVO_MAX) angle = SERVO_MAX;
  uint32_t duty = (uint32_t)(
    SERVO_DUTY_MIN +
    (angle - SERVO_MIN) *
    (SERVO_DUTY_MAX - SERVO_DUTY_MIN) /
    (SERVO_MAX - SERVO_MIN)
  );
  ledc_set_duty(LEDC_LOW_SPEED_MODE, SERVO_CHANNEL, duty);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, SERVO_CHANNEL);
}

static void buzzer_tone(uint32_t freq_hz, uint32_t ms) {
  if (freq_hz == 0) {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BUZZER_CHANNEL, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BUZZER_CHANNEL);
    vTaskDelay(pdMS_TO_TICKS(ms));
    return;
  }
  ledc_set_freq(LEDC_LOW_SPEED_MODE, BUZZER_TIMER, freq_hz);
  ledc_set_duty(LEDC_LOW_SPEED_MODE, BUZZER_CHANNEL, 512);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, BUZZER_CHANNEL);
  vTaskDelay(pdMS_TO_TICKS(ms));
  ledc_set_duty(LEDC_LOW_SPEED_MODE, BUZZER_CHANNEL, 0);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, BUZZER_CHANNEL);
}

static void buzzer_beep_limit(void) {
  buzzer_tone(1200, 80);
}

static void buzzer_success(void) {
  buzzer_tone(523, 100); vTaskDelay(pdMS_TO_TICKS(50));
  buzzer_tone(659, 100); vTaskDelay(pdMS_TO_TICKS(50));
  buzzer_tone(784, 200);
}

static void buzzer_alarm(void) {
  for (int i = 0; i < 6; i++) {
    buzzer_tone(880, 150);
    vTaskDelay(pdMS_TO_TICKS(100));
    buzzer_tone(440, 150);
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

// ЗАВДАННЯ 1: СЕРВО
static void task_servo(void *arg) {
  float angle      = SERVO_CENTER;
  float step       = 5.0f;    // початковий крок 5°
  bool  fine_mode  = false;   // false = 5°, true = 2.5°

  servo_set(angle);
  ESP_LOGI(TAG, "[SERVO] Старт. Кут=%.1f° Крок=%.1f°", angle, step);

  enc_event_t ev;
  for (;;) {
    if (xQueueReceive(g_enc_queue, &ev, portMAX_DELAY) != pdTRUE) continue;

    switch (ev) {
      case ENC_CCW: {
        float delta  = (ev == ENC_CW) ? step : -step;
        float target = angle + delta;

        if (target < SERVO_MIN) {
          buzzer_beep_limit();
          ESP_LOGW(TAG, "[SERVO] Ліва межа!");
          break;
        }
        if (target > SERVO_MAX) {
          buzzer_beep_limit();
          ESP_LOGW(TAG, "[SERVO] Права межа!");
          break;
        }
        angle = target;
        servo_set(angle);
        float offset = angle - SERVO_MIN;
        ESP_LOGI(TAG, "[SERVO] Кут=%.1f°  Відхилення від 0°=%.1f°", angle, offset);
        break;
      }

      case ENC_PRESS:
        // Перемикаємо точність: 5° ↔ 2.5°
        fine_mode = !fine_mode;
        step      = fine_mode ? 2.5f : 5.0f;
        ESP_LOGI(TAG, "[SERVO] Режим: %s (крок %.1f°)",
                 fine_mode ? "ТОЧНИЙ" : "ГРУБИЙ", step);
        break;

      case ENC_LONG:
        // Довге утримання → центр
        angle = SERVO_CENTER;
        servo_set(angle);
        buzzer_tone(600, 100);
        ESP_LOGI(TAG, "[SERVO] Центр → 90°");
        break;
      default:
        ESP_LOGW(TAG, "[SERVO] Невідома подія: %d", ev);
        break;
    }
  }
}

// ЗАВДАННЯ 2: СЕЙФ
#define SAFE_CODE_LEN   4       // довжина коду
#define SAFE_MAX_TRIES  3       // максимум спроб
static const uint8_t SAFE_SECRET[SAFE_CODE_LEN] = {3, 7, 1, 5};  // секретний код

static void task_safe(void *arg) {
  uint8_t  input[SAFE_CODE_LEN] = {0};
  uint8_t  digit_idx  = 0;   // поточна позиція
  uint8_t  cur_digit  = 0;   // поточна цифра
  int8_t   last_dir   = 0;   // останній напрямок (для визначення зміни)
  uint8_t  tries      = 0;

  printf("\n[SAFE] Введіть %d-значний код:\n", SAFE_CODE_LEN);
  printf("[SAFE] > ");
  fflush(stdout);

  enc_event_t ev;
  for (;;) {
    if (xQueueReceive(g_enc_queue, &ev, portMAX_DELAY) != pdTRUE) continue;

    if (ev == ENC_PRESS || ev == ENC_LONG) {
      // Скидання → нова спроба
      tries++;
      ESP_LOGW(TAG, "[SAFE] Скидання! Спроба %d/%d", tries, SAFE_MAX_TRIES);

      if (tries >= SAFE_MAX_TRIES) {
        printf("\n[SAFE] ЗАБЛОКОВАНО!\n");
        buzzer_alarm();
        ESP_LOGE(TAG, "[SAFE] Вичерпано спроби — блокування!");
        while (true) vTaskDelay(pdMS_TO_TICKS(1000));
      }

      memset(input, 0, sizeof(input));
      digit_idx = 0;
      cur_digit = 0;
      last_dir  = 0;
      printf("\n[SAFE] > ");
      fflush(stdout);
      continue;
    }

    int8_t dir = (ev == ENC_CW) ? 1 : -1;

    if (digit_idx >= SAFE_CODE_LEN) continue;

    // Зміна напрямку → підтверджуємо поточну цифру, переходимо до наступної
    if (last_dir != 0 && dir != last_dir) {
      input[digit_idx] = cur_digit;
      printf("%d", cur_digit);
      fflush(stdout);
      digit_idx++;
      cur_digit = 0;
      last_dir  = 0;

      if (digit_idx >= SAFE_CODE_LEN) {
        // Перевірка коду
        printf("\n[SAFE] Перевірка...\n");
        bool ok = (memcmp(input, SAFE_SECRET, SAFE_CODE_LEN) == 0);
        if (ok) {
          printf("[SAFE] ВІДЧИНЕНО!\n");
          buzzer_success();
          // Повертаємо серво (замок відкривається)
          servo_set(90.0f);
          vTaskDelay(pdMS_TO_TICKS(3000));
          servo_set(0.0f);
        } else {
          tries++;
          printf("[SAFE] НЕВІРНО! Спроба %d/%d\n", tries, SAFE_MAX_TRIES);
          buzzer_beep_limit();
          if (tries >= SAFE_MAX_TRIES) {
            printf("[SAFE] ЗАБЛОКОВАНО!\n");
            buzzer_alarm();
            while (true) vTaskDelay(pdMS_TO_TICKS(1000));
          }
        }
        // Скидаємо для нової спроби
        memset(input, 0, sizeof(input));
        digit_idx = 0;
        cur_digit = 0;
        last_dir  = 0;
        printf("[SAFE] > ");
        fflush(stdout);
      }
      continue;
    }

    // Крутимо цифру
    last_dir = dir;
    if (dir == 1) {
      cur_digit = (cur_digit + 1) % 10;
    } else {
      cur_digit = (cur_digit == 0) ? 9 : cur_digit - 1;
    }

    // \b для перемальовування поточної цифри на місці
    if (digit_idx > 0 || last_dir != dir) printf("\b");
    printf("%d", cur_digit);
    fflush(stdout);
  }
}

// ЗАВДАННЯ 3: КАЛЬКУЛЯТОР
typedef enum {
  CALC_OP_ADD = 0,
  CALC_OP_SUB,
  CALC_OP_MUL,
  CALC_OP_DIV,
} calc_op_t;

static const char *OP_STR[] = {"+", "-", "*", "/"};

typedef enum {
  CALC_STATE_A,    // вводимо перший операнд
  CALC_STATE_OP,   // вибираємо операцію
  CALC_STATE_B,    // вводимо другий операнд
} calc_state_t;

static void task_calc(void *arg) {
  calc_state_t state = CALC_STATE_A;
  int32_t  operand_a = 0;
  int32_t  operand_b = 0;
  calc_op_t op       = CALC_OP_ADD;

  printf("\n[CALC] Введіть перший операнд:\n");
  printf("[CALC] %ld",operand_a);
  fflush(stdout);

  enc_event_t ev;
  for (;;) {
    if (xQueueReceive(g_enc_queue, &ev, portMAX_DELAY) != pdTRUE) continue;

    switch (state) {

      // Перший операнд
      case CALC_STATE_A:
        if (ev == ENC_CW || ev == ENC_CCW) {
          int8_t dir  = (ev == ENC_CW) ? 1 : -1;
          operand_a  += dir;
          // \b стільки разів скільки цифр + знак мінус
          int digits = (int)log10f(fabsf((float)operand_a) + 1) + 1
                       + (operand_a < 0 ? 1 : 0);
          for (int i = 0; i < digits + 2; i++) printf("\b");
          printf("%ld", operand_a);
          fflush(stdout);
        }
        if (ev == ENC_PRESS) {
          state = CALC_STATE_OP;
          printf(" %s ", OP_STR[op]);
          fflush(stdout);
          ESP_LOGI(TAG, "[CALC] Операнд A = %ld", (long)operand_a);
        }
        break;

      // Операція
      case CALC_STATE_OP:
        if (ev == ENC_CW || ev == ENC_CCW) {
          // Стираємо поточний оператор (3 символи: ' + ')
          printf("\b\b\b");
          int8_t dir = (ev == ENC_CW) ? 1 : -1;
          op = (calc_op_t)((op + dir + 4) % 4);
          printf(" %s ", OP_STR[op]);
          fflush(stdout);
        }
        if (ev == ENC_PRESS) {
          state = CALC_STATE_B;
          printf("%ld", operand_b);
          fflush(stdout);
          ESP_LOGI(TAG, "[CALC] Операція: %s", OP_STR[op]);
        }
        break;

      // Другий операнд
      case CALC_STATE_B:
        if (ev == ENC_CW || ev == ENC_CCW) {
          int8_t dir  = (ev == ENC_CW) ? 1 : -1;
          operand_b  += dir;
          int digits = (int)log10f(fabsf((float)operand_b) + 1) + 1
                       + (operand_b < 0 ? 1 : 0);
          for (int i = 0; i < digits + 2; i++) printf("\b");
          printf("%ld", operand_b);
          fflush(stdout);
        }
        if (ev == ENC_PRESS) {
          // Обчислення
          float result = 0;
          bool  err    = false;

          switch (op) {
            case CALC_OP_ADD: result = operand_a + operand_b; break;
            case CALC_OP_SUB: result = operand_a - operand_b; break;
            case CALC_OP_MUL: result = operand_a * operand_b; break;
            case CALC_OP_DIV:
              if (operand_b == 0) {
                printf(" = ERR(div/0)\n");
                err = true;
                buzzer_beep_limit();
              } else {
                result = (float)operand_a / (float)operand_b;
              }
              break;
          }

          if (!err) {
            // Виводимо результат
            if (op == CALC_OP_DIV) {
              printf(" = %.4f\n", result);
            } else {
              printf(" = %ld\n", (long)result);
            }
            buzzer_tone(800, 80);
          }

          // Скидаємо для нового виразу
          operand_a = 0;
          operand_b = 0;
          op        = CALC_OP_ADD;
          state     = CALC_STATE_A;
          printf("[CALC] %ld", operand_a);
          fflush(stdout);
        }
        break;
      default:
        ESP_LOGW(TAG, "[CALC] Невідомий стан: %d", state);
        break;
    }

    // ENC_LONG в будь-якому стані — повний скид
    if (ev == ENC_LONG) {
      operand_a = 0;
      operand_b = 0;
      op        = CALC_OP_ADD;
      state     = CALC_STATE_A;
      printf("\n[CALC] Скид. %ld", operand_a);
      fflush(stdout);
    }
  }
}

// app_main
void app_main(void) {
  g_enc_queue = xQueueCreate(32, sizeof(enc_event_t));

  gpio_init();
  ledc_init();

  servo_set(SERVO_CENTER);

  // Змінюй: MODE_SERVO / MODE_SAFE / MODE_CALC
  g_mode = MODE_SERVO;

  switch (g_mode) {
    case MODE_SERVO:
      xTaskCreate(task_servo, "servo", 4096, NULL, 5, NULL);
      break;
    case MODE_SAFE:
      xTaskCreate(task_safe,  "safe",  4096, NULL, 5, NULL);
      break;
    case MODE_CALC:
      xTaskCreate(task_calc,  "calc",  4096, NULL, 5, NULL);
      break;
    default:
      ESP_LOGE(TAG, "Невідомий режим!");
  }
}
