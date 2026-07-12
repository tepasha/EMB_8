#include "display.h"
#include <stdio.h>
#include <string.h>
#include "u8g2.h"
#include "u8g2_esp32_hal.h"

#define OLED_SDA        8
#define OLED_SCL        9
#define OLED_I2C_ADDR   0x3C    /* 7-bit; u8x8_SetI2CAddress wants (addr<<1) */
#define OLED_RESET      U8G2_ESP32_HAL_UNDEFINED

static u8g2_t u8g2;

static const char *const DOW_NAME[] = {
    "???", "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};

static void draw_progress_bar(int x, int y, int w, int h,
                               int value, int max_val)
{
    u8g2_DrawFrame(&u8g2, x, y, w, h);
    int fill = 0;
    if (max_val > 0 && value > 0) {
        fill = (int)((long)value * (w - 2) / max_val);
        if (fill > w - 2) fill = w - 2;
    }
    if (fill > 0) u8g2_DrawBox(&u8g2, x + 1, y + 1, fill, h - 2);
}

void display_init(i2c_master_bus_handle_t bus_handle)
{
    (void)bus_handle;

    // 1. Configure the HAL with our pin numbers
    u8g2_esp32_hal_t hal = U8G2_ESP32_HAL_DEFAULT;
    hal.bus.i2c.sda = OLED_SDA;
    hal.bus.i2c.scl = OLED_SCL;
    u8g2_esp32_hal_init(hal);

    // 2. Choose display + rotation + full-frame-buffer mode
    u8g2_Setup_ssd1306_i2c_128x64_noname_f(
        &u8g2,
        U8G2_R0,
        u8g2_esp32_i2c_byte_cb,
        u8g2_esp32_gpio_and_delay_cb);

    // 3. Set I2C address (shifted left — U8g2 convention)
    u8x8_SetI2CAddress(&u8g2.u8x8, OLED_I2C_ADDR << 1);

    // 4. Power on
    u8g2_InitDisplay(&u8g2);
    u8g2_SetPowerSave(&u8g2, 0);
    u8g2_SetContrast(&u8g2, 220);

    // 5. Splash
    u8g2_ClearBuffer(&u8g2);
    u8g2_SetFont(&u8g2, u8g2_font_6x10_tr);
    u8g2_DrawStr(&u8g2, 14, 36, "Clock starting...");
    u8g2_SendBuffer(&u8g2);
}

void display_show_error(const char *msg)
{
    u8g2_ClearBuffer(&u8g2);
    u8g2_SetFont(&u8g2, u8g2_font_6x10_tr);
    u8g2_DrawStr(&u8g2, 4, 32, msg ? msg : "Error");
    u8g2_SendBuffer(&u8g2);
}

void display_update(const ds1307_time_t *t, float temp_c, bool colon_on)
{
    char buf[32];

    u8g2_ClearBuffer(&u8g2);

    //Row 1: Day-of-week + Date
    u8g2_SetFont(&u8g2, u8g2_font_5x8_tr);
    const char *dow_s = (t->dow >= 1 && t->dow <= 7)
                        ? DOW_NAME[t->dow] : "???";
    snprintf(buf, sizeof(buf), "%s %02d.%02d.%04d",
             dow_s, t->day, t->month, t->year);
    u8g2_DrawStr(&u8g2, 0, 8, buf);

    // Thin separator
    u8g2_DrawHLine(&u8g2, 0, 10, 128);

    // Row 2: HH:MM large bold + blinking colon + SS
    u8g2_SetFont(&u8g2, u8g2_font_logisoso20_tn);

    snprintf(buf, sizeof(buf), "%02d", t->hour);
    u8g2_DrawStr(&u8g2, 0, 34, buf);

    if (colon_on) {
        u8g2_DrawStr(&u8g2, 26, 34, ":");
    }

    snprintf(buf, sizeof(buf), "%02d", t->minute);
    u8g2_DrawStr(&u8g2, 33, 34, buf);

    // Seconds — slightly smaller, top-right
    u8g2_SetFont(&u8g2, u8g2_font_logisoso16_tn);
    snprintf(buf, sizeof(buf), "%02d", t->second);
    u8g2_DrawStr(&u8g2, 104, 32, buf);

    // Row 3: Seconds progress bar (full width)
    draw_progress_bar(0, 36, 128, 5, (int)t->second, 59);

    // Row 4: Minutes bar (left) + Hours bar (right)
    u8g2_SetFont(&u8g2, u8g2_font_4x6_tr);
    u8g2_DrawStr(&u8g2, 0, 55, "min");
    draw_progress_bar(13, 44, 54, 6, (int)t->minute, 59);

    u8g2_DrawStr(&u8g2, 70, 55, "hr");
    draw_progress_bar(80, 44, 48, 6, (int)(t->hour % 12), 11);

    // Row 5: Temperature
    u8g2_SetFont(&u8g2, u8g2_font_5x8_tr);
    if (temp_c <= -100.0f) {
        u8g2_DrawStr(&u8g2, 0, 63, "Temp: --.-" "\xb0" "C");
    } else {
        int ti = (int)temp_c;
        int tf = (int)((temp_c - (float)ti) * 10.0f);
        if (tf < 0) tf = -tf;
        snprintf(buf, sizeof(buf), "Temp: %d.%d" "\xb0" "C", ti, tf);
        u8g2_DrawStr(&u8g2, 0, 63, buf);
    }

    u8g2_SendBuffer(&u8g2);
}
