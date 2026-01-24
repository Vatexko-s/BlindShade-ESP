/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <atomic>
#include <cstdlib>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <driver/gpio.h>
#include <esp_rom_sys.h>

#include <app/clusters/window-covering-server/window-covering-server.h>
#include <esp_matter.h>
#include <esp_matter_attribute_utils.h>
#include <platform/CHIPDeviceLayer.h>

#include "app_priv.h"
#include "bs_log.h"
#include "bs_pins.h"

using namespace chip::app::Clusters;
using namespace esp_matter;

namespace {
constexpr uint16_t k_percent_100ths_max = 10000;
constexpr uint16_t k_max_steps = 5000;
constexpr uint16_t k_step_pulse_us = 10;
constexpr uint16_t k_step_delay_us = 2000;
constexpr TickType_t k_update_period_ticks = pdMS_TO_TICKS(100);
constexpr TickType_t k_report_min_interval_ticks = pdMS_TO_TICKS(200);
constexpr uint16_t k_report_every_steps = 50;
constexpr uint16_t k_yield_every_steps = 200;

struct motor_state_t {
    uint16_t current_percent100ths;
    uint16_t target_percent100ths;
    uint16_t current_steps;
    uint16_t target_steps;
    int8_t moving_dir;
    bool moving;
};

SemaphoreHandle_t s_state_lock = nullptr;
TaskHandle_t s_stepper_task = nullptr;
TaskHandle_t s_update_task = nullptr;
uint16_t s_endpoint_id = 0;
motor_state_t s_state = {};
std::atomic<bool> s_report_pending(false);

static inline void step_once()
{
    gpio_set_level(BS_PIN_STEP, 1);
    esp_rom_delay_us(k_step_pulse_us);
    gpio_set_level(BS_PIN_STEP, 0);
    esp_rom_delay_us(k_step_delay_us);
}

uint16_t clamp_percent100ths(uint16_t value)
{
    return value > k_percent_100ths_max ? k_percent_100ths_max : value;
}

uint16_t steps_from_percent100ths(uint16_t percent100ths)
{
    uint32_t scaled = static_cast<uint32_t>(percent100ths) * k_max_steps + (k_percent_100ths_max / 2);
    return static_cast<uint16_t>(scaled / k_percent_100ths_max);
}

uint16_t percent100ths_from_steps(uint16_t steps)
{
    uint32_t scaled = static_cast<uint32_t>(steps) * k_percent_100ths_max + (k_max_steps / 2);
    return static_cast<uint16_t>(scaled / k_max_steps);
}

void apply_wc_update(uint16_t endpoint_id, uint16_t current_percent100ths, bool moving, int8_t moving_dir)
{
    esp_matter_attr_val_t val = esp_matter_nullable_uint16(current_percent100ths);
    attribute::update(endpoint_id, WindowCovering::Id, WindowCovering::Attributes::CurrentPositionLiftPercent100ths::Id, &val);

    using chip::app::Clusters::WindowCovering::OperationalState;
    OperationalState state = OperationalState::Stall;
    if (moving) {
        state = (moving_dir > 0) ? OperationalState::MovingUpOrOpen : OperationalState::MovingDownOrClose;
    }
    chip::app::Clusters::WindowCovering::OperationalStateSet(endpoint_id, WindowCovering::OperationalStatus::kLift, state);
}

void report_work(intptr_t arg)
{
    (void)arg;
    if (!s_state_lock) {
        s_report_pending.store(false);
        return;
    }

    if (xSemaphoreTake(s_state_lock, 0) != pdTRUE) {
        s_report_pending.store(false);
        return;
    }

    motor_state_t state = s_state;
    xSemaphoreGive(s_state_lock);

    apply_wc_update(s_endpoint_id, state.current_percent100ths, state.moving, state.moving_dir);
    s_report_pending.store(false);
}

void stepper_task(void *arg)
{
    (void)arg;
    uint16_t since_yield = 0;
    while (true) {
        if (!s_state_lock) {
            vTaskDelay(10 / portTICK_PERIOD_MS);
            continue;
        }

        if (xSemaphoreTake(s_state_lock, portMAX_DELAY) != pdTRUE) {
            vTaskDelay(10 / portTICK_PERIOD_MS);
            continue;
        }

        bool moving = s_state.moving;
        int8_t dir = s_state.moving_dir;
        uint16_t target_steps = s_state.target_steps;
        uint16_t current_steps = s_state.current_steps;
        xSemaphoreGive(s_state_lock);

        if (!moving) {
            gpio_set_level(BS_PIN_EN, 1);
            vTaskDelay(10 / portTICK_PERIOD_MS);
            continue;
        }

        gpio_set_level(BS_PIN_EN, 0);
        gpio_set_level(BS_PIN_DIR, (dir > 0) ? 1 : 0);
        step_once();

        if (xSemaphoreTake(s_state_lock, portMAX_DELAY) != pdTRUE) {
            vTaskDelay(10 / portTICK_PERIOD_MS);
            continue;
        }

        if (dir > 0) {
            if (current_steps < k_max_steps) {
                current_steps++;
            }
        } else {
            if (current_steps > 0) {
                current_steps--;
            }
        }

        s_state.current_steps = current_steps;
        s_state.current_percent100ths = percent100ths_from_steps(current_steps);

        if (current_steps == target_steps) {
            s_state.moving = false;
            s_state.moving_dir = 0;
            BS_LOG_STATE("Reached target %u.%02u%%",
                         static_cast<unsigned>(s_state.current_percent100ths / 100),
                         static_cast<unsigned>(s_state.current_percent100ths % 100));
        }

        xSemaphoreGive(s_state_lock);

        since_yield++;
        if (since_yield >= k_yield_every_steps) {
            since_yield = 0;
            vTaskDelay(1);
        }
    }
}

void update_task(void *arg)
{
    (void)arg;
    uint16_t last_reported_steps = 0xFFFF;
    bool last_moving = false;
    int8_t last_dir = 0;
    TickType_t last_report_tick = 0;

    while (true) {
        if (!s_state_lock) {
            vTaskDelay(k_update_period_ticks);
            continue;
        }

        if (xSemaphoreTake(s_state_lock, portMAX_DELAY) != pdTRUE) {
            vTaskDelay(k_update_period_ticks);
            continue;
        }

        uint16_t current_steps = s_state.current_steps;
        bool moving = s_state.moving;
        int8_t dir = s_state.moving_dir;
        xSemaphoreGive(s_state_lock);

        bool state_changed = (moving != last_moving) || (dir != last_dir);
        bool steps_changed = (current_steps != last_reported_steps);
        bool moved_enough = steps_changed &&
            (static_cast<uint16_t>(std::abs(static_cast<int>(current_steps) - static_cast<int>(last_reported_steps))) >=
             k_report_every_steps);
        TickType_t now = xTaskGetTickCount();
        bool time_ok = (now - last_report_tick) >= k_report_min_interval_ticks;
        bool should_report = state_changed || (!moving && steps_changed) || (moving && moved_enough && time_ok);

        if (should_report && !s_report_pending.exchange(true)) {
            CHIP_ERROR err = chip::DeviceLayer::PlatformMgr().ScheduleWork(report_work, 0);
            if (err == CHIP_NO_ERROR) {
                last_reported_steps = current_steps;
                last_report_tick = now;
                last_moving = moving;
                last_dir = dir;
            } else {
                s_report_pending.store(false);
            }
        }

        vTaskDelay(k_update_period_ticks);
    }
}

} // namespace

esp_err_t app_driver_init(uint16_t endpoint_id)
{
    s_endpoint_id = endpoint_id;
    s_state_lock = xSemaphoreCreateMutex();
    if (!s_state_lock) {
        BS_LOG_ERROR("Failed to create motor state mutex");
        return ESP_ERR_NO_MEM;
    }

    gpio_config_t cfg = {};
    cfg.pin_bit_mask = (1ULL << BS_PIN_STEP) | (1ULL << BS_PIN_DIR) | (1ULL << BS_PIN_EN);
    cfg.mode = GPIO_MODE_OUTPUT;
    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        BS_LOG_ERROR("Failed to init motor GPIOs: %d", err);
        return err;
    }

    gpio_set_level(BS_PIN_STEP, 0);
    gpio_set_level(BS_PIN_DIR, 1);
    gpio_set_level(BS_PIN_EN, 1);

    s_state.current_percent100ths = 0;
    s_state.target_percent100ths = 0;
    s_state.current_steps = 0;
    s_state.target_steps = 0;
    s_state.moving_dir = 0;
    s_state.moving = false;

    BaseType_t ok = xTaskCreate(stepper_task, "wc_stepper", 4096, nullptr, 2, &s_stepper_task);
    if (ok != pdPASS) {
        BS_LOG_ERROR("Failed to start stepper task");
        return ESP_ERR_NO_MEM;
    }

    ok = xTaskCreate(update_task, "wc_update", 4096, nullptr, 1, &s_update_task);
    if (ok != pdPASS) {
        BS_LOG_ERROR("Failed to start update task");
        return ESP_ERR_NO_MEM;
    }

    BS_LOG_MOTOR("Pins: STEP=GPIO%u DIR=GPIO%u EN=GPIO%u (EN active LOW)",
                 static_cast<unsigned>(BS_PIN_STEP),
                 static_cast<unsigned>(BS_PIN_DIR),
                 static_cast<unsigned>(BS_PIN_EN));
    BS_LOG_MOTOR("Stepper: max_steps=%u, pulse=%uus, delay=%uus", k_max_steps, k_step_pulse_us, k_step_delay_us);

    return ESP_OK;
}

void app_driver_set_target_percent100ths(uint16_t endpoint_id, uint16_t target_percent100ths)
{
    if (endpoint_id != s_endpoint_id || !s_state_lock) {
        return;
    }
    if (xSemaphoreTake(s_state_lock, portMAX_DELAY) != pdTRUE) {
        return;
    }

    uint16_t target = clamp_percent100ths(target_percent100ths);
    uint16_t target_steps = steps_from_percent100ths(target);
    s_state.target_percent100ths = target;
    s_state.target_steps = target_steps;

    int32_t diff = static_cast<int32_t>(target_steps) - static_cast<int32_t>(s_state.current_steps);
    if (diff == 0) {
        s_state.moving = false;
        s_state.moving_dir = 0;
    } else {
        s_state.moving = true;
        s_state.moving_dir = (diff > 0) ? 1 : -1;
    }

    BS_LOG_STATE("Target set -> %u.%02u%% (%u steps)",
                 static_cast<unsigned>(target / 100), static_cast<unsigned>(target % 100),
                 static_cast<unsigned>(target_steps));

    xSemaphoreGive(s_state_lock);
}

void app_driver_stop(uint16_t endpoint_id)
{
    if (endpoint_id != s_endpoint_id || !s_state_lock) {
        return;
    }
    if (xSemaphoreTake(s_state_lock, portMAX_DELAY) != pdTRUE) {
        return;
    }

    s_state.moving = false;
    s_state.moving_dir = 0;
    s_state.target_percent100ths = s_state.current_percent100ths;
    s_state.target_steps = s_state.current_steps;
    gpio_set_level(BS_PIN_EN, 1);

    BS_LOG_STATE("Stopped at %u.%02u%% (%u steps)",
                 static_cast<unsigned>(s_state.current_percent100ths / 100),
                 static_cast<unsigned>(s_state.current_percent100ths % 100),
                 static_cast<unsigned>(s_state.current_steps));

    xSemaphoreGive(s_state_lock);
}
