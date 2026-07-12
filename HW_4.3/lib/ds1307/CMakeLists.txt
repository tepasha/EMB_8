idf_component_register(
    SRCS "ds1307.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_driver_i2c i2c_bus
)
