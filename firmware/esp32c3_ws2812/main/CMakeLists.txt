idf_component_register(
    SRCS "ws2812_scheme_b.c"
    INCLUDE_DIRS "."
    REQUIRES led_strip spi_flash nvs_flash esp_http_server esp_wifi esp_event lwip esp_netif mdns bt
)
