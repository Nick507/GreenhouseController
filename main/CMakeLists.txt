idf_component_register(SRCS "telemetry.c" "wifi_net.c" "sleep_prep.c" "battery.c" "ds18b20.c" "valve.c" "soilMoisture.c" "powerCtrl.c" "bmp280.c" "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES driver esp_adc esp_driver_i2c esp_event esp_netif esp_timer esp_wifi nvs_flash)
