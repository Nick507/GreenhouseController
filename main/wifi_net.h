#pragma once

#include <stddef.h>

#include "esp_err.h"

esp_err_t wifi_net_init(void);
esp_err_t wifi_net_exchange(const char *request, char *response, size_t response_size, size_t *response_len);
void wifi_net_deinit(void);
