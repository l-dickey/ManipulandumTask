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

// Define our GPIO Pins for use
#define GPIO_REWARD_SIGNAL   3 // GPIO pin connected to the syringe pump
#define GPIO_EVENT_PIN        4 // GPIO pin for 8 bit encoding

#define GPIO_OUTPUT_PIN_MASK ((1ULL<<GPIO_EVENT_PIN) |(1ULL<<GPIO_REWARD_SIGNAL)) // make the pins outputs

// Define our mapping parameters
#define ENCODER_MAX_RANGE    200      // Maximum encoder count for lever range
#define SCREEN_WIDTH         1024     // Screen width (pixels)
#define SCREEN_HEIGHT        600      // Screen height (pixels)
#define INDICATOR_SIZE       30       // Size of the indicator circle (pixels)
#define CONDITION_SIZE       80       // Size of condition circles (pixels) - INCREASED SIZE
#define INDICATOR_START_X    (SCREEN_WIDTH / 2)  // Center X position
#define INDICATOR_Y_POS      (SCREEN_HEIGHT / 2) // Vertical center position

// Condition circle positions (offset from center by approximately 200 encoder counts)
#define CONDITION_OFFSET     50      // ~200 encoder counts for condition circles
#define CONDITION_SCREEN_OFFSET ((CONDITION_OFFSET * (SCREEN_WIDTH / 2 - INDICATOR_SIZE)) / ENCODER_MAX_RANGE)

// Task parameters
#define STACK_SIZE          4096
#define ENCODER_TASK_PERIOD  5        // 5ms period (200Hz) - INCREASED FREQUENCY
#define UI_TASK_PERIOD       10        // 5ms period (200Hz) for responsive UI - INCREASED FREQUENCY
#define STATE_TASK_PERIOD    100        // 100ms period
#define REWARD_DURATION_MS   500       // 500ms to signal the syringe pump via TTL
#define TRIAL_TIMEOUT_MS     3500  // 3.5 seconds
#define ENCODER_DIR          -1 // invert the direction of the encoder values for push/pull


// // Define LEDC parameters, used here for audio generation via PWMs
// #define LEDC_TIMER              LEDC_TIMER_0
// #define LEDC_MODE               LEDC_HIGH_SPEED_MODE
// #define LEDC_CHANNEL            LEDC_CHANNEL_0
// #define LEDC_DUTY_RES           LEDC_TIMER_13_BIT    // 13-bit duty resolution
// #define LEDC_MAX_DUTY           ((1 << LEDC_DUTY_RES) - 1)  // For 13-bit, max duty is 8191
// #define TONE_GPIO               48                   // PWM output on GPIO 48


// Global variables
static const char *TAG = "LEVER_TEST";
SemaphoreHandle_t encoder_mutex = NULL;
volatile int32_t current_encoder_value = 0;
lv_obj_t *lever_indicator = NULL;
lv_obj_t *nonreward_circle = NULL;
// lv_obj_t *condition_circle_push = NULL;  // Forward/push condition circle
// lv_obj_t *condition_circle_pull = NULL;  // Backward/pull condition circle
lv_style_t indicator_style;
lv_style_t condition_style;
lv_style_t condition_active_style;

// -------------------
// Trial State Machine
typedef enum {
    TRIAL_CONFIG,    // Start for setting params, won't show up in state encoding    
    TRIAL_INIT,      // Reset lever to center and prepare for new trial
    TRIAL_CUE,       // Show only reward circle briefly
    TRIAL_SETUP,     // Set up trial conditions (hide cue, show both circles)
    TRIAL_ACTIVE,    // Lever is free to move
    TRIAL_COMPLETE,  // Condition reached, trial finished
    REWARD_PERIOD,   // Reward dispensed
    NON_REWARD_PERIOD, // Non-reward for same duration as reward
    TRIAL_RESET,      // Reset lever to center for next trial
    SESSION_END        // End of the trial session
} trial_state_t;

typedef enum {
    EVT_TRIAL_START,
    EVT_CUE,
    EVT_TRIAL_ACTIVE,
    EVT_TRIAL_END,
    EVT_REWARD,
    EVT_NON_REWARD,
    EVT_TRIAL_RESET,
    EVT_SESSION_END
} event_type_t;

static const uint32_t EVENT_WIDTH_US[] = {
    [EVT_TRIAL_START]   =   100,
    [EVT_CUE]           =   175,
    [EVT_TRIAL_ACTIVE]  =   225,
    [EVT_TRIAL_END]     =   300,
    [EVT_REWARD]        =   375,
    [EVT_NON_REWARD]    =   425,
    [EVT_TRIAL_RESET]   =   500,
    [EVT_SESSION_END]   =   1000
};

volatile trial_state_t trial_state = TRIAL_INIT;
volatile bool trial_success = false;
lv_obj_t *reward_circle = NULL;

// Animation flag (used by animation callback and UI task)
bool animation_active = false;
int32_t animation_target = 0;

typedef enum {
    REWARD_FIXED_PUSH,
    REWARD_FIXED_PULL,
    REWARD_RANDOM
} reward_mode_t;

typedef struct {
    bool           reward_color_is_green; // true=green reward, false=purple
    reward_mode_t  reward_mode;           // fixed push/pull or random
    int32_t        reward_x_pos;          // computed X for reward circle
    int32_t        nonreward_x_pos;       // opposite side
} RewardConfig;


// Initialize with default values. For example, if fixed and reward is on the left:
RewardConfig reward_config = {
    .reward_color_is_green = true,          // Green is the reward color
    .reward_mode = REWARD_FIXED_PUSH,       // Default to fixed left
    .reward_x_pos = INDICATOR_START_X - CONDITION_SCREEN_OFFSET,  // Reward circle on top(push)
    .nonreward_x_pos = INDICATOR_START_X + CONDITION_SCREEN_OFFSET  // Nonreward on bottom
};

typedef struct {
    int num_trials;              // Total number of trials in the session
    int current_trial;           // Current trial index (0-indexed)
    bool use_serial_config;      // Whether to use serial configuration
    lv_obj_t *trial_counter_label;
    RewardConfig reward_config;  // Reward configuration structure
} SessionConfig;

// Initialize with default values
SessionConfig session = {
    .num_trials = 20,            // Default: 20 trials
    .current_trial = 0,          // Start at trial 0
    .use_serial_config = true,   // Try to use serial config by default
    .reward_config = {
        .reward_color_is_green = true,
        .reward_x_pos = INDICATOR_START_X - CONDITION_SCREEN_OFFSET,
        .nonreward_x_pos = INDICATOR_START_X + CONDITION_SCREEN_OFFSET
    }
};

// Function prototypes
void encoder_read_task(void *pvParameters);
void ui_update_task(void *pvParameters);
void trial_state_task(void *pvParameters);
static void create_lever_ui(lv_display_t *display);
void play_tone(uint32_t tone_frequency, uint32_t duration_ms);
static inline void pulse_event_us(uint32_t width_us);
static void log_and_pulse(event_type_t evt, int trial_num);
void set_motor_position(int32_t position);
void start_animation_to_position(int32_t position);
static void animation_callback(void *var, int32_t value);
static void highlight_condition_circle( bool active);
bool check_circle_collision(int32_t x1, int32_t y1, int32_t r1, int32_t x2, int32_t y2, int32_t r2);
void randomize_condition_circle_positions(void);
void update_trial_counter(void);
void update_circle_styles(void);

// GPIO Setup
static esp_err_t setup_gpio(void) { // set up the GPIO pins according to the GPIO pin MASK (l.33)
    gpio_config_t io_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = GPIO_OUTPUT_PIN_MASK,
        .intr_type = GPIO_INTR_DISABLE,
        .pull_down_en = 0,
        .pull_up_en = 0
    };
    return gpio_config(&io_conf);
}

static inline void pulse_event_us(uint32_t width_us) {
    gpio_set_level(GPIO_EVENT_PIN, 1);
    esp_rom_delay_us(width_us);
    gpio_set_level(GPIO_EVENT_PIN, 0);
}


//----------------------
static char wait_for_user_input(int timeout_ms) {
    uint8_t data;
    int len = uart_read_bytes(UART_NUM_0, &data, 1, pdMS_TO_TICKS(timeout_ms));
    if (len > 0) {
        if (data >= 'a' && data <= 'z') data -= ('a' - 'A');
        return (char)data;
    }
    return '\0';
}

static void wait_for_config(void) {
    char    buf[128];
    size_t  idx = 0;
    uint8_t ch;

    printf("READY_FOR_CONFIG\n");

    // Read UART input until newline
    while (1) {
        int r = uart_read_bytes(UART_NUM_0, &ch, 1, pdMS_TO_TICKS(100));
        if (r > 0) {
            if (ch == '\r') continue;
            if (idx < sizeof(buf) - 1)
                buf[idx++] = (char)ch;
            if (ch == '\n') {
                buf[idx] = '\0';
                break;
            }
        }
    }

    printf("Received config string: %s\n", buf);

    // Temporary holding vars
    char mode = 'X';  // Default to random
    char side = 'L';  // Default side if needed (only for MODE=F)

    char *tok = strtok(buf, ";");
    while (tok) {
        printf("Parsed token: %s\n", tok);

        if (strncmp(tok, "TRIALS=", 7) == 0) {
            int trials = atoi(tok + 7);
            if (trials > 0 && trials <= 999) {
                session.num_trials = trials;
                printf("Set trials: %d\n", trials);
            }
        }
        else if (strncmp(tok, "COLOR=", 6) == 0) {
            session.reward_config.reward_color_is_green = (tok[6] == 'G');
            printf("Set reward color: %s\n", session.reward_config.reward_color_is_green ? "Green" : "Purple");
        }
        else if (strncmp(tok, "MODE=", 5) == 0) {
            mode = tok[5];
            printf("Parsed MODE: %c\n", mode);
        }
        else if (strncmp(tok, "SIDE=", 5) == 0) {
            side = tok[5];
            printf("Parsed SIDE: %c\n", side);
        }

        tok = strtok(NULL, ";");
    }


    // Apply mode
    if (mode == 'X') {
        session.reward_config.reward_mode = REWARD_RANDOM;
        printf("Reward mode: RANDOM\n");
    }
    else if (mode == 'F') {
        if (side == 'L') {
            session.reward_config.reward_mode = REWARD_FIXED_PUSH;
            session.reward_config.reward_x_pos     = INDICATOR_START_X - CONDITION_SCREEN_OFFSET;
            session.reward_config.nonreward_x_pos  = INDICATOR_START_X + CONDITION_SCREEN_OFFSET;
            printf("Reward mode: FIXED PUSH\n");
        }
        else if (side == 'R') {
            session.reward_config.reward_mode = REWARD_FIXED_PULL;
            session.reward_config.reward_x_pos     = INDICATOR_START_X + CONDITION_SCREEN_OFFSET;
            session.reward_config.nonreward_x_pos  = INDICATOR_START_X - CONDITION_SCREEN_OFFSET;
            printf("Reward mode: FIXED PULL\n");
        }
        else {
            // Invalid side value
            session.reward_config.reward_mode = REWARD_RANDOM;
            printf("Invalid SIDE with MODE=F → defaulting to RANDOM\n");
        }
    }
    else {
        // Invalid mode value
        session.reward_config.reward_mode = REWARD_RANDOM;
        printf("Invalid MODE value → defaulting to RANDOM\n");
    }

    printf("Final config → Trials: %d, Color: %s, Mode: %s\n",
        session.num_trials,
        session.reward_config.reward_color_is_green ? "Green" : "Purple",
        (session.reward_config.reward_mode == REWARD_RANDOM) ? "Random" :
        (session.reward_config.reward_mode == REWARD_FIXED_PUSH) ? "Fixed Push" :
        (session.reward_config.reward_mode == REWARD_FIXED_PULL) ? "Fixed Pull" : "Unknown");

    update_circle_styles();
    printf("ACK\n");
}

static void log_and_pulse(event_type_t evt, int trial_num) {
    uint64_t t_us = esp_timer_get_time();

    const char *evt_name =
        (evt==EVT_TRIAL_START)   ? "START"       :
        (evt==EVT_CUE)           ? "CUE"         :
        (evt==EVT_TRIAL_ACTIVE)  ? "ACTIVE"      :
        (evt==EVT_TRIAL_END)     ? "END"         :
        (evt==EVT_REWARD)        ? "REWARD"      :
        (evt==EVT_NON_REWARD)    ? "NON_REWARD"  :
        (evt==EVT_TRIAL_RESET)   ? "RESET"       : "UNKNOWN";

    if (evt == EVT_CUE) {
        const char *color = session.reward_config.reward_color_is_green
                            ? "GREEN" : "PURPLE";
        printf("EVENT,%s,%d,%s,%" PRIu64 "\n",
               evt_name, trial_num, color, t_us);
    } else {
        printf("EVENT,%s,%d,%" PRIu64 "\n",
               evt_name, trial_num, t_us);
    }

    pulse_event_us(EVENT_WIDTH_US[evt]);
}

//----------------------
// Check collision between two circles
bool check_circle_collision(int32_t x1, int32_t y1, int32_t r1, int32_t x2, int32_t y2, int32_t r2) {
    int32_t dx = x1 - x2;
    int32_t dy = y1 - y2;
    int32_t distance_squared = dx * dx + dy * dy;
    int32_t radius_sum = r1 + r2;
    
    return distance_squared <= (radius_sum * radius_sum);
}

//----------------------
// Motor position update callback (called from animation)
static void animation_callback(void *var, int32_t value) {
    // Update UI indicator position
    if (lvgl_lock(2)) {
        lv_obj_set_x(lever_indicator, value);
        lv_obj_move_foreground(lever_indicator);  // Always bring to front
        lvgl_unlock();
    } else {
        ESP_LOGW(TAG, "failed to acquire LVGL mutex for updates anim_cb");
    }
    
    // Convert screen coordinate back to encoder position
    int32_t screen_center = SCREEN_WIDTH / 2;
    int32_t screen_offset = value - screen_center;
    int32_t encoder_position = (screen_offset * ENCODER_MAX_RANGE) / (SCREEN_WIDTH / 2 - INDICATOR_SIZE);
    
    // Update motor position
    set_motor_position(encoder_position);
    
    // Update our stored encoder value
    if (encoder_mutex != NULL) {
        xSemaphoreTake(encoder_mutex, portMAX_DELAY);
        current_encoder_value = encoder_position;
        xSemaphoreGive(encoder_mutex);
    }
}

//----------------------
// Start an animation to move the lever to a target position
void start_animation_to_position(int32_t position) {
    // Convert encoder position to screen coordinate
    int32_t screen_center = SCREEN_WIDTH / 2;
    int32_t screen_value = screen_center + (ENCODER_DIR * ((position * (SCREEN_WIDTH/2 - INDICATOR_SIZE)) / ENCODER_MAX_RANGE));

    // Clamp to screen bounds
    if (screen_value < INDICATOR_SIZE / 2) screen_value = INDICATOR_SIZE / 2;
    if (screen_value > SCREEN_WIDTH - INDICATOR_SIZE / 2) screen_value = SCREEN_WIDTH - INDICATOR_SIZE / 2;
    
    animation_active = true;
    animation_target = position;
    
    if (lvgl_lock(25)) {  // Reduced lock time
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, lever_indicator);
        lv_anim_set_values(&a, lv_obj_get_x(lever_indicator), screen_value);
        lv_anim_set_time(&a, 300);  // Animation duration: 300ms (faster)
        lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)animation_callback);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_start(&a);
        lvgl_unlock();
    } else {
        ESP_LOGW(TAG, "failed to acquire LVGL mutex for updates s_a_t_p");
    }

}

//----------------------
// Set motor position using mcpcommands.c functions
void set_motor_position(int32_t position) {
    // Clamp position to safe range
    if (position > ENCODER_MAX_RANGE) position = ENCODER_MAX_RANGE;
    if (position < -ENCODER_MAX_RANGE) position = -ENCODER_MAX_RANGE;
    
    // Update motor position (using drive_M1 from mcpcommands.c)
    drive_M1(800, 500, 800, position);
    
    static int log_counter = 0;
    if (++log_counter >= 100) {  // Reduced logging frequency
        ESP_LOGI(TAG, "Setting motor position to: %" PRId32, position);
        log_counter = 0;
    }
}


//----------------------
// Task to read encoder values
void encoder_read_task(void *pvParameters) {
    TickType_t last_wake_time = xTaskGetTickCount();
    
    // ESP_LOGI(TAG, "Encoder reading task started"); Uncomment for debugging
    
    while (1) {
        int32_t encoder_value = (int32_t)read_encoder_value(16);
        
        if (encoder_mutex != NULL) {
            xSemaphoreTake(encoder_mutex, portMAX_DELAY);
            current_encoder_value = encoder_value;
            encoder_out_update(encoder_value);
            xSemaphoreGive(encoder_mutex);
        }
        
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(ENCODER_TASK_PERIOD));
    }
}

//----------------------
/// Highlight (or un-highlight) the single reward circle
static void highlight_condition_circle(bool active) {
    if (!lvgl_lock(10)) {
        ESP_LOGW(TAG, "failed to acquire LVGL mutex in highlight_condition_circle");
        return;
    }

    if (active) {
        lv_obj_add_style(reward_circle, &condition_active_style, 0);
    } else {
        lv_obj_remove_style(reward_circle, &condition_active_style, 0);
    }

    lvgl_unlock();
}

// Function to update styles based on reward configuration
static void update_reward_styles() {
    if (!lvgl_lock(10)) {
        ESP_LOGE(TAG, "Failed to acquire LVGL mutex in update_reward_styles");
        return;
    }

    // Pick the reward color
    lv_color_t reward_color = session.reward_config.reward_color_is_green
                                ? lv_color_hex(0x00C000)
                                : lv_color_hex(0x800080);

    // Apply it to the reward_circle
    lv_obj_set_style_bg_color(reward_circle, reward_color, 0);

    // And update the 'active' highlight style to match
    lv_style_set_bg_color(&condition_active_style, reward_color);

    lvgl_unlock();
}

void update_circle_styles(void) {
    if (!lvgl_lock(10)) {
        ESP_LOGE(TAG, "Failed to acquire LVGL mutex in update_circle_styles");
        return;
    }

    // Dynamic color selection based on reward color
    lv_color_t reward_color    = session.reward_config.reward_color_is_green 
                                 ? lv_color_hex(0x00C000)  // Green
                                 : lv_color_hex(0x800080); // Purple

    lv_color_t nonreward_color = session.reward_config.reward_color_is_green 
                                 ? lv_color_hex(0x800080)  // Purple
                                 : lv_color_hex(0x00C000); // Green

    // Apply to circles
    lv_obj_set_style_bg_color(reward_circle, reward_color, 0);
    lv_obj_set_style_bg_color(nonreward_circle, nonreward_color, 0);

    // Update highlight style to match reward color
    lv_style_set_bg_color(&condition_active_style, reward_color);
    lv_style_set_border_color(&condition_active_style, lv_color_hex(0xFFFFFF));

    lvgl_unlock();
}

//----------------------
// create the ui for the lever
static void create_lever_ui(lv_display_t *display) {
    lv_obj_t *scr = lv_disp_get_scr_act(display);
    // Black background
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);

    // --- Lever indicator style ---
    lv_style_init(&indicator_style);
    lv_style_set_radius(&indicator_style, LV_RADIUS_CIRCLE);
    lv_style_set_bg_color(&indicator_style, lv_color_hex(0xFFFFFF));
    lv_style_set_bg_opa(&indicator_style, 255);
    lv_style_set_border_width(&indicator_style, 2);
    lv_style_set_border_color(&indicator_style, lv_color_hex(0xFFFFFF));

    lever_indicator = lv_obj_create(scr);
    lv_obj_move_foreground(lever_indicator);
    lv_obj_remove_style_all(lever_indicator);
    lv_obj_add_style(lever_indicator, &indicator_style, 0);
    lv_obj_set_size(lever_indicator, INDICATOR_SIZE, INDICATOR_SIZE);
    lv_obj_set_pos(lever_indicator,
                   INDICATOR_START_X - INDICATOR_SIZE/2,
                   INDICATOR_Y_POS   - INDICATOR_SIZE/2);

    // --- Base circle style (for both circles) ---
    lv_style_init(&condition_style);
    lv_style_set_radius(&condition_style, LV_RADIUS_CIRCLE);
    lv_style_set_bg_opa(&condition_style, 255);
    lv_style_set_border_width(&condition_style, 2);
    lv_style_set_border_color(&condition_style, lv_color_hex(0x808080));

    // --- Highlight style (border + fill for correct circle) ---
    lv_style_init(&condition_active_style);
    lv_style_set_radius(&condition_active_style, LV_RADIUS_CIRCLE);
    lv_style_set_bg_opa(&condition_active_style, 255);
    lv_style_set_border_width(&condition_active_style, 3);
    lv_style_set_border_color(&condition_active_style, lv_color_hex(0xFFFFFF));
    // fill color for this style will be set in update_circle_styles()

    // --- Reward circle ---
    reward_circle = lv_obj_create(scr);
    lv_obj_remove_style_all(reward_circle);
    lv_obj_add_style(reward_circle, &condition_style, 0);
    lv_obj_set_size(reward_circle, CONDITION_SIZE, CONDITION_SIZE);

    // --- Non-reward circle ---
    nonreward_circle = lv_obj_create(scr);
    lv_obj_remove_style_all(nonreward_circle);
    lv_obj_add_style(nonreward_circle, &condition_style, 0);
    lv_obj_set_size(nonreward_circle, CONDITION_SIZE, CONDITION_SIZE);

    // Do not position here; trial code will place them each trial.
    // Hide both until needed:
    lv_obj_add_flag(reward_circle,    LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(nonreward_circle, LV_OBJ_FLAG_HIDDEN);

    // Apply initial colors/highlight (green/purple) to each
    update_circle_styles();

    // --- Center start marker ---
    lv_obj_t *center = lv_obj_create(scr);
    lv_obj_remove_style_all(center);
    lv_obj_set_style_bg_color(center, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(center, 200, 0);
    lv_obj_set_size(center, 2, 40);
    lv_obj_set_pos(center,
                   SCREEN_WIDTH/2 - 1,
                   INDICATOR_Y_POS - 20);

    // --- Trial counter label ---
    session.trial_counter_label = lv_label_create(scr);
    lv_obj_set_style_text_color(session.trial_counter_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(session.trial_counter_label, &lv_font_montserrat_12, 0); // Small font
    lv_label_set_text_fmt(session.trial_counter_label, "Trial: 0/%d", session.num_trials);
    lv_obj_align(session.trial_counter_label, LV_ALIGN_TOP_RIGHT, -10, 10); // Top-right corner

    ESP_LOGI(TAG, "UI creation complete");
}


//----------------------
// Task to update UI based on encoder position
// UI only ever moves the lever indicator now — no need to touch the reward circle here.
void ui_update_task(void *pvParameters) {
    TickType_t last_wake_time = xTaskGetTickCount();
    int32_t    last_encoder_value = 0;

    ESP_LOGI(TAG, "UI update task started");

    while (1) {
        if (!animation_active) {
            int32_t encoder_value = 0;
            if (encoder_mutex) {
                xSemaphoreTake(encoder_mutex, portMAX_DELAY);
                encoder_value = current_encoder_value;
                xSemaphoreGive(encoder_mutex);
            }

            if (encoder_value != last_encoder_value) {
                int32_t screen_center = SCREEN_WIDTH / 2;
                int32_t screen_val = screen_center
                            + (ENCODER_DIR * ((encoder_value * (SCREEN_WIDTH/2 - INDICATOR_SIZE))
                            / ENCODER_MAX_RANGE));

                screen_val = LV_CLAMP(screen_val, INDICATOR_SIZE/2, SCREEN_WIDTH - INDICATOR_SIZE/2);

                if (lvgl_lock(2)) {
                    lv_obj_set_x(lever_indicator, screen_val - INDICATOR_SIZE/2);
                    lv_obj_move_foreground(lever_indicator);  
                    lvgl_unlock();
                }
                last_encoder_value = encoder_value;
            }
        } else {
            int32_t encoder_value = 0;
            if (encoder_mutex) {
                xSemaphoreTake(encoder_mutex, portMAX_DELAY);
                encoder_value = current_encoder_value;
                xSemaphoreGive(encoder_mutex);
            }
            if (abs(encoder_value - animation_target) < 5) {
                animation_active = false;
            }
        }

        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(UI_TASK_PERIOD));
    }
}

// Now only randomize our single reward_circle, not two separate circles:
void randomize_condition_circle_positions(void) {
    bool on_bottom = (esp_random() & 0x01);
    int32_t pos_x = on_bottom
                  ? (INDICATOR_START_X + CONDITION_SCREEN_OFFSET)
                  : (INDICATOR_START_X - CONDITION_SCREEN_OFFSET);

    session.reward_config.reward_x_pos = pos_x;

    if (lvgl_lock(10)) {
        lv_obj_set_pos(reward_circle,
                       pos_x - CONDITION_SIZE/2,
                       INDICATOR_Y_POS - CONDITION_SIZE/2);
        lvgl_unlock();
    }

    // recolor/highlight to match reward_config
    update_reward_styles();

    ESP_LOGI(TAG, "Randomized reward circle to %s side", on_bottom ? "pull" : "push");
}

// Function to update the trial counter display
void update_trial_counter() {
    if (lvgl_lock(10)) {
        if (session.trial_counter_label != NULL) {
            lv_label_set_text_fmt(session.trial_counter_label, 
                                 "Trial: %d/%d", 
                                 session.current_trial, 
                                 session.num_trials);
        }
        lvgl_unlock();
    }
}

//----------------------
// Trial state machine task

// Define a state period if not already defined
#ifndef STATE_TASK_PERIOD
#define STATE_TASK_PERIOD 5
#endif

void trial_state_task(void *pvParameters) {
    TickType_t state_start = xTaskGetTickCount();
    setup_gpio();
    trial_state = TRIAL_CONFIG;

    while (1) {
        switch (trial_state) {

        case TRIAL_CONFIG: {
            wait_for_config();  // UART string: TRIALS=25;COLOR=G;MODE=L;...
        
            // Update styles to match configuration
            update_circle_styles();
            
            session.current_trial = 0;
            update_trial_counter();
            trial_state = TRIAL_INIT;
            state_start = xTaskGetTickCount();
        } break;
            

        case TRIAL_INIT:
            log_and_pulse(EVT_TRIAL_START, session.current_trial);
            // hide both circles until cue
            lv_obj_add_flag(reward_circle,    LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(nonreward_circle, LV_OBJ_FLAG_HIDDEN);

            start_animation_to_position(0);
            vTaskDelay(pdMS_TO_TICKS(800));
            reset_position();
            
            // advance trial index 1..num_trials
            session.current_trial = (session.current_trial % session.num_trials) + 1;
            update_trial_counter();

            trial_state = TRIAL_CUE;
            state_start = xTaskGetTickCount();
            break;

        case TRIAL_CUE: {
            // position reward_circle alone
            int32_t rx, nrx;
            if (session.reward_config.reward_mode == REWARD_FIXED_PUSH) {
                rx  = INDICATOR_START_X - CONDITION_SCREEN_OFFSET;
                nrx = INDICATOR_START_X + CONDITION_SCREEN_OFFSET;
            } else if (session.reward_config.reward_mode == REWARD_FIXED_PULL) {
                rx  = INDICATOR_START_X + CONDITION_SCREEN_OFFSET;
                nrx = INDICATOR_START_X - CONDITION_SCREEN_OFFSET;
            } else { // random
                bool pull = (esp_random() & 1);
                rx  = INDICATOR_START_X + ( pull ? CONDITION_SCREEN_OFFSET : -CONDITION_SCREEN_OFFSET);
                nrx = INDICATOR_START_X + (!pull? CONDITION_SCREEN_OFFSET : -CONDITION_SCREEN_OFFSET);
            }
            // store for later
            session.reward_config.reward_x_pos = rx;
            session.reward_config.nonreward_x_pos = nrx;

            // show cue only (reward_circle)
            lv_obj_set_pos(reward_circle,    rx  - CONDITION_SIZE/2, INDICATOR_Y_POS - CONDITION_SIZE/2);
            lv_obj_clear_flag(reward_circle, LV_OBJ_FLAG_HIDDEN);
            play_tone(3000, 1000);
            log_and_pulse(EVT_CUE, session.current_trial);
            vTaskDelay(pdMS_TO_TICKS(1000));

            // hide cue & blank
            lv_obj_add_flag(reward_circle, LV_OBJ_FLAG_HIDDEN);
            vTaskDelay(pdMS_TO_TICKS(500));

            trial_state = TRIAL_SETUP;
            state_start = xTaskGetTickCount();
        } break;

        case TRIAL_SETUP:
            // position both circles
            lv_obj_set_pos(reward_circle,    session.reward_config.reward_x_pos     - CONDITION_SIZE/2, INDICATOR_Y_POS - CONDITION_SIZE/2);
            lv_obj_set_pos(nonreward_circle, session.reward_config.nonreward_x_pos - CONDITION_SIZE/2, INDICATOR_Y_POS - CONDITION_SIZE/2);

            // show both
            lv_obj_clear_flag(reward_circle,    LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(nonreward_circle, LV_OBJ_FLAG_HIDDEN);
            update_reward_styles();  // recolor them

            vTaskDelay(pdMS_TO_TICKS(500));
            unlock_lever(500);

            trial_state = TRIAL_ACTIVE;
            state_start = xTaskGetTickCount();
            log_and_pulse(EVT_TRIAL_ACTIVE, session.current_trial);
            break;

        case TRIAL_ACTIVE: {
            TickType_t trial_start = xTaskGetTickCount();
            TickType_t reward_contact_start = 0;
            TickType_t nonreward_contact_start = 0;
            bool timeout_occurred = false;
            const TickType_t hold_duration = pdMS_TO_TICKS(500);  // Require 500ms hold
        
            while (1) {
                int32_t cx = lv_obj_get_x(lever_indicator) + INDICATOR_SIZE / 2;
                int32_t cy = lv_obj_get_y(lever_indicator) + INDICATOR_SIZE / 2;
        
                int32_t rx = session.reward_config.reward_x_pos;
                int32_t ry = INDICATOR_Y_POS;
                int32_t nrx = session.reward_config.nonreward_x_pos;
                int32_t nry = INDICATOR_Y_POS;
        
                bool hit_reward = check_circle_collision(cx, cy, INDICATOR_SIZE / 2, rx, ry, CONDITION_SIZE / 2);
                bool hit_nonreward = check_circle_collision(cx, cy, INDICATOR_SIZE / 2, nrx, nry, CONDITION_SIZE / 2);
        
                // Handle reward hold timing
                if (hit_reward) {
                    if (reward_contact_start == 0) {
                        reward_contact_start = xTaskGetTickCount();
                    } else if (xTaskGetTickCount() - reward_contact_start >= hold_duration) {
                        trial_success = true;
                        trial_state = TRIAL_COMPLETE;
                        state_start = xTaskGetTickCount();
                        break;
                    }
                } else {
                    reward_contact_start = 0; // reset if not sustained
                }
        
                // Handle non-reward hold timing
                if (hit_nonreward) {
                    if (nonreward_contact_start == 0) {
                        nonreward_contact_start = xTaskGetTickCount();
                    } else if (xTaskGetTickCount() - nonreward_contact_start >= hold_duration) {
                        trial_success = false;
                        trial_state = TRIAL_COMPLETE;
                        state_start = xTaskGetTickCount();
                        break;
                    }
                } else {
                    nonreward_contact_start = 0; // reset if not sustained
                }
        
                //Trial timeout check
                if ((xTaskGetTickCount() - trial_start) > pdMS_TO_TICKS(TRIAL_TIMEOUT_MS)) {
                    timeout_occurred = true;
                    printf("Trial timed out — no response detected\n");
                    trial_success = false;
                    trial_state = TRIAL_COMPLETE;
                    state_start = xTaskGetTickCount();
                    break;
                }
        
                vTaskDelay(pdMS_TO_TICKS(10)); // add small delay to prevent task hogging
            }
        } break;         

        case TRIAL_COMPLETE:
            // highlight only the reward circle
            if (trial_success) {
                highlight_condition_circle(true);
                log_and_pulse(EVT_REWARD, session.current_trial);
                trial_state = REWARD_PERIOD;
            } else {
                highlight_condition_circle(false);
                log_and_pulse(EVT_NON_REWARD, session.current_trial);
                trial_state = NON_REWARD_PERIOD;
            }
            vTaskDelay(pdMS_TO_TICKS(1500));
            break;

        case REWARD_PERIOD:
            play_tone(5000,500);
            gpio_set_level(GPIO_REWARD_SIGNAL, 1);
            vTaskDelay(pdMS_TO_TICKS(REWARD_DURATION_MS));
            gpio_set_level(GPIO_REWARD_SIGNAL, 0);
            trial_state = TRIAL_RESET;
            state_start = xTaskGetTickCount();
            break;

        case NON_REWARD_PERIOD:
            vTaskDelay(pdMS_TO_TICKS(REWARD_DURATION_MS));
            trial_state = TRIAL_RESET;
            state_start = xTaskGetTickCount();
            break;

        case TRIAL_RESET:
            // hide circles, reset lever
            lv_obj_add_flag(reward_circle,    LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(nonreward_circle, LV_OBJ_FLAG_HIDDEN);
            highlight_condition_circle(false);
        
            start_animation_to_position(0);
            vTaskDelay(pdMS_TO_TICKS(100));
            reset_position();
        
            // End-of-session check
            if (session.current_trial >= session.num_trials) {
                log_and_pulse(EVT_SESSION_END, session.current_trial);
                trial_state = SESSION_END;
            } else {
                trial_state = TRIAL_INIT;
            }
        
            state_start = xTaskGetTickCount();
            break;

        case SESSION_END: {
            printf("SESSION_END: Trials complete. Session is over.\\n");
        
            // Optional: black out screen background
            if (lvgl_lock(10)) {
                lv_obj_t *scr = lv_scr_act();
                lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
                lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
        
                // Hide all UI objects
                lv_obj_add_flag(reward_circle,    LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(nonreward_circle, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(lever_indicator,  LV_OBJ_FLAG_HIDDEN);
        
                // Optional: show "Session Complete" label
                lv_obj_t *label = lv_label_create(scr);
                lv_label_set_text(label, "Session Complete");
                lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
                lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
                lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
        
                lvgl_unlock();
            }
              
            // Idle loop
            while (1) {
                vTaskDelay(pdMS_TO_TICKS(1000)); // Remain in idle state
            }
        } break;
        
        default:
            ESP_LOGW(TAG, "Unknown state %d → CONFIG", trial_state);
            trial_state = TRIAL_CONFIG;
            state_start = xTaskGetTickCount();
            break;
        }

        // safety timeout: reset to config if stuck
        if ((xTaskGetTickCount() - state_start) > pdMS_TO_TICKS(10000)) {
            ESP_LOGW(TAG, "State %d stuck → CONFIG", trial_state);
            trial_state = TRIAL_CONFIG;
            state_start = xTaskGetTickCount();
        }

        vTaskDelay(pdMS_TO_TICKS(STATE_TASK_PERIOD));
    }
}
//----------------------
// Main application entry point
void app_main(void) {
    ESP_LOGI(TAG, "Starting lever test application");
    // Only keep ERROR+ for LEDC; similarly you can mute any tag you like.
    esp_log_level_set("ledc", ESP_LOG_ERROR);
    // If you want to mute all other modules except your TAG:
    esp_log_level_set("*", ESP_LOG_ERROR);
    // Then re-enable your task’s TAG:
    esp_log_level_set(TAG, ESP_LOG_INFO);

    
    // Initialize UART for motor communication
    ESP_ERROR_CHECK(uart_param_config(uart_num, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(uart_num, 25, 24, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(uart_num, 256, 0, 0, NULL, 0));
    // after existing UART₂
    ESP_ERROR_CHECK(uart_param_config   (UART_NUM_0, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin        (UART_NUM_0, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install (UART_NUM_0, 256, 0, 0, NULL, 0));

        
    // Initialize encoder mutex
    encoder_mutex = xSemaphoreCreateMutex();
    if (encoder_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create encoder mutex");
        return;
    }
    
    // Initialize display and set backlight
    lv_display_t *display = lcd_init();
    bsp_set_lcd_backlight(1);  // Turn on backlight
    
    ESP_LOGI(TAG, "Creating UI elements");
    create_lever_ui(display);
    
    // Reset encoder counter and position
    ESP_LOGI(TAG, "Initializing motor and encoder");
    reset_encoder_counter();
    reset_position();
    vTaskDelay(pdMS_TO_TICKS(500));

    ESP_ERROR_CHECK(encoder_out_init());
    
    // Create tasks with appropriate priorities
    ESP_LOGI(TAG, "Creating tasks");
    
    // Create encoder reading task (highest priority)
    xTaskCreate(encoder_read_task, "encoder_task", STACK_SIZE, NULL, 
                configMAX_PRIORITIES - 5, NULL);
    
    // Create UI update task
    xTaskCreate(ui_update_task, "ui_task", STACK_SIZE, NULL, 
                configMAX_PRIORITIES - 4, NULL);
    
    // Create trial state machine task (lower priority)
    xTaskCreate(trial_state_task, "trial_state_task", STACK_SIZE, NULL, 
                configMAX_PRIORITIES - 6, NULL);
    
    ESP_LOGI(TAG, "Initialization complete, starting main loop");
}