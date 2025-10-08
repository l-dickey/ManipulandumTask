// main/event.h
#ifndef EVENT_H
#define EVENT_H

#include "driver/gpio.h"
#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Event states for behavioral task
 * Each state gets a unique TTL pulse width for synchronization
 */
typedef enum {
    INIT = 0,      // Trial initialization
    CUE_1,         // Cue for reward type 1
    CUE_2,         // Cue for reward type 2
    CUE_3,         // Cue for reward type 3
    GO,            // Go cue (formerly MOVING)
    REWARD_1,      // Reward delivery type 1
    REWARD_2,      // Reward delivery type 2
    REWARD_3,      // Reward delivery type 3
    TIMEOUT,       // Trial timeout
    RESET,         // Reset/homing phase
    EVENT_STATE_COUNT  // Total number of states
} event_state_t;

/**
 * @brief Initialize the event marker system using RMT
 * 
 * @param pin GPIO pin for event output
 * @param resolution_hz RMT resolution in Hz (will be optimized to 10MHz)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t event_init_rmt(gpio_num_t pin, uint32_t resolution_hz);

/**
 * @brief Send an event marker (queued, low latency)
 * 
 * @param st Event state to send
 * @return esp_err_t ESP_OK on success
 */
esp_err_t event_send_state(event_state_t st);

/**
 * @brief Send an event marker immediately (bypasses queue)
 * Use sparingly for critical timing events
 * 
 * @param st Event state to send
 * @return esp_err_t ESP_OK on success
 */
esp_err_t event_send_state_immediate(event_state_t st);

/**
 * @brief Get number of events waiting in queue
 * 
 * @return uint32_t Number of pending events
 */
uint32_t event_get_queue_waiting(void);

/**
 * @brief Clean up event system resources
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t event_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // EVENT_H