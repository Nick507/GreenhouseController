#include "battery.h"
#include "bmp280.h"
#include "ds18b20.h"
#include "esp_log.h"
#include "powerCtrl.h"
#include "sleep_prep.h"
#include "soilMoisture.h"
#include "telemetry.h"
#include "valve.h"
#include "wifi_net.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "main";

static void init_sensors(void)
{
    ESP_ERROR_CHECK(battery_init());
    ESP_ERROR_CHECK(soil_moisture_init());
    ESP_ERROR_CHECK(valve_init());

    esp_err_t err = bmp280_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "bmp280 init failed: %s", esp_err_to_name(err));
    }

    err = ds18b20_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ds18b20 init failed: %s", esp_err_to_name(err));
    }
}

static void run_host_exchange(void)
{
    telemetry_snapshot_t snap;
    telemetry_collect(&snap);

    char tx_json[TELEMETRY_JSON_SIZE];
    if (!telemetry_build_json(&snap, tx_json, sizeof(tx_json))) {
        ESP_LOGE(TAG, "telemetry JSON build failed");
        return;
    }

    ESP_LOGI(TAG, "tx: %s", tx_json);
    ESP_ERROR_CHECK(wifi_net_init());

    char rx_json[TELEMETRY_RESPONSE_SIZE];
    size_t rx_len = 0;
    esp_err_t err = wifi_net_exchange(tx_json, rx_json, sizeof(rx_json), &rx_len);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "rx: %s", rx_json);
        telemetry_apply_host_response(rx_json);
    } else {
        ESP_LOGW(TAG, "host exchange failed: %s", esp_err_to_name(err));
    }

    wifi_net_deinit();
}

void app_main(void)
{
    ESP_LOGI(TAG, "greenhouse controller starting");

    sleep_release_i2c_pins();

    ESP_ERROR_CHECK(power_ctrl_init());
    power_ctrl_set(true);

    init_sensors();

    run_host_exchange();

    sleep_enter_deep();
}
