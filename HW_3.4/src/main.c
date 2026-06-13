#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "BUZZER";

// Конфіг 
#define BUZZER_PIN      GPIO_NUM_5
#define BUZZER_TIMER    LEDC_TIMER_0
#define BUZZER_CHANNEL  LEDC_CHANNEL_0
#define BUZZER_RES      LEDC_TIMER_10_BIT    // 0–1023
#define BUZZER_DUTY     512                  // 50% — максимальна гучність

#define TICK_MS         50   // часовий тік плеєра

// Ноти (частоти в Гц) 
#define NOTE_C4   262
#define NOTE_D4   294
#define NOTE_E4   330
#define NOTE_F4   349
#define NOTE_G4   392
#define NOTE_A4   440
#define NOTE_B4   494
#define NOTE_C5   523
#define NOTE_REST 0   // пауза

// Тривалість у тіках (1 тік = 50 мс) 
#define Q   4    // чверть    = 200 мс
#define H   8    // половина  = 400 мс
#define W   16   // ціла      = 800 мс
#define E   2    // восьма    = 100 мс

// Нота 
typedef struct {
  uint32_t freq;    // Гц, 0 = пауза
  uint32_t ticks;   // тривалість у тіках
} Note_t;

// ══════════════════════════════════════════════════════════════════════════════
// МЕЛОДІЇ
// ══════════════════════════════════════════════════════════════════════════════

// Twinkle Twinkle Little Star
static const Note_t TWINKLE[] = {
  {NOTE_C4,Q},{NOTE_C4,Q},{NOTE_G4,Q},{NOTE_G4,Q},
  {NOTE_A4,Q},{NOTE_A4,Q},{NOTE_G4,H},
  {NOTE_F4,Q},{NOTE_F4,Q},{NOTE_E4,Q},{NOTE_E4,Q},
  {NOTE_D4,Q},{NOTE_D4,Q},{NOTE_C4,H},
  {NOTE_G4,Q},{NOTE_G4,Q},{NOTE_F4,Q},{NOTE_F4,Q},
  {NOTE_E4,Q},{NOTE_E4,Q},{NOTE_D4,H},
  {NOTE_G4,Q},{NOTE_G4,Q},{NOTE_F4,Q},{NOTE_F4,Q},
  {NOTE_E4,Q},{NOTE_E4,Q},{NOTE_D4,H},
  {NOTE_C4,Q},{NOTE_C4,Q},{NOTE_G4,Q},{NOTE_G4,Q},
  {NOTE_A4,Q},{NOTE_A4,Q},{NOTE_G4,H},
  {NOTE_F4,Q},{NOTE_F4,Q},{NOTE_E4,Q},{NOTE_E4,Q},
  {NOTE_D4,Q},{NOTE_D4,Q},{NOTE_C4,H},
  {NOTE_REST,0}   // кінець
};

// Jingle Bells
static const Note_t JINGLE[] = {
  {NOTE_E4,Q},{NOTE_E4,Q},{NOTE_E4,H},
  {NOTE_E4,Q},{NOTE_E4,Q},{NOTE_E4,H},
  {NOTE_E4,Q},{NOTE_G4,Q},{NOTE_C4,Q},{NOTE_D4,Q},
  {NOTE_E4,W},
  {NOTE_F4,Q},{NOTE_F4,Q},{NOTE_F4,Q},{NOTE_F4,Q},
  {NOTE_F4,Q},{NOTE_E4,Q},{NOTE_E4,Q},{NOTE_E4,Q},
  {NOTE_E4,Q},{NOTE_D4,Q},{NOTE_D4,Q},{NOTE_E4,Q},
  {NOTE_D4,H},{NOTE_G4,H},
  {NOTE_REST,0}
};

// Baby Shark
static const Note_t BABY_SHARK[] = {
  {NOTE_C4,Q},{NOTE_D4,Q},{NOTE_E4,Q},{NOTE_REST,Q},
  {NOTE_C4,Q},{NOTE_D4,Q},{NOTE_E4,Q},{NOTE_REST,Q},
  {NOTE_C4,Q},{NOTE_D4,Q},{NOTE_E4,Q},{NOTE_REST,Q},
  {NOTE_C4,H},{NOTE_REST,H},
  {NOTE_REST,0}
};

// We Will Rock You
static const Note_t WE_WILL[] = {
  {NOTE_C4,Q},{NOTE_C4,Q},{NOTE_D4,H},
  {NOTE_C4,Q},{NOTE_C4,Q},{NOTE_D4,H},
  {NOTE_C4,Q},{NOTE_C4,Q},{NOTE_D4,Q},{NOTE_E4,H},
  {NOTE_REST,H},
  {NOTE_REST,0}
};

// Old MacDonald
static const Note_t OLD_MAC[] = {
  {NOTE_G4,Q},{NOTE_G4,Q},{NOTE_G4,Q},{NOTE_D4,Q},
  {NOTE_E4,Q},{NOTE_E4,Q},{NOTE_D4,H},
  {NOTE_B4,Q},{NOTE_B4,Q},{NOTE_A4,Q},{NOTE_A4,Q},
  {NOTE_G4,W},
  {NOTE_REST,0}
};

// Список мелодій
typedef struct {
  const char   *name;
  const Note_t *notes;
} Melody_t;

static const Melody_t PLAYLIST[] = {
  {"Twinkle Twinkle", TWINKLE   },
  {"Jingle Bells",    JINGLE    },
  {"Baby Shark",      BABY_SHARK},
  {"We Will Rock You",WE_WILL   },
  {"Old MacDonald",   OLD_MAC   },
};
#define PLAYLIST_LEN (sizeof(PLAYLIST) / sizeof(PLAYLIST[0]))

// Стан плеєра 
static struct {
  const Note_t *notes;        // поточна мелодія
  uint32_t      note_idx;     // індекс поточної ноти
  uint32_t      ticks_left;   // тіків до наступної ноти
  uint32_t      melody_idx;   // індекс мелодії у плейлисті
  bool          playing;
} g_player = {0};

// LEDC 
static void buzzer_init(void) {
  ledc_timer_config_t t = {
    .speed_mode      = LEDC_LOW_SPEED_MODE,
    .duty_resolution = BUZZER_RES,
    .timer_num       = BUZZER_TIMER,
    .freq_hz         = 1000,   // початкова — буде змінюватись
    .clk_cfg         = LEDC_AUTO_CLK,
  };
  ESP_ERROR_CHECK(ledc_timer_config(&t));

  ledc_channel_config_t ch = {
    .gpio_num   = BUZZER_PIN,
    .speed_mode = LEDC_LOW_SPEED_MODE,
    .channel    = BUZZER_CHANNEL,
    .timer_sel  = BUZZER_TIMER,
    .duty       = 0,
    .hpoint     = 0,
  };
  ESP_ERROR_CHECK(ledc_channel_config(&ch));
}

static void buzzer_play_freq(uint32_t freq) {
  if (freq == 0) {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BUZZER_CHANNEL, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BUZZER_CHANNEL);
  } else {
    ledc_set_freq(LEDC_LOW_SPEED_MODE, BUZZER_TIMER, freq);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BUZZER_CHANNEL, BUZZER_DUTY);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BUZZER_CHANNEL);
  }
}

static void buzzer_stop(void) {
  ledc_set_duty(LEDC_LOW_SPEED_MODE, BUZZER_CHANNEL, 0);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, BUZZER_CHANNEL);
}

// Плеєр: завантажити мелодію
static void player_load(uint32_t idx) {
  g_player.melody_idx = idx % PLAYLIST_LEN;
  g_player.notes      = PLAYLIST[g_player.melody_idx].notes;
  g_player.note_idx   = 0;
  g_player.ticks_left = g_player.notes[0].ticks;
  g_player.playing    = true;

  ESP_LOGI(TAG, "Playing: %s", PLAYLIST[g_player.melody_idx].name);
  buzzer_play_freq(g_player.notes[0].freq);
}

// Плеєр: тік (викликається кожні TICK_MS) 
// Не блокує, не використовує delay — лише просуває стан машини
static void player_tick(void *arg) {
  if (!g_player.playing) return;

  const Note_t *notes = g_player.notes;

  if (g_player.ticks_left > 0) {
    g_player.ticks_left--;
    return;
  }

  // Переходимо до наступної ноти
  g_player.note_idx++;
  const Note_t *n = &notes[g_player.note_idx];

  // Кінець мелодії — freq=0 і ticks=0
  if (n->freq == NOTE_REST && n->ticks == 0) {
    buzzer_stop();
    g_player.playing = false;
    ESP_LOGI(TAG, "Melody done. Next in 2s.");

    // Через 2 с — наступна мелодія
    // (обробляється в app_main loop)
    return;
  }

  g_player.ticks_left = n->ticks;
  buzzer_play_freq(n->freq);
}

// app_main
void app_main(void) {
  buzzer_init();

  // Таймер тіку плеєра — апаратний, не потребує задачі
  esp_timer_handle_t tick_timer;
  esp_timer_create_args_t tick_cfg = {
    .callback              = player_tick,
    .arg                   = NULL,
    .dispatch_method       = ESP_TIMER_TASK,
    .name                  = "player_tick",
    .skip_unhandled_events = true,
  };
  ESP_ERROR_CHECK(esp_timer_create(&tick_cfg, &tick_timer));
  ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, TICK_MS * 1000ULL));

  // Завантажуємо першу мелодію
  player_load(0);

  uint32_t next_melody = 1;
  int64_t  wait_until  = 0;

  // Головний цикл — не блокує, лише перемикає мелодії
  for (;;) {
    if (!g_player.playing) {
      int64_t now = esp_timer_get_time();
      if (wait_until == 0) {
        wait_until = now + 2000000LL;   // 2 с пауза між мелодіями
      } else if (now >= wait_until) {
        wait_until = 0;
        player_load(next_melody++);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
