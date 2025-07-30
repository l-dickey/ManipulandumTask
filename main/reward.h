#pragma once
#include "freertos/FreeRTOS.h"
#include <stdbool.h>

/**
 * @brief  Configure the reward‐output pin.
 */
void reward_init(int gpio_num);

/**
 * @brief  Start N TTL pulses, each 500 ms on / 500 ms off.
 */
void reward_start(int pulses);

/**
 * @brief  Must be called every tick of your main loop (pass xTaskGetTickCount()).
 *         Drives the pin automatically.
 */
void reward_update(TickType_t now);

/**
 * @brief  Returns true while pulses are still in progress.
 */
bool reward_active(void);

