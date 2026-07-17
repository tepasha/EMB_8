#include "bme280_spi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "bme280_spi";

#define BME280_REG_CHIP_ID   0xD0
#define BME280_REG_RESET     0xE0
#define BME280_REG_CTRL_HUM  0xF2
#define BME280_REG_CTRL_MEAS 0xF4
#define BME280_REG_CONFIG    0xF5
#define BME280_REG_DATA      0xF7  // press_msb ... hum_lsb (8 байт)
#define BME280_REG_CALIB_1   0x88  // 26 байт: dig_T1..dig_P9, dig_H1
#define BME280_REG_CALIB_2   0xE1  // 7 байт: dig_H2..dig_H6

#define BME280_CHIP_ID_VAL   0x60

// ---------- Низькорівневий обмін через SPI ----------
// BME280 SPI-протокол: старший біт адреси = 1 -> читання, 0 -> запис.

static esp_err_t reg_write(bme280_dev_t *dev, uint8_t reg, uint8_t value)
{
    uint8_t tx[2] = { (uint8_t)(reg & 0x7F), value };
    spi_transaction_t t = {
        .length = 16,
        .tx_buffer = tx,
    };
    return spi_device_transmit(dev->spi, &t);
}

static esp_err_t reg_read(bme280_dev_t *dev, uint8_t reg, uint8_t *buf, size_t len)
{
    uint8_t tx[40] = {0};
    uint8_t rx[40] = {0};
    if (len + 1 > sizeof(tx)) {
        return ESP_ERR_INVALID_SIZE;
    }
    tx[0] = (uint8_t)(reg | 0x80);

    spi_transaction_t t = {
        .length = (len + 1) * 8,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    esp_err_t err = spi_device_transmit(dev->spi, &t);
    if (err == ESP_OK) {
        memcpy(buf, rx + 1, len);
    }
    return err;
}

// ---------- Ініціалізація ----------

esp_err_t bme280_spi_init(bme280_dev_t *dev, gpio_num_t mosi, gpio_num_t miso,
                           gpio_num_t sclk, gpio_num_t cs)
{
    memset(dev, 0, sizeof(*dev));

    spi_bus_config_t buscfg = {
        .mosi_io_num = mosi,
        .miso_io_num = miso,
        .sclk_io_num = sclk,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 40,
    };
    esp_err_t err = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "spi_bus_initialize: %s", esp_err_to_name(err));
        return err;
    }

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 1 * 1000 * 1000, // 1 MHz, з запасом (макс. для BME280 - 10 МГц)
        .mode = 0,
        .spics_io_num = cs,
        .queue_size = 1,
    };
    err = spi_bus_add_device(SPI2_HOST, &devcfg, &dev->spi);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_add_device: %s", esp_err_to_name(err));
        return err;
    }

    uint8_t chip_id = 0;
    reg_read(dev, BME280_REG_CHIP_ID, &chip_id, 1);
    ESP_LOGI(TAG, "CHIP_ID = 0x%02X (очікується 0x60 для BME280)", chip_id);
    if (chip_id != BME280_CHIP_ID_VAL) {
        ESP_LOGW(TAG, "Незвичний chip id - перевірте підключення SPI (MOSI/MISO/SCLK/CS)");
    }

    // Програмний скидання
    reg_write(dev, BME280_REG_RESET, 0xB6);
    vTaskDelay(pdMS_TO_TICKS(10)); // одноразова пауза при старті, не пов'язана з циклом опитування 1 Гц

    // Зчитування калібрувальних коефіцієнтів
    uint8_t c1[26] = {0};
    reg_read(dev, BME280_REG_CALIB_1, c1, sizeof(c1));

    dev->dig_T1 = (uint16_t)(c1[1] << 8 | c1[0]);
    dev->dig_T2 = (int16_t)(c1[3] << 8 | c1[2]);
    dev->dig_T3 = (int16_t)(c1[5] << 8 | c1[4]);

    dev->dig_P1 = (uint16_t)(c1[7] << 8 | c1[6]);
    dev->dig_P2 = (int16_t)(c1[9] << 8 | c1[8]);
    dev->dig_P3 = (int16_t)(c1[11] << 8 | c1[10]);
    dev->dig_P4 = (int16_t)(c1[13] << 8 | c1[12]);
    dev->dig_P5 = (int16_t)(c1[15] << 8 | c1[14]);
    dev->dig_P6 = (int16_t)(c1[17] << 8 | c1[16]);
    dev->dig_P7 = (int16_t)(c1[19] << 8 | c1[18]);
    dev->dig_P8 = (int16_t)(c1[21] << 8 | c1[20]);
    dev->dig_P9 = (int16_t)(c1[23] << 8 | c1[22]);

    dev->dig_H1 = c1[25]; // регістр 0xA1

    uint8_t c2[7] = {0};
    reg_read(dev, BME280_REG_CALIB_2, c2, sizeof(c2));
    dev->dig_H2 = (int16_t)(c2[1] << 8 | c2[0]);
    dev->dig_H3 = c2[2];
    dev->dig_H4 = (int16_t)((c2[3] << 4) | (c2[4] & 0x0F));
    dev->dig_H5 = (int16_t)((c2[5] << 4) | (c2[4] >> 4));
    dev->dig_H6 = (int8_t)c2[6];

    // ctrl_hum ПОВИНЕН бути записаний до ctrl_meas (вимога datasheet)
    reg_write(dev, BME280_REG_CTRL_HUM, 0x01);   // humidity oversampling x1

    // osrs_t=001, osrs_p=001, mode=11 (NORMAL) -> безперервні виміри у фоні
    reg_write(dev, BME280_REG_CTRL_MEAS, 0x27);

    // t_sb = 000 (0.5 мс), filter = off, spi3w_en = 0
    reg_write(dev, BME280_REG_CONFIG, 0x00);

    ESP_LOGI(TAG, "BME280 ініціалізовано (SPI, NORMAL mode)");
    return ESP_OK;
}

// ---------- Формули компенсації (Bosch, з datasheet BME280, double-варіант) ----------

static double compensate_temperature(bme280_dev_t *dev, int32_t adc_T)
{
    double var1, var2, T;
    var1 = (((double)adc_T) / 16384.0 - ((double)dev->dig_T1) / 1024.0) * ((double)dev->dig_T2);
    var2 = ((((double)adc_T) / 131072.0 - ((double)dev->dig_T1) / 8192.0) *
            (((double)adc_T) / 131072.0 - ((double)dev->dig_T1) / 8192.0)) * ((double)dev->dig_T3);
    dev->t_fine = var1 + var2;
    T = (var1 + var2) / 5120.0;
    return T;
}

static double compensate_pressure(bme280_dev_t *dev, int32_t adc_P)
{
    double var1, var2, p;
    var1 = ((double)dev->t_fine / 2.0) - 64000.0;
    var2 = var1 * var1 * ((double)dev->dig_P6) / 32768.0;
    var2 = var2 + var1 * ((double)dev->dig_P5) * 2.0;
    var2 = (var2 / 4.0) + (((double)dev->dig_P4) * 65536.0);
    var1 = (((double)dev->dig_P3) * var1 * var1 / 524288.0 + ((double)dev->dig_P2) * var1) / 524288.0;
    var1 = (1.0 + var1 / 32768.0) * ((double)dev->dig_P1);
    if (var1 == 0.0) {
        return 0; // уникаємо ділення на нуль
    }
    p = 1048576.0 - (double)adc_P;
    p = (p - (var2 / 4096.0)) * 6250.0 / var1;
    var1 = ((double)dev->dig_P9) * p * p / 2147483648.0;
    var2 = p * ((double)dev->dig_P8) / 32768.0;
    p = p + (var1 + var2 + ((double)dev->dig_P7)) / 16.0;
    return p; // Па
}

static double compensate_humidity(bme280_dev_t *dev, int32_t adc_H)
{
    double var_h;
    var_h = (((double)dev->t_fine) - 76800.0);
    var_h = (adc_H - (((double)dev->dig_H4) * 64.0 + ((double)dev->dig_H5) / 16384.0 * var_h)) *
            (((double)dev->dig_H2) / 65536.0 * (1.0 + ((double)dev->dig_H6) / 67108864.0 * var_h *
            (1.0 + ((double)dev->dig_H3) / 67108864.0 * var_h)));
    var_h = var_h * (1.0 - ((double)dev->dig_H1) * var_h / 524288.0);
    if (var_h > 100.0) var_h = 100.0;
    if (var_h < 0.0) var_h = 0.0;
    return var_h; // %RH
}

esp_err_t bme280_read(bme280_dev_t *dev, float *temperature_c, float *pressure_pa, float *humidity_rh)
{
    uint8_t data[8] = {0};
    esp_err_t err = reg_read(dev, BME280_REG_DATA, data, sizeof(data));
    if (err != ESP_OK) {
        return err;
    }

    int32_t adc_P = ((int32_t)data[0] << 12) | ((int32_t)data[1] << 4) | (data[2] >> 4);
    int32_t adc_T = ((int32_t)data[3] << 12) | ((int32_t)data[4] << 4) | (data[5] >> 4);
    int32_t adc_H = ((int32_t)data[6] << 8)  | data[7];

    double t = compensate_temperature(dev, adc_T); // потрібно рахувати першою (задає t_fine)
    double p = compensate_pressure(dev, adc_P);
    double h = compensate_humidity(dev, adc_H);

    *temperature_c = (float)t;
    *pressure_pa = (float)p;
    *humidity_rh = (float)h;
    return ESP_OK;
}
