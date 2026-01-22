
idf_component_register(
    SRCS "display.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_gpio u8g2_hal_esp32
)

