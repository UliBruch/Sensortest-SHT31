FILE(GLOB app_sources "*.c")

idf_component_register(
    SRCS ${app_sources}
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_wifi esp_netif esp_event nvs_flash mqtt esp_timer esp-tls mbedtls
                  esp_driver_gpio esp_driver_i2c
)
