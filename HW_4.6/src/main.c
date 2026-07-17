/*
 * ESP32-S3 "console oscilloscope"
 * -------------------------------
 * 1. Samples an analog signal continuously using the ADC's built-in
 *    DMA engine (adc_continuous driver — the ADC writes conversion
 *    results straight into RAM via GDMA, no CPU involvement per sample).
 * 2. Converts raw counts to volts (with factory calibration if available).
 * 3. Keeps a running Vmin / Vmax / Vpp / Vavg over each measurement window.
 * 4. Pushes the formatted line out over UART using the driver's
 *    interrupt/ring-buffer TX path (uart_write_bytes), which is the
 *    supported way to do buffered, non-blocking UART output on ESP-IDF.
 *
 * Note on "UART DMA": the plain UART peripheral on ESP32-S3 has a small
 * hardware FIFO and is driven by the IDF UART driver via interrupts +
 * a ring buffer — this is what uart_driver_install()/uart_write_bytes()
 * give you, and it's what almost all ESP-IDF UART-DMA coursework refers
 * to in practice. True memory-to-peripheral DMA for UART exists only via
 * the low level UHCI2 controller, which is not exposed through a stable
 * public IDF API, so it is intentionally not used here.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_err.h"

#include "esp_adc/adc_continuous.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#include "driver/uart.h"

static const char *TAG = "OSCILLO";

/* ---------------- UART configuration ---------------- */
#define UART_PORT       UART_NUM_0
#define UART_BAUD       921600
#define UART_TX_BUF     2048
#define UART_RX_BUF     0          /* we only transmit */

/* ---------------- ADC configuration ---------------- */
#define ADC_UNIT_USED       ADC_UNIT_1
#define ADC_CHANNEL_USED    ADC_CHANNEL_4      /* GPIO5 on ESP32-S3 */
#define ADC_ATTEN_USED      ADC_ATTEN_DB_12    /* ~0 - 3.3 V input range */
#define ADC_BITWIDTH_USED   ADC_BITWIDTH_12

#define ADC_SAMPLE_FREQ_HZ  (20 * 1000)        /* 20 kSPS */
#define ADC_FRAME_SAMPLES   256
#define ADC_FRAME_SIZE      (ADC_FRAME_SAMPLES * SOC_ADC_DIGI_RESULT_BYTES)
#define ADC_POOL_FRAMES     4

/* Print a fresh Vmin/Vmax/Vpp/Vavg line this many samples */
#define REPORT_EVERY_N_SAMPLES  (ADC_SAMPLE_FREQ_HZ / 10)   /* ~10 Hz updates */

static TaskHandle_t s_oscillo_task = NULL;
static adc_continuous_handle_t s_adc_handle = NULL;
static adc_cali_handle_t s_cali_handle = NULL;
static bool s_cali_enabled = false;

/* Called from the ADC ISR each time a DMA conversion frame is ready */
static bool IRAM_ATTR adc_conv_done_cb(adc_continuous_handle_t handle,
                                        const adc_continuous_evt_data_t *edata,
                                        void *user_data)
{
    BaseType_t higher_prio_task_woken = pdFALSE;
    vTaskNotifyGiveFromISR(s_oscillo_task, &higher_prio_task_woken);
    return higher_prio_task_woken == pdTRUE;
}

static bool adc_calibration_init(adc_unit_t unit, adc_channel_t channel,
                                  adc_atten_t atten, adc_cali_handle_t *out_handle)
{
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id  = unit,
        .chan     = channel,
        .atten    = atten,
        .bitwidth = ADC_BITWIDTH_USED,
    };
    esp_err_t err = adc_cali_create_scheme_curve_fitting(&cali_cfg, out_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ADC calibration not available (err=0x%x), falling back to raw scaling", err);
        return false;
    }
    return true;
}

static void uart_init_dma_style(void)
{
    uart_config_t cfg = {
        .baud_rate = UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    /* A non-zero TX buffer makes uart_write_bytes() non-blocking: the
     * driver's ISR drains the ring buffer into the hardware FIFO in the
     * background, so the calling task is not stalled waiting on the wire. */
    ESP_ERROR_CHECK(uart_driver_install(UART_PORT, UART_RX_BUF, UART_TX_BUF, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_PORT, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE,
                                  UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
}

static void adc_init_continuous_dma(void)
{
    adc_continuous_handle_cfg_t handle_cfg = {
        .max_store_buf_size = ADC_FRAME_SIZE * ADC_POOL_FRAMES,
        .conv_frame_size    = ADC_FRAME_SIZE,
    };
    ESP_ERROR_CHECK(adc_continuous_new_handle(&handle_cfg, &s_adc_handle));

    adc_digi_pattern_config_t pattern = {
        .atten     = ADC_ATTEN_USED,
        .channel   = ADC_CHANNEL_USED,
        .unit      = ADC_UNIT_USED,
        .bit_width = ADC_BITWIDTH_USED,
    };

    adc_continuous_config_t dig_cfg = {
        .sample_freq_hz = ADC_SAMPLE_FREQ_HZ,
        .conv_mode      = ADC_CONV_SINGLE_UNIT_1,
        .format         = ADC_DIGI_OUTPUT_FORMAT_TYPE2,
        .pattern_num    = 1,
        .adc_pattern    = &pattern,
    };
    ESP_ERROR_CHECK(adc_continuous_config(s_adc_handle, &dig_cfg));

    adc_continuous_evt_cbs_t cbs = {
        .on_conv_done = adc_conv_done_cb,
    };
    ESP_ERROR_CHECK(adc_continuous_register_event_callbacks(s_adc_handle, &cbs, NULL));

    s_cali_enabled = adc_calibration_init(ADC_UNIT_USED, ADC_CHANNEL_USED, ADC_ATTEN_USED, &s_cali_handle);
}

static inline float raw_to_volts(uint32_t raw)
{
    if (s_cali_enabled) {
        int mv = 0;
        adc_cali_raw_to_voltage(s_cali_handle, (int)raw, &mv);
        return mv / 1000.0f;
    }
    /* Rough fallback if no calibration eFuse data is present on this chip */
    return (raw / 4095.0f) * 3.3f;
}

static void oscillo_task(void *arg)
{
    uint8_t *dma_buf = malloc(ADC_FRAME_SIZE);
    if (dma_buf == NULL) {
        ESP_LOGE(TAG, "Out of memory allocating DMA read buffer");
        vTaskDelete(NULL);
        return;
    }

    ESP_ERROR_CHECK(adc_continuous_start(s_adc_handle));
    ESP_LOGI(TAG, "ADC continuous (DMA) sampling started at %d Hz", ADC_SAMPLE_FREQ_HZ);

    float v_min = 1e9f, v_max = -1e9f, v_sum = 0.0f;
    uint32_t n = 0;
    char line[96];

    while (1) {
        /* Sleep until the ADC DMA ISR signals a completed frame */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        uint32_t bytes_read = 0;
        esp_err_t err;

        /* Drain all frames currently available in the DMA pool */
        while ((err = adc_continuous_read(s_adc_handle, dma_buf, ADC_FRAME_SIZE, &bytes_read, 0)) == ESP_OK) {
            for (uint32_t i = 0; i + SOC_ADC_DIGI_RESULT_BYTES <= bytes_read; i += SOC_ADC_DIGI_RESULT_BYTES) {
                adc_digi_output_data_t *sample = (adc_digi_output_data_t *)&dma_buf[i];

                if (sample->type2.channel != ADC_CHANNEL_USED) {
                    continue;   /* ignore anything not from our channel */
                }

                float v = raw_to_volts(sample->type2.data);

                if (v < v_min) v_min = v;
                if (v > v_max) v_max = v;
                v_sum += v;
                n++;
            }

            if (n >= REPORT_EVERY_N_SAMPLES) {
                float v_avg = v_sum / (float)n;
                float v_pp  = v_max - v_min;

                int len = snprintf(line, sizeof(line),
                    "Vmin=%.3fV  Vmax=%.3fV  Vpp=%.3fV  Vavg=%.3fV  (n=%lu)\r\n",
                    v_min, v_max, v_pp, v_avg, (unsigned long)n);

                uart_write_bytes(UART_PORT, line, len);

                v_min = 1e9f;
                v_max = -1e9f;
                v_sum = 0.0f;
                n = 0;
            }
        }

        if (err != ESP_ERR_TIMEOUT) {
            /* ESP_ERR_TIMEOUT just means "no more data right now" — anything
             * else is unexpected, so log it for visibility. */
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "adc_continuous_read returned 0x%x", err);
            }
        }
    }
}

void app_main(void)
{
    uart_init_dma_style();
    adc_init_continuous_dma();

    /* Capture the task handle synchronously so the ADC ISR callback
     * (registered above, but not yet firing since sampling hasn't
     * started) always has a valid target to notify. */
    xTaskCreate(oscillo_task, "oscillo_task", 4096, NULL, 5, &s_oscillo_task);
}