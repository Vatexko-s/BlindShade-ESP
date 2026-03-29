/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

#include <esp_err.h>
#include <stdint.h>

typedef void *app_driver_handle_t;

typedef struct {
    uint32_t voltage_mv;
    uint8_t percent;
    bool valid;
} app_battery_status_t;

/** Initialize the window covering dummy motor driver. */
esp_err_t app_driver_init(uint16_t endpoint_id);

/** Handle target position updates (percent100ths). */
void app_driver_set_target_percent100ths(uint16_t endpoint_id, uint16_t target_percent100ths);

/** Stop motion immediately. */
void app_driver_stop(uint16_t endpoint_id);

/** Get latest battery measurement (GPIO0). */
esp_err_t app_driver_get_battery_status(app_battery_status_t *status);

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
#include "esp_openthread_types.h"
#define ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG()                                           \
    {                                                                                   \
        .radio_mode = RADIO_MODE_NATIVE,                                                \
    }

#define ESP_OPENTHREAD_DEFAULT_HOST_CONFIG()                                            \
    {                                                                                   \
        .host_connection_mode = HOST_CONNECTION_MODE_NONE,                              \
    }

#define ESP_OPENTHREAD_DEFAULT_PORT_CONFIG()                                            \
    {                                                                                   \
        .storage_partition_name = "nvs", .netif_queue_size = 10, .task_queue_size = 10, \
    }
#endif
