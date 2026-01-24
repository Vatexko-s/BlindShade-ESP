/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

#include <driver/gpio.h>

// Stepper driver wiring (A4988 EN is active LOW).
static constexpr gpio_num_t BS_PIN_STEP = GPIO_NUM_4;
static constexpr gpio_num_t BS_PIN_DIR = GPIO_NUM_5;
static constexpr gpio_num_t BS_PIN_EN = GPIO_NUM_6;
