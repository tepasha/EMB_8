idf_component_register(
    SRCS
        # header-only: no .c sources
    INCLUDE_DIRS
        "."
    REQUIRES
        driver
        esp_common
)
