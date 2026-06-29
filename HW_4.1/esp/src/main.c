#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"

static const char *TAG = "ESP32_BTN_LED";

// Налаштування пінів
#define BUTTON_GPIO     GPIO_NUM_0      // кнопка на ESP32
#define LED_GPIO        GPIO_NUM_5      // світлодіод на ESP32

#define UART_PORT       UART_NUM_1
#define UART_TX_GPIO    GPIO_NUM_17     // -> RX на STM32
#define UART_RX_GPIO    GPIO_NUM_18     // <- TX зі STM32
#define UART_BAUD_RATE  115200
#define UART_BUF_SIZE   256

// Байт-подія, який шлемо при натисканні кнопки.
#define BTN_EVENT_BYTE  0xA5

#define DEBOUNCE_MS     30

static void uart_init(void)
{
    uart_config_t uart_config = {
        .baud_rate  = UART_BAUD_RATE,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_driver_install(UART_PORT, UART_BUF_SIZE, UART_BUF_SIZE, 0, NULL, 0);
    uart_param_config(UART_PORT, &uart_config);
    uart_set_pin(UART_PORT, UART_TX_GPIO, UART_RX_GPIO,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

static void gpio_init(void)
{
    gpio_config_t led_cfg = {
        .pin_bit_mask = 1ULL << LED_GPIO,
        .mode         = GPIO_MODE_OUTPUT,
    };
    gpio_config(&led_cfg);
    gpio_set_level(LED_GPIO, 0);

    gpio_config_t btn_cfg = {
        .pin_bit_mask  = 1ULL << BUTTON_GPIO,
        .mode          = GPIO_MODE_INPUT,
        .pull_up_en    = GPIO_PULLUP_ENABLE,
        .pull_down_en  = GPIO_PULLDOWN_DISABLE,
    };
    gpio_config(&btn_cfg);
}

// Задача опитування кнопки з програмним антидребезгом.
static void button_task(void *arg)
{
    int last_state = 1; // 1 = не натиснута (через pull-up)

    while (1) {
        int level = gpio_get_level(BUTTON_GPIO);

        if (level != last_state) {
            vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_MS));
            level = gpio_get_level(BUTTON_GPIO);

            if (level != last_state) {
                last_state = level;

                if (level == 0) { // натиснута (активний LOW)
                    uint8_t b = BTN_EVENT_BYTE;
                    uart_write_bytes(UART_PORT, (const char *)&b, 1);
                    ESP_LOGI(TAG, "Кнопка натиснута -> подію надіслано на STM32");
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// Задача прийому даних по UART від STM32
static void uart_rx_task(void *arg)
{
    uint8_t data[UART_BUF_SIZE];
    static int led_state = 0;

    while (1) {
        int len = uart_read_bytes(UART_PORT, data, UART_BUF_SIZE, pdMS_TO_TICKS(50));
        for (int i = 0; i < len; i++) {
            if (data[i] == BTN_EVENT_BYTE) {
                led_state = !led_state;
                gpio_set_level(LED_GPIO, led_state);
                ESP_LOGI(TAG, "Подія від STM32 отримана -> LED = %d", led_state);
            }
        }
    }
}

void app_main(void)
{
    gpio_init();
    uart_init();

    xTaskCreate(button_task,  "button_task",  2048, NULL, 10, NULL);
    xTaskCreate(uart_rx_task, "uart_rx_task", 2048, NULL, 10, NULL);
}
