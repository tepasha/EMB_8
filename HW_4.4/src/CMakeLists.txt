idf_component_register(
    SRCS "main.c"
         "../include/bme280_spi.c"
         "../include/ds18b20.c"
         "../lib/ssd1306.c"
         "../lib/font_5x7.c"
    INCLUDE_DIRS "." "../include" "../lib"
    REQUIRES driver esp_timer freertos
)
