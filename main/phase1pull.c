#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/projdefs.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "lvgl.h"
#include "lv_conf.h"
#include "audio_pwm.c"
#include "graphics.h"
#include "mcpcommands.c"
#include "peripheral_config.c"
#include "encoder_out.h"
#include "driver/ledc.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_rom_sys.h"
#include "font/lv_font.h"
#include <string.h>

#define TAG "PHASE1_TASK"

#define GPIO_REWARD_SIGNAL 3
#define GPIO_OUTPUT_PIN_MASK (1ULL<<GPIO_REWARD_SIGNAL)
#define ENCODER_THRESHOLD 25 // vchanged from 40
#define HOLD_TIME_MS 0
#define REWARD_DURATION_MS 500
#define RESET_DELAY_MS 1000
#define TRIAL_TIMEOUT_MS 3000
#define STACK_SIZE 16384
#define UI_TASK_PERIOD 10
#define SCREEN_WIDTH         1024
#define SCREEN_HEIGHT        600
#define GRATING_STRIPE_WIDTH 40
#define ENCODER_DIRECTION    -1

// Trial outcome definitions
typedef enum {
    TRIAL_CORRECT = 0,
    TRIAL_INCORRECT,
    TRIAL_TIMEOUT
} trial_outcome_t;

SemaphoreHandle_t encoder_mutex = NULL;
volatile int32_t current_encoder_value = 0;
lv_obj_t *lever_indicator = NULL;
lv_obj_t *grating_container = NULL;
lv_obj_t *trial_info_label = NULL;
volatile bool show_grating = true;

// Trial tracking variables
static uint32_t trial_number = 0;
static uint32_t session_correct = 0;
static uint32_t session_total = 0;

// Function to send trial data to PC via serial
static void send_trial_data(trial_outcome_t outcome, uint32_t reaction_time_ms, int32_t encoder_position) {
    const char* outcome_str;
    switch (outcome) {
        case TRIAL_CORRECT: outcome_str = "CORRECT"; break;
        case TRIAL_INCORRECT: outcome_str = "INCORRECT"; break;
        case TRIAL_TIMEOUT: outcome_str = "TIMEOUT"; break;
        default: outcome_str = "UNKNOWN"; break;
    }
    
    // Send data in CSV format to Python GUI
    printf("TRIAL,%s,%lu,%ld\n", outcome_str, reaction_time_ms, encoder_position);
    
    // Also log locally for debugging
    ESP_LOGI(TAG, "Trial %lu: %s, RT=%lums, Pos=%ld", 
             trial_number, outcome_str, reaction_time_ms, encoder_position);
}

// Function to update on-screen trial information
static void update_trial_display(void) {
    if (trial_info_label && lvgl_lock(10)) {
        float success_rate = session_total > 0 ? (float)session_correct / session_total * 100.0f : 0.0f;
        lv_label_set_text_fmt(trial_info_label, 
                              "Trial: %lu\nCorrect: %lu/%lu\nSuccess: %.1f%%", 
                              trial_number, session_correct, session_total, success_rate);
        lvgl_unlock();
    }
}

static void pulse_reward_ttl() {
    gpio_set_level(GPIO_REWARD_SIGNAL, 1);
    vTaskDelay(pdMS_TO_TICKS(REWARD_DURATION_MS));
    gpio_set_level(GPIO_REWARD_SIGNAL, 0);
}

static esp_err_t setup_gpio(void) {
    gpio_config_t io_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = GPIO_OUTPUT_PIN_MASK,
        .intr_type = GPIO_INTR_DISABLE,
        .pull_down_en = 0,
        .pull_up_en = 0
    };
    return gpio_config(&io_conf);
}

static void create_grating_pattern(lv_obj_t *parent) {
    grating_container = lv_obj_create(parent);
    lv_obj_remove_style_all(grating_container);
    lv_obj_set_size(grating_container, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_pos(grating_container, 0, 0);
    lv_obj_set_style_bg_color(grating_container, lv_color_hex(0x000000), 0);

    int num_stripes = SCREEN_WIDTH / GRATING_STRIPE_WIDTH;
    for (int i = 0; i < num_stripes; i += 2) {
        lv_obj_t *stripe = lv_obj_create(grating_container);
        lv_obj_remove_style_all(stripe);
        lv_obj_set_size(stripe, GRATING_STRIPE_WIDTH, SCREEN_HEIGHT);
        lv_obj_set_pos(stripe, i * GRATING_STRIPE_WIDTH, 0);
        lv_obj_set_style_bg_color(stripe, lv_color_hex(0x00FF00), 0);
        lv_obj_set_style_bg_opa(stripe, LV_OPA_COVER, 0);
    }
}

static void create_simple_ui(lv_display_t *display) {
    lv_obj_t *scr = lv_disp_get_scr_act(display);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    
    create_grating_pattern(scr);
    
    // Create lever indicator
    lever_indicator = lv_obj_create(scr);
    lv_obj_remove_style_all(lever_indicator);
    lv_obj_set_size(lever_indicator, 50, 200);
    lv_obj_set_style_bg_color(lever_indicator, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(lever_indicator, LV_OPA_COVER, 0);
    lv_obj_set_pos(lever_indicator, SCREEN_WIDTH/2 - 25, SCREEN_HEIGHT/2 - 100);
    
    // Create trial information display
    trial_info_label = lv_label_create(scr);
    lv_obj_set_pos(trial_info_label, 20, 20);
    lv_obj_set_style_text_color(trial_info_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_color(trial_info_label, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(trial_info_label, LV_OPA_70, 0);
    lv_obj_set_style_pad_all(trial_info_label, 10, 0);
    lv_label_set_text(trial_info_label, "Trial: 0\nCorrect: 0/0\nSuccess: 0.0%");
}

static void hide_grating() {
    if (grating_container && lvgl_lock(10)) {
        lv_obj_add_flag(grating_container, LV_OBJ_FLAG_HIDDEN);
        show_grating = false;
        lvgl_unlock();
    }
}

static void show_grating_pattern() {
    if (grating_container && lvgl_lock(10)) {
        lv_obj_clear_flag(grating_container, LV_OBJ_FLAG_HIDDEN);
        show_grating = true;
        lvgl_unlock();
    }
}

void encoder_read_task(void *pvParameters) {
    TickType_t last_wake_time = xTaskGetTickCount();
    while (1) {
        int32_t encoder_value = (int32_t)read_encoder_value(16);
        if (encoder_mutex) {
            xSemaphoreTake(encoder_mutex, portMAX_DELAY);
            current_encoder_value = encoder_value;
            encoder_out_update(encoder_value);
            xSemaphoreGive(encoder_mutex);
        }
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(5));
    }
}

void ui_update_task(void *pvParameters) {
    TickType_t last_wake_time = xTaskGetTickCount();
    while (1) {
        int32_t encoder_value = 0;
        if (encoder_mutex) {
            xSemaphoreTake(encoder_mutex, portMAX_DELAY);
            encoder_value = current_encoder_value;
            xSemaphoreGive(encoder_mutex);
        }
        
        int32_t screen_center = SCREEN_WIDTH / 2;
        int32_t screen_val = screen_center + ((encoder_value * ENCODER_DIRECTION * (SCREEN_WIDTH / 2 - 25)) / 200);
        screen_val = LV_CLAMP(screen_val, 25, SCREEN_WIDTH - 25);
        
        if (lvgl_lock(10)) {
            if (lever_indicator) {
                lv_obj_set_x(lever_indicator, screen_val - 25);
            }
            lv_timer_handler();
            lvgl_unlock();
        }
        
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(UI_TASK_PERIOD));
    }
}

static void play_audio_and_visual_cue(uint32_t freq, uint32_t duration_ms) {
    if (lvgl_lock(10)) {
        show_grating_pattern();
        lvgl_unlock();
    }
    init_ledc(freq);
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
    ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, 0);
    if (lvgl_lock(10)) {
        hide_grating();
        lvgl_unlock();
    }
}

void simplified_trial_task(void *pvParameters) {
    setup_gpio();
    
    ESP_LOGI(TAG, "Starting behavioral task trials...");
    printf("ESP32_READY\n"); // Signal to PC that ESP32 is ready
    
    while (1) {
        trial_number++;
        session_total++;
        
        ESP_LOGI(TAG, "Starting trial %lu", trial_number);
        
        reset_position();
        vTaskDelay(pdMS_TO_TICKS(1000));
        unlock_lever(100);
        
        // Record trial start time
        TickType_t trial_start_time = xTaskGetTickCount();
        
        // Play audio and visual cue
        play_audio_and_visual_cue(3000, 500);

        TickType_t cue_end_time = xTaskGetTickCount();
        TickType_t hold_start = 0;
        bool rewarded = false;
        trial_outcome_t outcome = TRIAL_TIMEOUT;

        while (!rewarded && (xTaskGetTickCount() - cue_end_time < pdMS_TO_TICKS(TRIAL_TIMEOUT_MS))) {
            int32_t enc_val = 0;
            if (encoder_mutex) {
                xSemaphoreTake(encoder_mutex, portMAX_DELAY);
                enc_val = current_encoder_value;
                xSemaphoreGive(encoder_mutex);
            }
            
            if (enc_val <= -ENCODER_THRESHOLD) {
                if (hold_start == 0) hold_start = xTaskGetTickCount();
                if (xTaskGetTickCount() - hold_start >= pdMS_TO_TICKS(HOLD_TIME_MS)) {
                    play_tone(5000, 300);
                    pulse_reward_ttl();
                    rewarded = true;
                    outcome = TRIAL_CORRECT;
                    session_correct++;
                }
            } else {
                hold_start = 0;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        
        // Calculate reaction time
        TickType_t trial_end_time = xTaskGetTickCount();
        uint32_t reaction_time_ms = pdTICKS_TO_MS(trial_end_time - cue_end_time);
        
        // Get final encoder position
        int32_t final_encoder_pos = 0;
        if (encoder_mutex) {
            xSemaphoreTake(encoder_mutex, portMAX_DELAY);
            final_encoder_pos = current_encoder_value;
            xSemaphoreGive(encoder_mutex);
        }
        
        // Send trial data to PC
        send_trial_data(outcome, reaction_time_ms, final_encoder_pos);
        
        // Update local display
        update_trial_display();
        
        // Log trial completion
        ESP_LOGI(TAG, "Trial %lu completed: %s", trial_number, 
                 outcome == TRIAL_CORRECT ? "CORRECT" : 
                 outcome == TRIAL_INCORRECT ? "INCORRECT" : "TIMEOUT");
        
        vTaskDelay(pdMS_TO_TICKS(RESET_DELAY_MS));
    }
}

void app_main(void) {
    esp_log_level_set(TAG, ESP_LOG_INFO);
    ESP_LOGI(TAG, "Starting phase 1 pull task with PC logging");
    
    // Initialize UART for communication with PC
    ESP_ERROR_CHECK(uart_param_config(uart_num, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(uart_num, 25, 24, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(uart_num, 256, 0, 0, NULL, 0));
    
    // Create encoder mutex
    encoder_mutex = xSemaphoreCreateMutex();
    if (!encoder_mutex) {
        ESP_LOGE(TAG, "Failed to create encoder mutex");
        return;
    }
    
    // Initialize display
    lv_display_t *display = lcd_init();
    if (!display) {
        ESP_LOGE(TAG, "Failed to initialize LCD");
        return;
    }
    bsp_set_lcd_backlight(1);
    
    // Create UI
    if (lvgl_lock(100)) {
        create_simple_ui(display);
        lv_timer_handler();
        lvgl_unlock();
    }
    
    // Initialize encoder and position
    reset_encoder_counter();
    reset_position();
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Initialize encoder output
    ESP_ERROR_CHECK(encoder_out_init());
    
    // Create tasks
    xTaskCreate(encoder_read_task, "encoder_task", STACK_SIZE, NULL, configMAX_PRIORITIES - 4, NULL);
    xTaskCreatePinnedToCore(ui_update_task, "ui_task", STACK_SIZE, NULL, configMAX_PRIORITIES - 2, NULL, 1);
    xTaskCreate(simplified_trial_task, "trial_task", STACK_SIZE, NULL, configMAX_PRIORITIES - 6, NULL);
    
    ESP_LOGI(TAG, "All tasks created successfully");
    ESP_LOGI(TAG, "Connect to PC via serial to start logging data");
}