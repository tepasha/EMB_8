#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

static const char *TAG = "SMART_LIGHT";

// Конфігурація інваріантів (GPIO та ADC)
#define LED_PIN             GPIO_NUM_21
#define ADC1_LDR_CHANNEL    ADC_CHANNEL_2  // Наприклад, GPIO3 на ESP32-S3

// Параметри фільтрації та гістерезису
#define SMA_WINDOW_SIZE     8      // Розмір вікна ковзного середнього (степінь 2 для оптимізації)
#define THRESHOLD_DARK      1200   // Поріг для ВМИКАННЯ світлодіода (нижче цього значення — темно)
#define THRESHOLD_LIGHT     1600   // Поріг для ВИМИКАННЯ світлодіода (вище цього значення — світло)

// Структура для Simple Moving Average 
typedef struct {
    int32_t buffer[SMA_WINDOW_SIZE];
    uint8_t index;
    int32_t sum;
} sma_filter_t;

// Ініціалізація фільтра
void sma_init(sma_filter_t *filter) {
    for (int i = 0; i < SMA_WINDOW_SIZE; i++) {
        filter->buffer[i] = 0;
    }
    filter->index = 0;
    filter->sum = 0;
}

// Додавання значення та отримання відфільтрованого результату
int32_t sma_update(sma_filter_t *filter, int32_t new_val) {
    filter->sum -= filter->buffer[filter->index];
    filter->buffer[filter->index] = new_val;
    filter->sum += new_val;
    
    filter->index = (filter->index + 1) % SMA_WINDOW_SIZE;
    
    return filter->sum / SMA_WINDOW_SIZE; 
    // Оптимізація компілятора: якщо SMA_WINDOW_SIZE є степенем 2, ділення заміниться на зсув (>>).
}

void app_main(void)
{
    //  1. Ініціалізація GPIO для Світлодіода 
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << LED_PIN);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);
    
    // Початковий стан — вимкнено
    bool led_state = false;
    gpio_set_level(LED_PIN, led_state);

    //  2. Ініціалізація ADC1 у режимі Oneshot 
    adc_oneshot_unit_handle_t adc1_handle;
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

    // Налаштування каналу (12 біт роздільна здатність, атенюація 11dB для повного діапазону ~3.3V)
    adc_oneshot_chan_cfg_t config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC1_LDR_CHANNEL, &config));

    //  3. Ініціалізація SMA Фільтра 
    sma_filter_t ldr_filter;
    sma_init(&ldr_filter);

    ESP_LOGI(TAG, "Система запущена. Початок моніторингу.");

    while (1) {
        int raw_adc_val = 0;
        // Зчитування Oneshot
        esp_err_t r = adc_oneshot_read(adc1_handle, ADC1_LDR_CHANNEL, &raw_adc_val);
        
        if (r == ESP_OK) {
            // Фільтрація
            int32_t filtered_val = sma_update(&ldr_filter, raw_adc_val);

            // Логіка керування з гістерезисом
            if (led_state == false && filtered_val < THRESHOLD_DARK) {
                led_state = true;
                gpio_set_level(LED_PIN, led_state);
                ESP_LOGI(TAG, "Темно (%ld). Вмикаємо світлодіод.", filtered_val);
            } 
            else if (led_state == true && filtered_val > THRESHOLD_LIGHT) {
                led_state = false;
                gpio_set_level(LED_PIN, led_state);
                ESP_LOGI(TAG, "Світло (%ld). Вимикаємо світлодіод.", filtered_val);
            }

            // Налагоджувальний вивід у термінал
            ESP_LOGD(TAG, "Raw: %d | Filtered: %ld | LED: %s", raw_adc_val, filtered_val, led_state ? "ON" : "OFF");
        } else {
            ESP_LOGE(TAG, "Помилка зчитування ADC.");
        }

        // Дискретизація кожні 50 мс
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    // Очищення ресурсів (у разі виходу з циклу)
    ESP_ERROR_CHECK(adc_oneshot_del_unit(adc1_handle));
}
