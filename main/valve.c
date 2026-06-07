#include "valve.h"

#include "configuration.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "valve";

static void motor_stop(void)
{
    gpio_set_level(PIN_VALVE_MOTOR, 0);
}

static void motor_run(void)
{
    gpio_set_level(PIN_VALVE_MOTOR, 1);
}

esp_err_t valve_init(void)
{
    gpio_config_t motor = {
        .pin_bit_mask = 1ULL << PIN_VALVE_MOTOR,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&motor));

    gpio_config_t state = {
        .pin_bit_mask = 1ULL << PIN_VALVE_STATE,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&state));

    motor_stop();
    ESP_LOGI(TAG, "motor GPIO%d, feedback GPIO%d", PIN_VALVE_MOTOR, PIN_VALVE_STATE);
    return ESP_OK;
}

bool valve_is_open(void)
{
    return gpio_get_level(PIN_VALVE_STATE) == VALVE_OPEN;
}

esp_err_t valve_set(bool open)
{
    if (valve_is_open() == open) 
    {
        ESP_LOGI(TAG, "already %s", open ? "open" : "closed");
        return ESP_OK;
    }

    motor_run();
    ESP_LOGI(TAG, "moving to %s", open ? "open" : "closed");

    const int64_t deadline_us = esp_timer_get_time() + ((int64_t)VALVE_TIMEOUT_MS * 1000);
    int debounce_count = 0;
    while (esp_timer_get_time() < deadline_us) 
    {
        if (valve_is_open() == open) 
        {
            debounce_count++;
            if (debounce_count > 5) 
            {
                motor_stop();
                ESP_LOGI(TAG, "reached %s", open ? "open" : "closed");
                return ESP_OK;
            }
        }
        else 
        {
            debounce_count = 0;
        }
        //ESP_LOGI(TAG, "valve is open: %d", valve_is_open());
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    motor_stop();
    ESP_LOGW(TAG, "timeout moving to %s (state=%s)",
             open ? "open" : "closed", valve_is_open() ? "open" : "closed");
    return ESP_ERR_TIMEOUT;
}
