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
#include <esp_timer.h>
#include <nvs_flash.h>
#include <nvs.h>

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

// === CALIBRATION HARDWARE ===
constexpr gpio_num_t k_btn_up = GPIO_NUM_1;
constexpr gpio_num_t k_btn_stop = GPIO_NUM_2;
constexpr gpio_num_t k_btn_down = GPIO_NUM_3;
constexpr gpio_num_t k_led_calib = GPIO_NUM_7;

// === CALIBRATION CONFIG ===
constexpr uint32_t k_btn_debounce_ms = 50;
constexpr uint32_t k_btn_hold_ms = 2000;
constexpr uint32_t k_double_press_ms = 1000;
constexpr uint32_t k_calib_timeout_ms = 300000;  // 5 minutes
constexpr uint16_t k_min_travel_steps = 100;

enum class CalibState : uint8_t {
    IDLE,              // Normal operation
    READY,             // Calibration mode active, waiting for input
    MOVING_TO_HOME,    // User pressed UP, moving to home
    HOME_SET,          // Home position saved, ready for bottom
    MOVING_TO_BOTTOM,  // User pressed DOWN, moving to bottom
    COMPLETE           // Bottom set, waiting for exit
};

enum class ButtonState : uint8_t {
    RELEASED,
    PRESSED,
    HELD
};

struct button_data_t {
    ButtonState state;
    int64_t last_change_us;
    int64_t press_start_us;
    bool debounce_stable;
};

struct motor_state_t {
    uint16_t current_percent100ths;
    uint16_t target_percent100ths;
    uint16_t current_steps;
    uint16_t target_steps;
    int8_t moving_dir;
    bool moving;
};

// === SHARED WITH CALIBRATION MODULE ===
SemaphoreHandle_t s_state_lock = nullptr;
motor_state_t s_state = {};
CalibState s_calib_state = CalibState::IDLE;

// === MOTOR CONTROL STATE ===
TaskHandle_t s_stepper_task = nullptr;
TaskHandle_t s_update_task = nullptr;
uint16_t s_endpoint_id = 0;
std::atomic<bool> s_report_pending(false);

// === CALIBRATION STATE ===
TaskHandle_t s_button_task = nullptr;
TaskHandle_t s_led_task = nullptr;
button_data_t s_btn_up_data = {};
button_data_t s_btn_stop_data = {};
button_data_t s_btn_down_data = {};
int64_t s_last_stop_press_us = 0;
int64_t s_calib_last_activity_us = 0;
uint16_t s_home_steps = 0;
uint16_t s_bottom_steps = 5000;
bool s_matter_blocked = false;

// === LED CONTROL ===
std::atomic<uint8_t> s_led_blink_count(0);
std::atomic<uint16_t> s_led_blink_period_ms(0);
std::atomic<bool> s_led_continuous(false);

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
    uint16_t max = (s_bottom_steps > 0) ? s_bottom_steps : k_max_steps;
    uint32_t scaled = static_cast<uint32_t>(percent100ths) * max + (k_percent_100ths_max / 2);
    return static_cast<uint16_t>(scaled / k_percent_100ths_max);
}

uint16_t percent100ths_from_steps(uint16_t steps)
{
    uint16_t max = (s_bottom_steps > 0) ? s_bottom_steps : k_max_steps;
    uint32_t scaled = static_cast<uint32_t>(steps) * k_percent_100ths_max + (max / 2);
    return static_cast<uint16_t>(scaled / max);
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
            // Moving DOWN (increasing steps)
            // During calibration to bottom, count from 0 up to 20000 max
            if (s_calib_state == CalibState::MOVING_TO_BOTTOM) {
                if (current_steps < 65535) {  // Prevent uint16_t overflow
                    current_steps++;
                } else {
                    // Reached maximum possible steps - stop here
                    BS_LOG_ERROR("⚠️  Reached maximum steps (65535) during calibration!");
                    s_state.moving = false;
                    s_state.moving_dir = 0;
                }
            }
            // Normal operation - respect bottom limit
            else if (current_steps < s_bottom_steps) {
                current_steps++;
            }
        } else {
            // Moving UP (decreasing steps)
            // During calibration to home: DON'T change counter, only motor moves!
            // Counter will be reset to 0 when user presses STOP
            if (s_calib_state == CalibState::MOVING_TO_HOME) {
                // Motor moves but counter stays unchanged - STOP defines the zero point
            }
            // Normal operation - respect home limit (0)
            else if (current_steps > 0) {
                current_steps--;
            }
        }

        s_state.current_steps = current_steps;
        s_state.current_percent100ths = percent100ths_from_steps(current_steps);

        // During HOME calibration, motor runs until STOP is pressed (ignore target)
        // During BOTTOM calibration, motor runs until STOP is pressed (ignore target)
        if (s_calib_state != CalibState::MOVING_TO_HOME && 
            s_calib_state != CalibState::MOVING_TO_BOTTOM &&
            current_steps == target_steps) {
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

// === LED CONTROL TASK ===
void led_task(void *arg)
{
    (void)arg;
    while (true) {
        if (s_led_continuous.load()) {
            gpio_set_level(k_led_calib, 1);
            vTaskDelay(pdMS_TO_TICKS(100));
        } else {
            uint8_t count = s_led_blink_count.load();
            uint16_t period = s_led_blink_period_ms.load();
            
            if (count > 0 && period > 0) {
                for (uint8_t i = 0; i < count; i++) {
                    gpio_set_level(k_led_calib, 1);
                    vTaskDelay(pdMS_TO_TICKS(period / 2));
                    gpio_set_level(k_led_calib, 0);
                    vTaskDelay(pdMS_TO_TICKS(period / 2));
                }
                s_led_blink_count.store(0);
                s_led_blink_period_ms.store(0);
            } else {
                gpio_set_level(k_led_calib, 0);
                vTaskDelay(pdMS_TO_TICKS(100));
            }
        }
    }
}

void set_led_blink(uint8_t count, uint16_t period_ms)
{
    s_led_continuous.store(false);
    s_led_blink_count.store(count);
    s_led_blink_period_ms.store(period_ms);
}

void set_led_continuous(bool enabled)
{
    s_led_continuous.store(enabled);
    s_led_blink_count.store(0);
    s_led_blink_period_ms.store(0);
}

// === BUTTON HELPERS ===
bool update_button_state(button_data_t &btn, bool raw_pressed)
{
    int64_t now = esp_timer_get_time();
    
    // Debouncing
    if (raw_pressed != (btn.state != ButtonState::RELEASED)) {
        if (now - btn.last_change_us > k_btn_debounce_ms * 1000) {
            btn.last_change_us = now;
            if (raw_pressed) {
                btn.state = ButtonState::PRESSED;
                btn.press_start_us = now;
                btn.debounce_stable = true;
                return true;  // New press event
            } else {
                btn.state = ButtonState::RELEASED;
                btn.debounce_stable = true;
            }
        }
    }
    
    // Hold detection
    if (btn.state == ButtonState::PRESSED && raw_pressed) {
        if (now - btn.press_start_us > k_btn_hold_ms * 1000) {
            btn.state = ButtonState::HELD;
        }
    }
    
    return false;
}

// === NVS HELPERS ===
void reset_calibration_to_defaults()
{
    s_home_steps = 0;
    s_bottom_steps = k_max_steps;
    BS_LOG_MOTOR("🔄 Reset calibration to defaults: home=0, bottom=%u", k_max_steps);
}

void clear_calibration_nvs()
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("calibration", NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        nvs_erase_key(handle, "home_steps");
        nvs_erase_key(handle, "bottom_steps");
        nvs_commit(handle);
        nvs_close(handle);
        BS_LOG_MOTOR("🗑️  Cleared calibration from NVS");
    }
}

void load_calibration_from_nvs()
{
    // Start with defaults
    reset_calibration_to_defaults();
    
    nvs_handle_t handle;
    esp_err_t err = nvs_open("calibration", NVS_READONLY, &handle);
    if (err == ESP_OK) {
        uint16_t home = 0;
        uint16_t bottom = k_max_steps;
        
        nvs_get_u16(handle, "home_steps", &home);
        nvs_get_u16(handle, "bottom_steps", &bottom);
        nvs_close(handle);
        
        // Validate loaded values
        bool valid = true;
        
        // Home should always be 0
        if (home != 0) {
            BS_LOG_ERROR("❌ Invalid home position: %u (expected 0)", home);
            valid = false;
        }
        
        // Bottom should be reasonable (100 to 20000 steps)
        if (bottom < k_min_travel_steps || bottom > 20000) {
            BS_LOG_ERROR("❌ Invalid bottom position: %u (expected %u-20000)", bottom, k_min_travel_steps);
            valid = false;
        }
        
        if (valid) {
            s_home_steps = home;
            s_bottom_steps = bottom;
            BS_LOG_MOTOR("✅ Loaded calibration: home=%u, bottom=%u", s_home_steps, s_bottom_steps);
        } else {
            BS_LOG_ERROR("⚠️  Invalid calibration data, using defaults");
            clear_calibration_nvs();  // Clear bad data
            reset_calibration_to_defaults();
        }
    } else {
        BS_LOG_MOTOR("ℹ️  No calibration in NVS, using defaults: home=0, bottom=%u", k_max_steps);
    }
}

void save_calibration_to_nvs()
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("calibration", NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        nvs_set_u16(handle, "home_steps", s_home_steps);
        nvs_set_u16(handle, "bottom_steps", s_bottom_steps);
        nvs_commit(handle);
        nvs_close(handle);
        BS_LOG_STATE("💾 Calibration saved to NVS");
    } else {
        BS_LOG_ERROR("Failed to save calibration: %d", err);
    }
}

// === CALIBRATION STATE MACHINE ===
void handle_calibration_events()
{
    int64_t now = esp_timer_get_time();
    
    // Read GPIO
    bool up_raw = (gpio_get_level(k_btn_up) == 0);
    bool stop_raw = (gpio_get_level(k_btn_stop) == 0);
    bool down_raw = (gpio_get_level(k_btn_down) == 0);
    
    // Update button states
    bool up_pressed = update_button_state(s_btn_up_data, up_raw);
    bool stop_pressed = update_button_state(s_btn_stop_data, stop_raw);
    bool down_pressed = update_button_state(s_btn_down_data, down_raw);
    
    // Timeout check (5 minutes)
    if (s_calib_state != CalibState::IDLE) {
        if (now - s_calib_last_activity_us > k_calib_timeout_ms * 1000) {
            BS_LOG_ERROR("⏱️  Calibration timeout!");
            set_led_blink(10, 100);  // Fast error blinks
            s_calib_state = CalibState::IDLE;
            s_matter_blocked = false;
            return;
        }
    }
    
    switch (s_calib_state) {
        case CalibState::IDLE:
            // Entry: Hold STOP for 2 seconds
            if (s_btn_stop_data.state == ButtonState::HELD) {
                BS_LOG_STATE("🔧 ENTERING CALIBRATION MODE");
                s_calib_state = CalibState::READY;
                s_calib_last_activity_us = now;
                s_matter_blocked = true;
                set_led_continuous(true);  // LED ON continuously
                s_btn_stop_data.state = ButtonState::RELEASED;  // Reset to avoid re-trigger
            }
            break;
            
        case CalibState::READY:
            if (up_pressed) {
                BS_LOG_STATE("⬆️  Starting move to HOME position");
                s_calib_state = CalibState::MOVING_TO_HOME;
                s_calib_last_activity_us = now;
                
                // Start motor moving up (direction -1 = towards 0)
                xSemaphoreTake(s_state_lock, portMAX_DELAY);
                s_state.target_steps = 0;
                s_state.target_percent100ths = 0;
                s_state.moving = true;
                s_state.moving_dir = -1;  // UP = towards 0
                xSemaphoreGive(s_state_lock);
            }
            break;
            
        case CalibState::MOVING_TO_HOME:
            if (stop_pressed) {
                BS_LOG_STATE("✅ HOME position set!");
                xSemaphoreTake(s_state_lock, portMAX_DELAY);
                // Reset to absolute zero - this is home position
                s_home_steps = 0;  // Home is always 0
                s_state.current_steps = 0;
                s_state.current_percent100ths = 0;
                s_state.target_steps = 0;
                s_state.target_percent100ths = 0;
                s_state.moving = false;  // Stop motor
                s_state.moving_dir = 0;
                xSemaphoreGive(s_state_lock);
                
                s_calib_state = CalibState::HOME_SET;
                s_calib_last_activity_us = now;
                set_led_blink(2, 200);  // 2 quick blinks
                
                BS_LOG_STATE("💾 Saving home position (0) to NVS");
                save_calibration_to_nvs();
            }
            break;
            
        case CalibState::HOME_SET:
            if (down_pressed) {
                BS_LOG_STATE("⬇️  Starting move to BOTTOM position");
                s_calib_state = CalibState::MOVING_TO_BOTTOM;
                s_calib_last_activity_us = now;
                
                // Start motor moving down (direction +1 = away from 0)
                xSemaphoreTake(s_state_lock, portMAX_DELAY);
                s_state.target_steps = 65535;  // Large value
                s_state.target_percent100ths = k_percent_100ths_max;
                s_state.moving = true;
                s_state.moving_dir = 1;  // DOWN = positive direction
                xSemaphoreGive(s_state_lock);
            }
            break;
            
        case CalibState::MOVING_TO_BOTTOM:
            if (stop_pressed) {
                xSemaphoreTake(s_state_lock, portMAX_DELAY);
                uint16_t travel = s_state.current_steps;
                s_state.moving = false;  // Stop motor
                s_state.moving_dir = 0;
                xSemaphoreGive(s_state_lock);
                
                if (travel < k_min_travel_steps) {
                    BS_LOG_ERROR("❌ Travel too short (%u < %u steps)", travel, k_min_travel_steps);
                    set_led_blink(10, 100);  // Error
                    s_calib_state = CalibState::HOME_SET;  // Try again
                } else if (travel > 20000) {
                    BS_LOG_ERROR("❌ Travel too long (%u > 20000 steps) - motor may be stuck!", travel);
                    set_led_blink(10, 100);  // Error
                    s_calib_state = CalibState::HOME_SET;  // Try again
                } else {
                    BS_LOG_STATE("✅ BOTTOM position set! Travel: %u steps from home", travel);
                    s_bottom_steps = travel;  // This is the total travel distance
                    s_calib_state = CalibState::COMPLETE;
                    s_calib_last_activity_us = now;
                    set_led_blink(3, 200);  // 3 quick blinks
                    
                    BS_LOG_STATE("💾 Saving bottom position (%u) to NVS", travel);
                    save_calibration_to_nvs();
                }
            }
            break;
            
        case CalibState::COMPLETE:
            // Double-press STOP to exit
            if (stop_pressed) {
                if (now - s_last_stop_press_us < k_double_press_ms * 1000) {
                    BS_LOG_STATE("🏁 CALIBRATION COMPLETE - Exiting");
                    s_calib_state = CalibState::IDLE;
                    s_matter_blocked = false;
                    set_led_blink(5, 150);  // 5 victory blinks
                } else {
                    s_last_stop_press_us = now;
                }
            }
            break;
    }
}

// === BUTTON TASK ===
void button_task(void *arg)
{
    (void)arg;
    BS_LOG_STATE("🎮 Calibration button task started");
    
    while (true) {
        handle_calibration_events();
        vTaskDelay(pdMS_TO_TICKS(20));
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

    // === INIT CALIBRATION HARDWARE ===
    gpio_config_t btn_cfg = {};
    btn_cfg.pin_bit_mask = (1ULL << k_btn_up) | (1ULL << k_btn_stop) | (1ULL << k_btn_down);
    btn_cfg.mode = GPIO_MODE_INPUT;
    btn_cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    btn_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    err = gpio_config(&btn_cfg);
    if (err != ESP_OK) {
        BS_LOG_ERROR("Failed to init button GPIOs: %d", err);
        return err;
    }
    
    gpio_config_t led_cfg = {};
    led_cfg.pin_bit_mask = (1ULL << k_led_calib);
    led_cfg.mode = GPIO_MODE_OUTPUT;
    err = gpio_config(&led_cfg);
    if (err != ESP_OK) {
        BS_LOG_ERROR("Failed to init LED GPIO: %d", err);
        return err;
    }
    gpio_set_level(k_led_calib, 0);  // LED OFF initially
    
    BS_LOG_MOTOR("🎛️  Calibration HW: UP=GPIO%u STOP=GPIO%u DOWN=GPIO%u LED=GPIO%u",
                 static_cast<unsigned>(k_btn_up),
                 static_cast<unsigned>(k_btn_stop),
                 static_cast<unsigned>(k_btn_down),
                 static_cast<unsigned>(k_led_calib));

    // Load calibration from NVS
    load_calibration_from_nvs();

    s_state.current_percent100ths = 0;
    s_state.target_percent100ths = 0;
    s_state.current_steps = 0;
    s_state.target_steps = 0;
    s_state.moving_dir = 0;
    s_state.moving = false;

    // Start LED task
    BaseType_t ok = xTaskCreate(led_task, "calib_led", 3072, nullptr, 1, &s_led_task);
    if (ok != pdPASS) {
        BS_LOG_ERROR("Failed to start LED task");
        return ESP_ERR_NO_MEM;
    }

    // Start button calibration task
    ok = xTaskCreate(button_task, "calib_btn", 4096, nullptr, 3, &s_button_task);
    if (ok != pdPASS) {
        BS_LOG_ERROR("Failed to start button task");
        return ESP_ERR_NO_MEM;
    }

    ok = xTaskCreate(stepper_task, "wc_stepper", 4096, nullptr, 2, &s_stepper_task);
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
    
    // Block Matter commands during calibration
    if (s_matter_blocked) {
        BS_LOG_STATE("⚠️  Matter command BLOCKED - calibration in progress");
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
    
    // Block Matter commands during calibration
    if (s_matter_blocked) {
        BS_LOG_STATE("⚠️  Matter STOP command BLOCKED - calibration in progress");
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
