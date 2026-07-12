idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES i2c_bus ssd1306 ds1307 ds18b20
)
