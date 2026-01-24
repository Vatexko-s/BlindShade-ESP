/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

#include <esp_log.h>

#define BS_LOG_COLOR_RESET "\x1b[0m"
#define BS_LOG_COLOR_CYAN "\x1b[36m"
#define BS_LOG_COLOR_BLUE "\x1b[34m"
#define BS_LOG_COLOR_YELLOW "\x1b[33m"
#define BS_LOG_COLOR_MAGENTA "\x1b[35m"
#define BS_LOG_COLOR_RED "\x1b[31m"
#define BS_LOG_COLOR_GREEN "\x1b[32m"
#define BS_LOG_COLOR_DIM "\x1b[90m"

#define BS_TAG_APP "APP[WC]"
#define BS_TAG_MOTOR "DRIVER[MOTOR]"
#define BS_TAG_LED "DRV[LED]"
#define BS_TAG_WARN "WARN"
#define BS_TAG_ERROR "ERROR"
#define BS_TAG_STATE "OK/STATE"
#define BS_TAG_TICK "TICK/PROGRESS"

#define BS_LOG_APP(fmt, ...) ESP_LOGI(BS_TAG_APP, BS_LOG_COLOR_CYAN fmt BS_LOG_COLOR_RESET, ##__VA_ARGS__)
#define BS_LOG_MOTOR(fmt, ...) ESP_LOGI(BS_TAG_MOTOR, BS_LOG_COLOR_BLUE fmt BS_LOG_COLOR_RESET, ##__VA_ARGS__)
#define BS_LOG_LED(fmt, ...) ESP_LOGI(BS_TAG_LED, BS_LOG_COLOR_MAGENTA fmt BS_LOG_COLOR_RESET, ##__VA_ARGS__)

#define BS_LOG_WARN(fmt, ...) ESP_LOGW(BS_TAG_WARN, BS_LOG_COLOR_YELLOW fmt BS_LOG_COLOR_RESET, ##__VA_ARGS__)
#define BS_LOG_ERROR(fmt, ...) ESP_LOGE(BS_TAG_ERROR, BS_LOG_COLOR_RED fmt BS_LOG_COLOR_RESET, ##__VA_ARGS__)
#define BS_LOG_STATE(fmt, ...) ESP_LOGI(BS_TAG_STATE, BS_LOG_COLOR_GREEN fmt BS_LOG_COLOR_RESET, ##__VA_ARGS__)
#define BS_LOG_TICK(fmt, ...) ESP_LOGI(BS_TAG_TICK, BS_LOG_COLOR_DIM fmt BS_LOG_COLOR_RESET, ##__VA_ARGS__)
