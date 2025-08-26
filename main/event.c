// main/event.c
#include "event.h"
#include "driver/rmt_tx.h"
#include "esp_check.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

static const char *TAG = "EVENT";

// RMT hardware handles
static rmt_channel_handle_t   s_tx_chan   = NULL;
static rmt_encoder_handle_t   s_copy_enc  = NULL;
static gpio_num_t             s_pin       = GPIO_NUM_NC;

// Queue for event markers
static QueueHandle_t          s_event_queue = NULL;
static TaskHandle_t           s_event_task_handle = NULL;

// Pre-computed RMT symbols for each event state
static rmt_symbol_word_t      s_event_symbols[EVENT_STATE_COUNT];

// Pulse widths (in RMT ticks at 10MHz = 0.1µs resolution)
// Converting from microseconds to 0.1µs ticks (multiply by 10)
static const uint32_t EVENT_WIDTH_TICKS[EVENT_STATE_COUNT] = {
    [INIT]     =  100000,   // 10000µs = 100000 ticks
    [CUE_0]    =  300000,   // 30000µs = 300000 ticks
    [CUE_1]    =  400000,   // 40000µs = 400000 ticks
    [CUE_2]    =  500000,   // 50000µs = 500000 ticks
    [CUE_3]    =  600000,   // 60000µs = 600000 ticks
    [MOVING]   =  160000,   // 16000µs = 160000 ticks
    [REWARD_0] =  700000,   // 70000µs = 700000 ticks
    [REWARD_1] =  800000,   // 80000µs = 800000 ticks
    [REWARD_2] =  900000,   // 90000µs = 900000 ticks
    [REWARD_3] = 1000000,   // 100000µs = 1000000 ticks
    [TIMEOUT]  = 1600000,   // 160000µs = 1600000 ticks
    [RESET]    =  120000    // 12000µs = 120000 ticks
};

// High-priority task dedicated to sending event markers
static void event_marker_task(void *pvParameters)
{
    event_state_t state;
    
    ESP_LOGI(TAG, "Event marker task started on core %d", xPortGetCoreID());
    
    while (1) {
        // Wait for event state from queue
        if (xQueueReceive(s_event_queue, &state, portMAX_DELAY) == pdTRUE) {
            
            // Validate state
            if (state >= EVENT_STATE_COUNT) {
                ESP_LOGW(TAG, "Invalid event state: %d", state);
                continue;
            }
            
            // Send the pre-computed symbol immediately
            rmt_transmit_config_t cfg = {
                .loop_count = 0,
                .flags = { .eot_level = 0 }
            };
            
            esp_err_t ret = rmt_transmit(s_tx_chan, s_copy_enc, 
                                       &s_event_symbols[state], 
                                       sizeof(rmt_symbol_word_t), 
                                       &cfg);
            
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "Failed to send event %d: %s", state, esp_err_to_name(ret));
            }
        }
    }
}

esp_err_t event_init_rmt(gpio_num_t pin, uint32_t resolution_hz)
{
    s_pin = pin;
    
    // Override resolution for optimal timing (10MHz = 0.1µs resolution)
    const uint32_t optimal_resolution = 10000000;
    
    // 1) Create TX channel with optimized settings
    rmt_tx_channel_config_t tx_cfg = {
        .clk_src           = RMT_CLK_SRC_DEFAULT,
        .gpio_num          = pin,
        .mem_block_symbols = 48,                 // Minimal memory allocation
        .resolution_hz     = optimal_resolution, // 10MHz for 0.1µs precision
        .trans_queue_depth = 1,                 // Minimal queue depth
        .flags = { .invert_out = false, .with_dma = false }
    };
    
    ESP_RETURN_ON_ERROR(rmt_new_tx_channel(&tx_cfg, &s_tx_chan), TAG, "rmt_new_tx_channel failed");
    ESP_RETURN_ON_ERROR(rmt_enable(s_tx_chan), TAG, "rmt_enable failed");
    
    // 2) Create copy encoder
    rmt_copy_encoder_config_t copy_cfg = {};
    ESP_RETURN_ON_ERROR(rmt_new_copy_encoder(&copy_cfg, &s_copy_enc),
                        TAG, "rmt_new_copy_encoder failed");
    
    // 3) Pre-compute all RMT symbols for zero-overhead transmission
    for (int i = 0; i < EVENT_STATE_COUNT; i++) {
        s_event_symbols[i] = (rmt_symbol_word_t){
            .level0    = 1,                      // High pulse
            .duration0 = EVENT_WIDTH_TICKS[i],   // Event-specific duration
            .level1    = 0,                      // Low level
            .duration1 = 1                       // Minimal low duration (0.1µs)
        };
    }
    
    // 4) Create event queue (small size for low latency)
    s_event_queue = xQueueCreate(8, sizeof(event_state_t));
    if (s_event_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create event queue");
        return ESP_ERR_NO_MEM;
    }
    
    // 5) Create high-priority event marker task pinned to core 1
    BaseType_t task_ret = xTaskCreatePinnedToCore(
        event_marker_task,
        "event_marker",
        2048,                    // Stack size
        NULL,                    // Parameters
        8,                       // High priority (higher than your other tasks)
        &s_event_task_handle,    // Task handle
        1                        // Pin to core 1
    );
    
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create event marker task");
        vQueueDelete(s_event_queue);
        s_event_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    
    ESP_LOGI(TAG, "Event system initialized successfully");
    ESP_LOGI(TAG, "RMT resolution: %lu Hz, Pin: %d", optimal_resolution, pin);
    
    return ESP_OK;
}

esp_err_t event_send_state(event_state_t st)
{
    // Fast validation
    if (st >= EVENT_STATE_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (s_event_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Send to queue with minimal blocking (1 tick timeout)
    // This ensures we don't block the calling task if queue is full
    BaseType_t ret = xQueueSend(s_event_queue, &st, 1);
    
    if (ret != pdTRUE) {
        ESP_LOGW(TAG, "Event queue full, dropping event %d", st);
        return ESP_ERR_TIMEOUT;
    }
    
    return ESP_OK;
}

// New function for critical state transitions that need immediate marking
esp_err_t event_send_state_immediate(event_state_t st)
{
    // Fast validation
    if (st >= EVENT_STATE_COUNT || s_tx_chan == NULL || s_copy_enc == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Send immediately without queue (use sparingly for critical events)
    rmt_transmit_config_t cfg = {
        .loop_count = 0,
        .flags = { .eot_level = 0 }
    };
    
    return rmt_transmit(s_tx_chan, s_copy_enc, 
                       &s_event_symbols[st], 
                       sizeof(rmt_symbol_word_t), 
                       &cfg);
}

// Function to get queue status for debugging
uint32_t event_get_queue_waiting(void)
{
    if (s_event_queue == NULL) {
        return 0;
    }
    return uxQueueMessagesWaiting(s_event_queue);
}

// Cleanup function (optional, for proper shutdown)
esp_err_t event_deinit(void)
{
    // Delete task
    if (s_event_task_handle != NULL) {
        vTaskDelete(s_event_task_handle);
        s_event_task_handle = NULL;
    }
    
    // Delete queue
    if (s_event_queue != NULL) {
        vQueueDelete(s_event_queue);
        s_event_queue = NULL;
    }
    
    // Disable RMT channel (no delete function exists in ESP-IDF v5.x)
    if (s_tx_chan != NULL) {
        rmt_disable(s_tx_chan);
        s_tx_chan = NULL;
    }
    
    if (s_copy_enc != NULL) {
        rmt_del_encoder(s_copy_enc);
        s_copy_enc = NULL;
    }
    
    ESP_LOGI(TAG, "Event system deinitialized");
    return ESP_OK;
}