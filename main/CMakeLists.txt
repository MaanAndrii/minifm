idf_component_register(SRCS "app_main.c" "display.c" "power.c" "motion.c" "settings.c" "controller.c" "audio.c" "network.c" "web.c" "ui.c" "ui_editor.c" "rtc.c" "core.c" "font.c" "clock_font.c" "large_clock_font.c" "station_font.c"
    INCLUDE_DIRS "." "vendor"
    REQUIRES esp_wifi esp_event esp_netif esp_http_server esp_http_client nvs_flash esp_timer fatfs json esp_driver_i2c mbedtls
    EMBED_TXTFILES "web/index.html")
target_compile_options(${COMPONENT_LIB} PRIVATE -fstack-usage)
