#pragma once
#include "esp_err.h"
#include <stdint.h>
#include "driver/gpio.h"

typedef enum {
    INIT, CUE_0, CUE_1, CUE_2, CUE_3, MOVING,
    REWARD_0, REWARD_1, REWARD_2, REWARD_3,
    TIMEOUT, RESET, EVENT_STATE_COUNT
} event_state_t;

esp_err_t event_init_rmt(gpio_num_t pin, uint32_t resolution_hz);
esp_err_t event_send_state(event_state_t st);
