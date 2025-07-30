#pragma once
#include "event.h"     // for event_state_t & event_send_state()
#include "esp_err.h"   // for ESP_ERROR_CHECK()

// Map reward levels 0–3 → your CUE_x and REWARD_x codes
static const event_state_t CUE_EVENT[4] = {
    CUE_0, CUE_1, CUE_2, CUE_3
};
static const event_state_t REW_EVENT[4] = {
    REWARD_0, REWARD_1, REWARD_2, REWARD_3
};

// Your SM’s internal states
typedef enum {
    S_INIT,
    S_CUE,
    S_MOVING,
    S_REWARD,
    S_TIMEOUT,
    S_RESET
} sm_state_t;

// Track the current state
static sm_state_t _sm_current = S_INIT;

/**
 * Jump into `next` (once), firing the RMT event pulse.
 */
static inline void sm_enter(sm_state_t next, event_state_t ev_code)
{
    if (_sm_current == next) return;
    ESP_ERROR_CHECK(event_send_state(ev_code));
    _sm_current = next;
}
