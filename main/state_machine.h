// main/state_machine.h
#ifndef STATE_MACHINE_H
#define STATE_MACHINE_H

#include "event.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief State machine states for behavioral task
 */
typedef enum {
    S_INIT = 0,
    S_CUE,
    S_GO,          // Renamed from S_MOVING
    S_REWARD,
    S_TIMEOUT,
    S_RESET
} sm_state_t;

/**
 * @brief Event mappings for cue states (reward types 1-3)
 */
static const event_state_t CUE_EVENT[3] = {
    CUE_0,   // Reward type 1
    CUE_1,   // Reward type 2
    CUE_2    // Reward type 3
};

/**
 * @brief Event mappings for reward states (reward types 1-3)
 */
static const event_state_t REW_EVENT[3] = {
    REWARD_0,   // Reward type 1
    REWARD_1,   // Reward type 2
    REWARD_2    // Reward type 3
};

/**
 * @brief Enter a new state and emit the corresponding event marker
 * 
 * @param new_state The state being entered
 * @param event The event marker to emit
 */
static inline void sm_enter(sm_state_t new_state, event_state_t event)
{
    event_send_state(event);
}

/**
 * @brief Enter a new state WITHOUT emitting an event marker
 * Use when the event has already been emitted (e.g., early responses)
 * 
 * @param new_state The state being entered
 */
static inline void sm_enter_no_emit(sm_state_t new_state)
{
    // Just transition without emitting event
    // The actual state variable is updated in the calling code
}

#ifdef __cplusplus
}
#endif

#endif // STATE_MACHINE_H