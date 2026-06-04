#include "lights.h"
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static struct traffic_light_t {
    esp_timer_handle_t timer;
    traffic_light_state_t state;
    gpio_num_t red_pin;
    gpio_num_t yellow_pin;
    gpio_num_t green_pin;
    bool blinking;
} traffic_light;

static inline void IRAM_ATTR set_traffic_light(uint8_t r, uint8_t y, uint8_t g) {
    gpio_set_level(traffic_light.red_pin, r);
    gpio_set_level(traffic_light.yellow_pin, y);
    gpio_set_level(traffic_light.green_pin, g);
}

static void IRAM_ATTR traffic_light_timer_callback(void* arg) {
  if(!traffic_light.blinking) {
    switch (traffic_light.state) {
    case STATE_RED:
        set_traffic_light(1, 1, 0);
        traffic_light.state = STATE_RED_YELLOW;
        esp_timer_start_once(traffic_light.timer, TIME_RED_YELLOW * 1000);
        ESP_LOGI("TRAFFIC_LIGHT", "STATE_RED_YELLOW");
        break;
    case STATE_RED_YELLOW:
        set_traffic_light(0, 0, 1);
        traffic_light.state = STATE_GREEN;
        esp_timer_start_once(traffic_light.timer, TIME_GREEN * 1000);
        ESP_LOGI("TRAFFIC_LIGHT", "STATE_GREEN");
        break;
    case STATE_GREEN:
        set_traffic_light(0, 1, 0);
        traffic_light.state = STATE_YELLOW;
        esp_timer_start_once(traffic_light.timer, TIME_YELLOW * 1000);
        ESP_LOGI("TRAFFIC_LIGHT", "STATE_YELLOW");
        break;
    case STATE_YELLOW:
        set_traffic_light(1, 0, 0);
        traffic_light.state = STATE_RED;
        esp_timer_start_once(traffic_light.timer, TIME_RED * 1000);
        ESP_LOGI("TRAFFIC_LIGHT", "STATE_RED");
        break;
    default:
        break;
    }
  } else {
    if (traffic_light.state == STATE_BLINK_ON) {
        set_traffic_light(0, 1, 0);
        traffic_light.state = STATE_BLINK_OFF;
    } else {
        set_traffic_light(0, 0, 0);
        traffic_light.state = STATE_BLINK_ON;
    }
    ESP_LOGI("TRAFFIC_LIGHT", "STATE_BLINKING");
    esp_timer_start_once(traffic_light.timer, TIME_BLINK * 1000);
  }
}

void start_traffic_light(void) {
    esp_timer_start_once(traffic_light.timer, TIME_RED * 1000);
}

void stop_traffic_light(void) {
    esp_timer_stop(traffic_light.timer);
    set_traffic_light(0, 0, 0);
}

void enable_blinking_mode(bool state) {
    if(traffic_light.blinking == state) return;

    traffic_light.blinking = state;

    if (state) {
        traffic_light.state = STATE_BLINK_ON;
    } else {
        traffic_light.state = STATE_RED;
    }
    //TODO: IRQ
    traffic_light_timer_callback(NULL);
}


esp_err_t init_traffic_light(gpio_num_t red, gpio_num_t yellow, gpio_num_t green) {
    traffic_light.red_pin = red;
    traffic_light.yellow_pin = yellow;
    traffic_light.green_pin = green;
    traffic_light.blinking = false;
    traffic_light.state = STATE_RED;

    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << red) | (1ULL << yellow) | (1ULL << green),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE
    };

    if (gpio_config(&io_conf) != ESP_OK) {
        ESP_LOGE("LIGHTS", "Failed to configure traffic light GPIOs!");
        return ESP_FAIL;
    }

    const esp_timer_create_args_t timer_long_press_args = {
        .callback = traffic_light_timer_callback,
        .name = "traffic_light_timer_callback"
    };

    if (esp_timer_create(&timer_long_press_args, &traffic_light.timer) != ESP_OK) {
        ESP_LOGE("LIGHTS", "Failed to create traffic light timer!");
        return ESP_FAIL;
    }

    return ESP_OK;
}
