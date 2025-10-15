// event.c — ESP32-P4, ESP-IDF 5.x
// Optimized for minimal jitter TTL event markers
#include "event.h"
#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_check.h"
#include "esp_attr.h"
#include "rom/ets_sys.h"
#include "esp_cpu.h"

#define TIMER_RES_HZ              10000000UL  // 10 MHz → 0.1 µs ticks
#define MIN_WIDTH_US              20          // reject too-short pulses
#define MIN_INTERPULSE_GAP_US     50          // increased gap for cleaner separation
#define GPIO_IDLE_LEVEL           0
#define GPIO_ACTIVE_LEVEL         1

static gpio_num_t         s_pin    = GPIO_NUM_NC;
static gptimer_handle_t   s_tim    = NULL;
static QueueHandle_t      s_q      = NULL;
static TaskHandle_t       s_task   = NULL;
static SemaphoreHandle_t  s_done   = NULL;
static volatile bool      s_active = false;

// Pulse widths in microseconds (3-level discrimination: indices 0-2)
static const uint32_t EVENT_WIDTH_US[EVENT_STATE_COUNT] = {
    [INIT]     = 10000,   // 10ms
    [CUE_0]    = 30000,   // 30ms - level 0
    [CUE_1]    = 40000,   // 40ms - level 1
    [CUE_2]    = 50000,   // 50ms - level 2
    [GO]       = 16000,   // 16ms - movement cue
    [REWARD_0] = 70000,   // 70ms - level 0
    [REWARD_1] = 80000,   // 80ms - level 1
    [REWARD_2] = 90000,   // 90ms - level 2
    [TIMEOUT]  = 160000,  // 160ms
    [RESET]    = 12000    // 12ms
};

static inline bool width_ok(uint32_t us) { return us >= MIN_WIDTH_US; }

// ISR kept in IRAM for minimal latency
static bool IRAM_ATTR on_alarm(gptimer_handle_t t,
                               const gptimer_alarm_event_data_t *e,
                               void *ctx)
{
    (void)e; (void)ctx;
    
    // Immediate GPIO drop - this is the critical timing edge
    gpio_set_level(s_pin, GPIO_IDLE_LEVEL);
    s_active = false;
    
    // Stop timer after GPIO (order matters for timing)
    gptimer_stop(t);

    // Signal completion
    BaseType_t hp = pdFALSE;
    if (s_done) xSemaphoreGiveFromISR(s_done, &hp);
    return hp == pdTRUE;
}

// IRAM placement ensures fast execution, critical for jitter
static esp_err_t IRAM_ATTR start_pulse_us(uint32_t width_us)
{
    if (!width_ok(width_us)) return ESP_ERR_INVALID_ARG;
    if (s_active)            return ESP_ERR_INVALID_STATE;

    // Pre-calculate alarm count outside critical section
    const uint64_t ticks = (uint64_t)width_us * (TIMER_RES_HZ / 1000000UL);
    
    // Configure alarm before critical section
    gptimer_alarm_config_t cfg = {
        .alarm_count = ticks,
        .flags.auto_reload_on_alarm = false
    };
    
    // Stop any previous operation
    gptimer_stop(s_tim);
    
    // === CRITICAL SECTION: Minimize jitter ===
    // Disable interrupts for atomic GPIO + timer start
    uint32_t irq_state = portSET_INTERRUPT_MASK_FROM_ISR();
    
    // Set GPIO high - this is the rising edge we're trying to time precisely
    gpio_set_level(s_pin, GPIO_ACTIVE_LEVEL);
    s_active = true;
    
    // Reset counter to zero
    gptimer_set_raw_count(s_tim, 0);
    
    // Set alarm configuration
    gptimer_set_alarm_action(s_tim, &cfg);
    
    // Start timer - now running from zero to alarm_count
    gptimer_start(s_tim);
    
    // Re-enable interrupts
    portCLEAR_INTERRUPT_MASK_FROM_ISR(irq_state);
    // === END CRITICAL SECTION ===

    return ESP_OK;
}

static void event_task(void *arg)
{
    event_state_t st;
    
    // Set this task to run on a specific core for consistency
    // Core 1 typically has fewer system tasks on ESP32-P4
    
    for (;;) {
        // Block waiting for event
        if (xQueueReceive(s_q, &st, portMAX_DELAY) != pdTRUE) continue;
        if (st >= EVENT_STATE_COUNT) continue;

        // If a pulse is still active, wait for completion
        if (s_active) {
            xSemaphoreTake(s_done, pdMS_TO_TICKS(10));
        }
        
        // Enforce inter-pulse gap for clean separation
        if (MIN_INTERPULSE_GAP_US > 0) {
            ets_delay_us(MIN_INTERPULSE_GAP_US);
        }

        const uint32_t w_us = EVENT_WIDTH_US[st];
        if (start_pulse_us(w_us) != ESP_OK) continue;

        // Wait for ISR to complete the pulse
        // Add small margin over expected width
        TickType_t timeout_ms = (w_us / 1000) + 5;
        if (xSemaphoreTake(s_done, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
            // Recovery from stuck pulse
            gptimer_stop(s_tim);
            gpio_set_level(s_pin, GPIO_IDLE_LEVEL);
            s_active = false;
        }
    }
}

esp_err_t event_init_rmt(gpio_num_t pin, uint32_t ignored_resolution_hz)
{
    (void)ignored_resolution_hz;
    s_pin = pin;

    // === GPIO Configuration ===
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io), "EVENT", "gpio_config failed");
    
    // Set initial state
    gpio_set_level(s_pin, GPIO_IDLE_LEVEL);
    
    // Maximum drive strength for fast edge transitions
    gpio_set_drive_capability(s_pin, GPIO_DRIVE_CAP_3);

    // === GPTimer Configuration ===
    gptimer_config_t tcfg = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = TIMER_RES_HZ,
        .flags.intr_shared = false  // Dedicated interrupt for lower latency
    };
    ESP_RETURN_ON_ERROR(gptimer_new_timer(&tcfg, &s_tim), "EVENT", "new_timer failed");
    
    // Register alarm callback
    gptimer_event_callbacks_t cbs = { 
        .on_alarm = on_alarm 
    };
    ESP_RETURN_ON_ERROR(gptimer_register_event_callbacks(s_tim, &cbs, NULL), 
                        "EVENT", "register cbs failed");

    // Enable timer (must be called before use)
    ESP_RETURN_ON_ERROR(gptimer_enable(s_tim), "EVENT", "enable failed");

    // === Synchronization Objects ===
    s_done = xSemaphoreCreateBinary();
    if (!s_done) return ESP_ERR_NO_MEM;
    xSemaphoreGive(s_done);

    s_q = xQueueCreate(16, sizeof(event_state_t));  // Increased from 8
    if (!s_q) return ESP_ERR_NO_MEM;

    // === Create Event Task ===
    // Pin to core 1, high priority (higher than application tasks)
    if (xTaskCreatePinnedToCore(event_task, "event_marker", 
                                3072,  // Increased stack
                                NULL, 
                                10,    // Very high priority
                                &s_task, 
                                1) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

esp_err_t event_send_state(event_state_t st)
{
    if (!s_q || st >= EVENT_STATE_COUNT) return ESP_ERR_INVALID_ARG;
    
    // Non-blocking send with short timeout
    return xQueueSend(s_q, &st, pdMS_TO_TICKS(5)) ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t event_send_state_immediate(event_state_t st)
{
    if (st >= EVENT_STATE_COUNT || !s_tim) return ESP_ERR_INVALID_ARG;
    if (s_active) return ESP_ERR_INVALID_STATE;
    
    // Direct pulse generation, bypasses queue
    return start_pulse_us(EVENT_WIDTH_US[st]);
}

uint32_t event_get_queue_waiting(void)
{
    return s_q ? uxQueueMessagesWaiting(s_q) : 0;
}

esp_err_t event_deinit(void)
{
    if (s_task) { 
        vTaskDelete(s_task); 
        s_task = NULL; 
    }
    if (s_q) { 
        vQueueDelete(s_q); 
        s_q = NULL; 
    }
    if (s_tim) { 
        gptimer_stop(s_tim); 
        gptimer_disable(s_tim); 
        gptimer_del_timer(s_tim); 
        s_tim = NULL; 
    }
    if (s_done) { 
        vSemaphoreDelete(s_done); 
        s_done = NULL; 
    }
    if (s_pin != GPIO_NUM_NC) {
        gpio_set_level(s_pin, GPIO_IDLE_LEVEL);
    }
    
    return ESP_OK;
}