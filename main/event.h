// event.h — TTL event marker interface
// ESP32-P4, ESP-IDF 5.x

#ifndef EVENT_H
#define EVENT_H

#include "driver/gpio.h"
#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Event state enumeration
 * 
 * Each state corresponds to a specific pulse width for TTL event marking.
 * States are indexed 0-2 for CUE and REWARD to support 3-level discrimination.
 */
typedef enum {
    INIT     = 0,   // Trial initialization marker (10ms)
    CUE_0    = 1,   // Cue type 0 (30ms)
    CUE_1    = 2,   // Cue type 1 (40ms)
    CUE_2    = 3,   // Cue type 2 (50ms)
    GO       = 4,   // Go/Movement cue (16ms)
    REWARD_0 = 5,   // Reward level 0 (70ms)
    REWARD_1 = 6,   // Reward level 1 (80ms)
    REWARD_2 = 7,   // Reward level 2 (90ms)
    TIMEOUT  = 8,   // Trial timeout marker (160ms)
    RESET    = 9,   // Trial reset marker (12ms)
    
    EVENT_STATE_COUNT = 10  // Total number of states
} event_state_t;

// Note: CUE_EVENT and REW_EVENT arrays are defined in state_machine.h
// to avoid duplication and keep behavioral logic separate from hardware

/**
 * @brief Initialize the event marker system
 * 
 * Sets up GPIO and GPTimer hardware for precise TTL pulse generation.
 * The timer resolution parameter is ignored; system uses 10MHz (0.1µs) for optimal precision.
 * 
 * @param pin GPIO pin number for TTL output
 * @param ignored_resolution_hz Ignored parameter (kept for API compatibility)
 * 
 * @return 
 *     - ESP_OK on success
 *     - ESP_ERR_* on failure
 */
esp_err_t event_init_rmt(gpio_num_t pin, uint32_t ignored_resolution_hz);

/**
 * @brief Send an event marker (queued, non-blocking)
 * 
 * Queues an event state for transmission. The event will be sent by a dedicated
 * high-priority task. This function returns immediately.
 * 
 * Typical latency: <100µs from call to pulse start
 * 
 * @param st Event state to send
 * 
 * @return
 *     - ESP_OK if event was queued successfully
 *     - ESP_ERR_TIMEOUT if queue is full
 *     - ESP_ERR_INVALID_ARG if state is invalid
 */
esp_err_t event_send_state(event_state_t st);

/**
 * @brief Send an event marker immediately (bypasses queue)
 * 
 * Sends the event pulse directly without queuing. Use this for time-critical
 * events where minimal latency is required. This function blocks until the
 * pulse starts.
 * 
 * WARNING: Can only be called when no pulse is currently active.
 * 
 * Typical latency: <10µs from call to pulse start
 * 
 * @param st Event state to send
 * 
 * @return
 *     - ESP_OK if pulse started successfully
 *     - ESP_ERR_INVALID_STATE if a pulse is already active
 *     - ESP_ERR_INVALID_ARG if state is invalid
 */
esp_err_t event_send_state_immediate(event_state_t st);

/**
 * @brief Get the number of events waiting in the queue
 * 
 * Useful for debugging and monitoring system load.
 * 
 * @return Number of events currently queued (0 if queue is empty or not initialized)
 */
uint32_t event_get_queue_waiting(void);

/**
 * @brief Deinitialize the event marker system
 * 
 * Cleans up all resources including tasks, queues, timers, and GPIO.
 * The GPIO pin will be set to idle level (low).
 * 
 * @return ESP_OK on success
 */
esp_err_t event_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // EVENT_H