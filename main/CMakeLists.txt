idf_component_register(
    SRCS
        "enerlytix_main.c"
        "wifi/wifi.c"
        "mqtt/mqtt.c"

    INCLUDE_DIRS
        "."

    REQUIRES
        mqtt
        esp_wifi
        esp_event
        esp_netif
        nvs_flash
        esp_driver_gpio
        esp_timer
)