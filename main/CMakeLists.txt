idf_component_register(
    SRCS "main.cpp" "app.cpp" "display.cpp" "face_store.cpp" "text_input.cpp" "settings_store.cpp" "time_manager.cpp" "wifi_manager.cpp" "web_server.cpp" "voice_chat.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_event esp_netif esp_http_client esp_http_server esp-tls nvs_flash mbedtls json M5Unified joltwallet__littlefs
)
