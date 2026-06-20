#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

// Конфіг 
#define ADC_CHANNEL     ADC_CHANNEL_0   // GPIO1 на ESP32-S3
#define ADC_ATTEN       ADC_ATTEN_DB_12 // 0–3.1V діапазон
#define ADC_BITWIDTH    ADC_BITWIDTH_12 // 12 біт → 0–4095
#define ADC_REF_MV      3100            // Vref при DB_12 (~3.1V)
#define SAMPLE_COUNT    20              // кількість вимірювань
#define SAMPLE_DELAY_MS 100             // затримка між вимірами

static const char *TAG = "ADC";

// Таблиця 
// Заголовок + роздільник
static void print_header(void) {
  printf("\n");
  printf("%-6s  %-14s  %-12s  %-10s  %-8s  %-10s\n",
         "RAW", "U_cali(mV)", "Error(%)",
         "Atten", "Vref(mV)");
  printf("%-6s  %-14s  %-12s  %-10s  %-8s  %-10s\n",
         "------", "------------", "----------",
         "--------", "----------");
}

static void print_row(int raw, int u_cali, float error) {
  printf("%-6d  %-14.1f  %-12d  %-10.2f  %-8s  %-10d\n",
         raw, u_cali, error, "12 dB", ADC_REF_MV);
}

void app_main(void) {

  // Ініціалізація ADC oneshot 
  adc_oneshot_unit_handle_t adc_handle;
  adc_oneshot_unit_init_cfg_t init_cfg = {
    .unit_id  = ADC_UNIT_1,
    .ulp_mode = ADC_ULP_MODE_DISABLE,
  };
  ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_cfg, &adc_handle));

  adc_oneshot_chan_cfg_t chan_cfg = {
    .atten    = ADC_ATTEN,
    .bitwidth = ADC_BITWIDTH,
  };
  ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, ADC_CHANNEL, &chan_cfg));

  // Ініціалізація калібрування 
  adc_cali_handle_t cali_handle = NULL;
  bool cali_ok = false;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
  adc_cali_curve_fitting_config_t cali_cfg = {
    .unit_id  = ADC_UNIT_1,
    .chan     = ADC_CHANNEL,
    .atten   = ADC_ATTEN,
    .bitwidth = ADC_BITWIDTH,
  };
  if (adc_cali_create_scheme_curve_fitting(&cali_cfg, &cali_handle) == ESP_OK) {
    cali_ok = true;
    ESP_LOGI(TAG, "Calibration: curve fitting");
  }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
  if (!cali_ok) {
    adc_cali_line_fitting_config_t cali_cfg = {
      .unit_id   = ADC_UNIT_1,
      .atten     = ADC_ATTEN,
      .bitwidth  = ADC_BITWIDTH,
    };
    if (adc_cali_create_scheme_line_fitting(&cali_cfg, &cali_handle) == ESP_OK) {
      cali_ok = true;
      ESP_LOGI(TAG, "Calibration: line fitting");
    }
  }
#endif

  // Інформація про конфігурацію 
  printf("\n=== ADC Configuration ===\n");
  printf("Channel   : ADC1 CH%d (GPIO%d)\n", ADC_CHANNEL, ADC_CHANNEL + 1);
  printf("Attenuation: 12 dB  (0 – %d mV)\n", ADC_REF_MV);
  printf("Resolution : 12 bit (0 – 4095)\n");
  printf("Vref       : %d mV\n", ADC_REF_MV);
  printf("Formula    : U = RAW × %d / 4095\n", ADC_REF_MV);
  printf("Calibration: %s\n\n", cali_ok ? "YES" : "NO");

  // Вимірювання 
  print_header();

  float    sum_error  = 0;
  int      valid_rows = 0;
  int      prev_raw   = -1;   // захист від дублікатів

  for (int i = 0; i < SAMPLE_COUNT; i++) {
    int raw = 0;
    ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, ADC_CHANNEL, &raw));

    // Каліброване значення
    if (cali_ok) {
      adc_cali_raw_to_voltage(cali_handle, raw, &u_cali);
    }

    // Виводимо лише якщо RAW змінився (унікальні точки)
    if (raw != prev_raw) {
      print_row(raw, u_cali, error);
      sum_error += error;
      valid_rows++;
      prev_raw = raw;
    }

    vTaskDelay(pdMS_TO_TICKS(SAMPLE_DELAY_MS));
  }

  // Підсумок
  printf("%-6s  %-14s  %-12s  %-10s\n",
         "------", "--------------", "------------", "----------");
  if (valid_rows > 0) {
    printf("Avg error: %.2f%%   Samples: %d\n",
           sum_error / valid_rows, valid_rows);
  }

  // Звільнення ресурсів
  if (cali_ok) {
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_delete_scheme_curve_fitting(cali_handle);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_delete_scheme_line_fitting(cali_handle);
#endif
  }
  adc_oneshot_del_unit(adc_handle);
}
