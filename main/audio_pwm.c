#include "freertos/FreeRTOS.h"
#include "driver/ledc.h"

// Define LEDC parameters using the low-speed mode
#define LEDC_TIMER              LEDC_TIMER_0
#define LEDC_MODE               LEDC_LOW_SPEED_MODE   // Use low speed mode
#define LEDC_CHANNEL_0          LEDC_CHANNEL_0        // Channel for GPIO 47
#define LEDC_CHANNEL_1          LEDC_CHANNEL_1        // Channel for GPIO 48
#define LEDC_DUTY_RES           LEDC_TIMER_13_BIT     // 13-bit duty resolution
#define LEDC_MAX_DUTY           ((1 << LEDC_DUTY_RES) - 1)  
#define TONE_GPIO_1             47                    // PWM output on GPIO 47
#define TONE_GPIO_2             48                    // PWM output on GPIO 48

static void init_ledc(uint32_t frequency) {
    // Configure the timer (shared by both channels)
    ledc_timer_config_t ledc_timer = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,  // Use low speed mode
        .timer_num       = LEDC_TIMER,
        .duty_resolution = LEDC_DUTY_RES,
        .freq_hz         = frequency,         // Tone frequency
        .clk_cfg         = LEDC_AUTO_CLK
    };
    ledc_timer_config(&ledc_timer);

    // Configure channel 0 for GPIO 47
    ledc_channel_config_t ledc_channel_0 = {
        .speed_mode = LEDC_LOW_SPEED_MODE,       // Use low speed mode
        .channel    = LEDC_CHANNEL_0,
        .timer_sel  = LEDC_TIMER,
        .intr_type  = LEDC_INTR_DISABLE,
        .gpio_num   = TONE_GPIO_1,
        .duty       = LEDC_MAX_DUTY / 2,          // 50% duty cycle for a square wave
        .hpoint     = 0
    };
    ledc_channel_config(&ledc_channel_0);

    // Configure channel 1 for GPIO 48
    ledc_channel_config_t ledc_channel_1 = {
        .speed_mode = LEDC_LOW_SPEED_MODE,       // Use low speed mode
        .channel    = LEDC_CHANNEL_1,
        .timer_sel  = LEDC_TIMER,
        .intr_type  = LEDC_INTR_DISABLE,
        .gpio_num   = TONE_GPIO_2,
        .duty       = LEDC_MAX_DUTY / 2,          // 50% duty cycle for a square wave
        .hpoint     = 0
    };
    ledc_channel_config(&ledc_channel_1);
}

void play_tone(uint32_t tone_frequency, uint32_t duration_ms) {
    init_ledc(tone_frequency);
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    
    // Stop both channels
    ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
    ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, 0);
}