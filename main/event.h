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
    PENALTY  = 8,   // Early movement penalty (130ms)
    TIMEOUT  = 9,   // Trial timeout marker (160ms)
    RESET    = 10,  // Trial reset marker (12ms)
   
    EVENT_STATE_COUNT = 11  // Total number of states
} event_state_t;

// ... rest of the file remains the same ...

esp_err_t event_init_rmt(gpio_num_t pin, uint32_t ignored_resolution_hz);
esp_err_t event_send_state(event_state_t st);
esp_err_t event_send_state_immediate(event_state_t st);
uint32_t event_get_queue_waiting(void);
esp_err_t event_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // EVENT_H