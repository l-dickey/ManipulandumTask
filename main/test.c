#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/projdefs.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "lvgl.h"
#include "audio_pwm.c"
#include "graphics.h"
#include "mcpcommands.c"
#include "peripheral_config.c"
#include "driver/ledc.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "font/lv_font.h"

// Define our GPIO Pins for use
#define GPIO_REWARD_SIGNAL   3 // GPIO pin connected to the syringe pump
#define GPIO_STATE_0         4 // GPIO pin for 8 bit encoding
#define GPIO_STATE_1         5 // GPIO pin for 8 bit encoding
#define GPIO_STATE_2         6 // GPIO pin for 8 bit encoding

#define GPIO_OUTPUT_PIN_MASK ((1ULL<<GPIO_STATE_0) | (1ULL<<GPIO_STATE_1) | \
                             (1ULL<<GPIO_STATE_2) | (1ULL<<GPIO_REWARD_SIGNAL)) // make the pins outputs

// Define our mapping parameters
#define ENCODER_MAX_RANGE    500      // Maximum encoder count for lever range
#define SCREEN_WIDTH         1024     // Screen width (pixels)
#define SCREEN_HEIGHT        600      // Screen height (pixels)
#define INDICATOR_SIZE       30       // Size of the indicator circle (pixels)
#define CONDITION_SIZE       60       // Size of condition circles (pixels) - INCREASED SIZE
#define INDICATOR_START_X    (SCREEN_WIDTH / 2)  // Center X position
#define INDICATOR_Y_POS      (SCREEN_HEIGHT / 2) // Vertical center position

// Condition circle positions (offset from center by approximately 200 encoder counts)
#define CONDITION_OFFSET     200      // ~200 encoder counts for condition circles
#define CONDITION_SCREEN_OFFSET ((CONDITION_OFFSET * (SCREEN_WIDTH / 2 - INDICATOR_SIZE)) / ENCODER_MAX_RANGE)

// Task parameters
#define STACK_SIZE           4096
#define ENCODER_TASK_PERIOD  10        // 5ms period (200Hz) - INCREASED FREQUENCY
#define UI_TASK_PERIOD       10        // 5ms period (200Hz) for responsive UI - INCREASED FREQUENCY
#define STATE_TASK_PERIOD    100        // 100ms period
#define REWARD_DURATION_MS   500       // 500ms to signal the syringe pump via TTL

// Define LEDC parameters, used here for audio generation via PWMs
#define LEDC_TIMER              LEDC_TIMER_0
#define LEDC_MODE               LEDC_HIGH_SPEED_MODE
#define LEDC_CHANNEL            LEDC_CHANNEL_0
#define LEDC_DUTY_RES           LEDC_TIMER_13_BIT    // 13-bit duty resolution
#define LEDC_MAX_DUTY           ((1 << LEDC_DUTY_RES) - 1)  // For 13-bit, max duty is 8191
#define TONE_GPIO               48                   // PWM output on GPIO 48


// Global variables
static const char *TAG = "LEVER_TEST";
SemaphoreHandle_t encoder_mutex = NULL;
volatile int32_t current_encoder_value = 0;
lv_obj_t *lever_indicator = NULL;
lv_obj_t *condition_circle_push = NULL;  // Forward/push condition circle
lv_obj_t *condition_circle_pull = NULL;  // Backward/pull condition circle
lv_style_t indicator_style;
lv_style_t condition_style;
lv_style_t condition_active_style;

// -------------------
// Trial State Machine
typedef enum {
    TRIAL_INIT,      // Reset lever to center and prepare for new trial
    TRIAL_CUE,       // Show only reward circle briefly
    TRIAL_SETUP,     // Set up trial conditions (hide cue, show both circles)
    TRIAL_ACTIVE,    // Lever is free to move
    TRIAL_COMPLETE,  // Condition reached, trial finished
    REWARD_PERIOD,   // Reward dispensed
    NON_REWARD_PERIOD, // Non-reward for same duration as reward
    TRIAL_RESET      // Reset lever to center for next trial
} trial_state_t;

const uint8_t STATE_ENCODINGS[8][3] = { // each state can be called in the state machine to output the state via DAC board
    {0,0,0}, // TRIAL_INIT
    {0,0,1}, // TRIAL_CUE
    {0,1,0}, // TRIAL_SETUP
    {0,1,1}, // TRIAL_ACTIVE
    {1,0,0}, // TRIAL_COMPLETE
    {1,0,1}, // REWARD_PERIOD
    {1,1,0}, // NON_REWARD_PERIOD
    {1,1,1}  // TRIAL_RESET
};

// Condition type for the trial
typedef enum {
    CONDITION_PUSH,  // Push forward is correct
    CONDITION_PULL   // Pull back is correct
} condition_type_t;

volatile trial_state_t trial_state = TRIAL_INIT;
volatile condition_type_t current_condition = CONDITION_PUSH;
volatile bool trial_success = false;

// Animation flag (used by animation callback and UI task)
bool animation_active = false;
int32_t animation_target = 0;

typedef struct {
    bool reward_color_is_green;    // If true: reward circle is green, non-reward is purple; if false: vice versa
    bool reward_position_fixed;    // If true: reward circle remains in a fixed position (left or right)
    bool reward_is_push;           // If true: the PUSH condition is rewarded; if false: PULL is rewarded
    int32_t reward_x_pos;          // X-position for reward circle when fixed
    int32_t nonreward_x_pos;       // X-position for non-reward circle when fixed
} RewardConfig;

// Initialize with default values. For example, if fixed and reward is on the left:
RewardConfig reward_config = {
    .reward_color_is_green = true,          // Green is the reward color.
    .reward_position_fixed = true,          // Reward circle is fixed.
    .reward_x_pos = INDICATOR_START_X - CONDITION_SCREEN_OFFSET,  // Reward circle on left.
    .nonreward_x_pos = INDICATOR_START_X + CONDITION_SCREEN_OFFSET  // Nonreward on right.
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
        .reward_position_fixed = true,
        .reward_x_pos = INDICATOR_START_X - CONDITION_SCREEN_OFFSET,
        .nonreward_x_pos = INDICATOR_START_X + CONDITION_SCREEN_OFFSET
    }
};

// Function prototypes
void encoder_read_task(void *pvParameters);
void ui_update_task(void *pvParameters);
void trial_state_task(void *pvParameters);
void create_lever_ui(lv_display_t *display);
void play_tone(uint32_t tone_frequency, uint32_t duration_ms);
void set_motor_position(int32_t position);
void start_animation_to_position(int32_t position);
static void animation_callback(void *var, int32_t value);
void highlight_condition_circle(condition_type_t condition, bool active);
condition_type_t determine_condition(uint8_t f1, uint8_t f2);
bool check_circle_collision(int32_t x1, int32_t y1, int32_t r1, int32_t x2, int32_t y2, int32_t r2);
void randomize_condition_circle_positions(void);
void update_trial_counter(void);

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

// Set State Encodings
static void set_state_pins(trial_state_t state) {
    gpio_set_level(GPIO_STATE_0, STATE_ENCODINGS[state][0]);
    gpio_set_level(GPIO_STATE_1, STATE_ENCODINGS[state][1]);
    gpio_set_level(GPIO_STATE_2, STATE_ENCODINGS[state][2]);
    ESP_LOGI(TAG, "State changed to: %d [%d%d%d]", state, 
             STATE_ENCODINGS[state][0],
             STATE_ENCODINGS[state][1],
             STATE_ENCODINGS[state][2]);
}


//----------------------
static char wait_for_user_input(int timeout_ms) {
    uint8_t data;
    int len = uart_read_bytes(uart_num, &data, 1, pdMS_TO_TICKS(timeout_ms));
    if (len > 0) {
        if (data >= 'a' && data <= 'z') {
            data -= ('a' - 'A');  // Convert to uppercase
        }
        ESP_LOGI(TAG, "User input: %c", data);
        return (char)data;
    }
    return '\0';  // Return null character if no input
}

// Enhanced session configuration function
bool configure_session() {
    ESP_LOGI(TAG, "===== SESSION CONFIGURATION =====");
    ESP_LOGI(TAG, "Press any key within 5 seconds to configure session, or wait for default settings");
    
    // Default configuration
    session.reward_config.reward_is_push = true;  // Default: PUSH is rewarded
    
    // Wait for initial input to start configuration
    char key = wait_for_user_input(5000);  // 5 second timeout
    
    if (key == '\0') {
        ESP_LOGI(TAG, "Using default configuration: %d trials, %s reward color, %s position, %s condition rewarded", 
                session.num_trials,
                session.reward_config.reward_color_is_green ? "Green" : "Purple",
                session.reward_config.reward_position_fixed ? "Fixed" : "Random",
                session.reward_config.reward_is_push ? "PUSH" : "PULL");
        return true;  // Use defaults
    }
    
    // Configure number of trials
    ESP_LOGI(TAG, "Enter number of trials (1-9 for 10-90 trials):");
    do {
        key = wait_for_user_input(10000);  // 10 second timeout
    } while(key == '\0');
    
    if (key >= '1' && key <= '9') {
        session.num_trials = (key - '0') * 10;  // Convert to number and multiply by 10
        ESP_LOGI(TAG, "Number of trials set to %d", session.num_trials);
    } else {
        ESP_LOGI(TAG, "Invalid input, using default of %d trials", session.num_trials);
    }
    
    // Configure reward color
    ESP_LOGI(TAG, "Select reward color: G for Green, P for Purple");
    do {
        key = wait_for_user_input(10000);
    } while(key == '\0');
    
    if (key == 'G') {
        session.reward_config.reward_color_is_green = true;
        ESP_LOGI(TAG, "Reward color set to Green");
    } else if (key == 'P') {
        session.reward_config.reward_color_is_green = false;
        ESP_LOGI(TAG, "Reward color set to Purple");
    } else {
        ESP_LOGI(TAG, "Invalid input, using default color");
    }
    
    // Configure reward position mode
    ESP_LOGI(TAG, "Select reward position mode: F for Fixed, R for Random");
    do {
        key = wait_for_user_input(10000);
    } while(key == '\0');
    
    if (key == 'F') {
        session.reward_config.reward_position_fixed = true;
        ESP_LOGI(TAG, "Reward position set to Fixed");
    } else if (key == 'R') {
        session.reward_config.reward_position_fixed = false;
        ESP_LOGI(TAG, "Reward position set to Random");
    } else {
        ESP_LOGI(TAG, "Invalid input, using default position mode");
    }
    
    // Configure which condition is rewarded
    ESP_LOGI(TAG, "Select rewarded condition: P for Push, L for Pull");
    do {
        key = wait_for_user_input(10000);
    } while(key == '\0');
    
    if (key == 'P') {
        session.reward_config.reward_is_push = true;
        ESP_LOGI(TAG, "PUSH condition will be rewarded");
    } else if (key == 'L') {
        session.reward_config.reward_is_push = false;
        ESP_LOGI(TAG, "PULL condition will be rewarded");
    } else {
        ESP_LOGI(TAG, "Invalid input, using default rewarded condition");
    }
    
    ESP_LOGI(TAG, "Configuration complete: %d trials, %s reward color, %s position, %s condition rewarded",
            session.num_trials,
            session.reward_config.reward_color_is_green ? "Green" : "Purple",
            session.reward_config.reward_position_fixed ? "Fixed" : "Random",
            session.reward_config.reward_is_push ? "PUSH" : "PULL");
    
    return true;
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
    if (lvgl_lock(2)) {  // Reduced lock time for faster updates
        lv_obj_set_x(lever_indicator, value);
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
    int32_t screen_value = screen_center + ((position * (SCREEN_WIDTH / 2 - INDICATOR_SIZE)) / ENCODER_MAX_RANGE);
    
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
            xSemaphoreGive(encoder_mutex);
        }
        
        // --- STREAM OVER UART ---
        // Use microsecond timer / 1000 for ms
        uint32_t ts = esp_timer_get_time() / 1000;
        // Print as: timestamp_ms,encoder_count\n
        printf("%" PRIu32 ",%" PRId32 "\n", ts, encoder_value);

        // static int log_counter = 0; // uncomment for debugging
        // if (++log_counter >= 100) {  // Reduced logging frequency
        //     ESP_LOGI(TAG, "Encoder value: %" PRId32, encoder_value);
        //     log_counter = 0;
        // }
        
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(ENCODER_TASK_PERIOD));
    }
}

//----------------------
// Determine the correct condition based on f1 and f2 values
condition_type_t determine_condition(uint8_t f1, uint8_t f2) {
    // If f1 > f2, push forward is the correct condition
    // If f1 < f2, pull back is the correct condition
    return (f1 > f2) ? CONDITION_PUSH : CONDITION_PULL;
}

//----------------------
// Modified highlight function to only highlight when correct
void highlight_condition_circle(condition_type_t condition, bool is_correct) {
    if (!is_correct) {
        return;  // Don't highlight incorrect choices
    }
    
    if (lvgl_lock(10)) {
        if (condition == CONDITION_PUSH) {
            // Add highlighting to push circle
            lv_obj_add_style(condition_circle_push, &condition_active_style, 0);
        } else {
            // Add highlighting to pull circle
            lv_obj_add_style(condition_circle_pull, &condition_active_style, 0);
        }
        lvgl_unlock();
    } else {
        ESP_LOGW(TAG, "failed to acquire LVGL mutex for updates h_c_c");
    }
}

 
void set_condition_circle_visibility(bool show_push, bool show_pull) {
    if (lvgl_lock(20)) {  // Longer timeout to ensure we get lock
        // For push circle
        if (show_push) {
            lv_obj_set_style_bg_opa(condition_circle_push, 255, 0);  // Full opacity
            lv_obj_clear_flag(condition_circle_push, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(condition_circle_push, LV_OBJ_FLAG_HIDDEN);
        }
        
        // For pull circle
        if (show_pull) {
            lv_obj_set_style_bg_opa(condition_circle_pull, 255, 0);  // Full opacity
            lv_obj_clear_flag(condition_circle_pull, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(condition_circle_pull, LV_OBJ_FLAG_HIDDEN);
        }
        
        ESP_LOGI(TAG, "Visibility set: Push=%d, Pull=%d", show_push, show_pull);
        lvgl_unlock();
    } else {
        ESP_LOGE(TAG, "Failed to acquire LVGL mutex in set_condition_circle_visibility");
    }
}

// Function to update styles based on reward configuration
void update_reward_styles() {
    if (lvgl_lock(10)) {
        // Define reward and non-reward colors based on configuration
        lv_color_t reward_color = session.reward_config.reward_color_is_green ? 
                                lv_color_hex(0x00C000) : // Green
                                lv_color_hex(0x800080);  // Purple
                                
        lv_color_t nonreward_color = session.reward_config.reward_color_is_green ? 
                                lv_color_hex(0x800080) : // Purple
                                lv_color_hex(0x00C000);  // Green
        
        // Apply appropriate colors based on which condition is rewarded
        if (session.reward_config.reward_is_push) {
            // PUSH circle gets reward color
            lv_obj_set_style_bg_color(condition_circle_push, reward_color, 0);
            lv_obj_set_style_bg_color(condition_circle_pull, nonreward_color, 0);
        } else {
            // PULL circle gets reward color
            lv_obj_set_style_bg_color(condition_circle_pull, reward_color, 0);
            lv_obj_set_style_bg_color(condition_circle_push, nonreward_color, 0);
        }
        
        // The active style should match the reward color for highlighting when correct
        lv_style_set_bg_color(&condition_active_style, reward_color);
        
        ESP_LOGI(TAG, "Reward styles updated: Push=%s, Pull=%s", 
                (session.reward_config.reward_is_push) ? "Reward" : "Non-reward",
                (session.reward_config.reward_is_push) ? "Non-reward" : "Reward");
        
        lvgl_unlock();
    } else {
        ESP_LOGE(TAG, "Failed to acquire LVGL mutex in update_reward_styles");
    }
}

//----------------------
// create the ui for the lever
// Enhanced UI creation function
void create_lever_ui(lv_display_t *display) {
    lv_obj_t *scr = lv_disp_get_scr_act(display);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);  // Black background

    // Create style for the lever indicator
    lv_style_init(&indicator_style);
    lv_style_set_radius(&indicator_style, LV_RADIUS_CIRCLE);
    lv_style_set_bg_color(&indicator_style, lv_color_hex(0xFFFFFF));  // White indicator
    lv_style_set_bg_opa(&indicator_style, 255);  // Fully opaque
    lv_style_set_border_width(&indicator_style, 2);
    lv_style_set_border_color(&indicator_style, lv_color_hex(0xFFFFFF));

    // Create the lever indicator at center position
    lever_indicator = lv_obj_create(scr);
    lv_obj_remove_style_all(lever_indicator);
    lv_obj_add_style(lever_indicator, &indicator_style, 0);
    lv_obj_set_size(lever_indicator, INDICATOR_SIZE, INDICATOR_SIZE);
    lv_obj_set_pos(lever_indicator, INDICATOR_START_X - INDICATOR_SIZE/2, INDICATOR_Y_POS - INDICATOR_SIZE/2);

    // Create style for condition circles (base style)
    lv_style_init(&condition_style);
    lv_style_set_radius(&condition_style, LV_RADIUS_CIRCLE);
    lv_style_set_bg_opa(&condition_style, 255);  // Fully opaque
    lv_style_set_border_width(&condition_style, 2);
    lv_style_set_border_color(&condition_style, lv_color_hex(0x808080));

    // Create style for active condition circle highlighting
    lv_style_init(&condition_active_style);
    lv_style_set_radius(&condition_active_style, LV_RADIUS_CIRCLE);
    lv_style_set_bg_opa(&condition_active_style, 255);
    lv_style_set_border_width(&condition_active_style, 3);
    lv_style_set_border_color(&condition_active_style, lv_color_hex(0xFFFFFF));
    lv_style_set_bg_color(&condition_active_style, lv_color_hex(0x00C000));  // Default highlight color, will be updated

    // Base positions for circles
    int32_t left_pos = INDICATOR_START_X - CONDITION_SCREEN_OFFSET;
    int32_t right_pos = INDICATOR_START_X + CONDITION_SCREEN_OFFSET;

    // Create the condition circles
    condition_circle_push = lv_obj_create(scr);
    lv_obj_remove_style_all(condition_circle_push);
    lv_obj_add_style(condition_circle_push, &condition_style, 0);  // Apply base style
    lv_obj_set_size(condition_circle_push, CONDITION_SIZE, CONDITION_SIZE);
    
    condition_circle_pull = lv_obj_create(scr);
    lv_obj_remove_style_all(condition_circle_pull);
    lv_obj_add_style(condition_circle_pull, &condition_style, 0);  // Apply base style
    lv_obj_set_size(condition_circle_pull, CONDITION_SIZE, CONDITION_SIZE);
    
    // Position the circles based on configuration
    if (session.reward_config.reward_position_fixed) {
        if (session.reward_config.reward_is_push) {
            // Push circle at reward position
            lv_obj_set_pos(condition_circle_push, 
                    session.reward_config.reward_x_pos - CONDITION_SIZE/2,
                    INDICATOR_Y_POS - CONDITION_SIZE/2);
            lv_obj_set_pos(condition_circle_pull, 
                    session.reward_config.nonreward_x_pos - CONDITION_SIZE/2,
                    INDICATOR_Y_POS - CONDITION_SIZE/2);
        } else {
            // Pull circle at reward position
            lv_obj_set_pos(condition_circle_pull, 
                    session.reward_config.reward_x_pos - CONDITION_SIZE/2,
                    INDICATOR_Y_POS - CONDITION_SIZE/2);
            lv_obj_set_pos(condition_circle_push, 
                    session.reward_config.nonreward_x_pos - CONDITION_SIZE/2,
                    INDICATOR_Y_POS - CONDITION_SIZE/2);
        }
    } else {
        // Default positions, will be randomized later
        lv_obj_set_pos(condition_circle_push, 
                right_pos - CONDITION_SIZE/2,
                INDICATOR_Y_POS - CONDITION_SIZE/2);
        lv_obj_set_pos(condition_circle_pull, 
                left_pos - CONDITION_SIZE/2,
                INDICATOR_Y_POS - CONDITION_SIZE/2);
    }
    
    // Initially hide both circles
    lv_obj_add_flag(condition_circle_push, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(condition_circle_pull, LV_OBJ_FLAG_HIDDEN);
    
    // Apply colors based on configuration
    update_reward_styles();
    
    // Create central start marker
    lv_obj_t *center_marker = lv_obj_create(scr);
    lv_obj_remove_style_all(center_marker);
    lv_obj_set_style_bg_color(center_marker, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(center_marker, 200, 0);
    lv_obj_set_size(center_marker, 2, 40);
    lv_obj_set_pos(center_marker, SCREEN_WIDTH/2 - 1, INDICATOR_Y_POS - 20);
    
    // Create trial counter text at the top
    lv_obj_t *trial_counter = lv_label_create(scr);
    lv_obj_set_style_text_color(trial_counter, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text_fmt(trial_counter, "Trial: 0/%d", session.num_trials);
    lv_obj_align(trial_counter, LV_ALIGN_TOP_MID, 0, 10);
    session.trial_counter_label = trial_counter;  // Save reference for later updates
    
    ESP_LOGI(TAG, "UI creation complete");
}

//----------------------
// Task to update UI based on encoder position
void ui_update_task(void *pvParameters) {
    TickType_t last_wake_time = xTaskGetTickCount();
    int32_t last_encoder_value = 0;
    
    ESP_LOGI(TAG, "UI update task started");
    
    while (1) {
        if (!animation_active) {
            int32_t encoder_value = 0;
            if (encoder_mutex != NULL) {
                xSemaphoreTake(encoder_mutex, portMAX_DELAY);
                encoder_value = current_encoder_value;
                xSemaphoreGive(encoder_mutex);
            }
            
            // Update UI even with small changes for smoother motion
            if (encoder_value != last_encoder_value) {
                int32_t screen_center = SCREEN_WIDTH / 2;
                int32_t screen_value = screen_center + ((encoder_value * (SCREEN_WIDTH / 2 - INDICATOR_SIZE)) / ENCODER_MAX_RANGE);
                
                if (screen_value < INDICATOR_SIZE / 2) screen_value = INDICATOR_SIZE / 2;
                if (screen_value > SCREEN_WIDTH - INDICATOR_SIZE / 2) screen_value = SCREEN_WIDTH - INDICATOR_SIZE / 2;
                
                if (lvgl_lock(2)) {  // Reduced lock time for faster updates
                    lv_obj_set_x(lever_indicator, screen_value - INDICATOR_SIZE / 2);
                    lvgl_unlock();
                } else {
                    ESP_LOGW(TAG, "failed to acquire LVGL mutex for updates ui_update task");
                }
                
                last_encoder_value = encoder_value;
            }
        } else {
            // During animation, check if target is reached to clear the flag
            int32_t encoder_value = 0;
            if (encoder_mutex != NULL) {
                xSemaphoreTake(encoder_mutex, portMAX_DELAY);
                encoder_value = current_encoder_value;
                xSemaphoreGive(encoder_mutex);
            }
            
            if (abs(encoder_value - animation_target) < 5) {  // Decreased threshold for faster response
                animation_active = false;
            }
        }
        
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(UI_TASK_PERIOD));
    }
}

// Function to randomize condition circle positions
void randomize_condition_circle_positions() {
    bool push_on_right = (esp_random() & 0x01);  // Random boolean (0 or 1)
    
    int32_t left_pos = INDICATOR_START_X - CONDITION_SCREEN_OFFSET;
    int32_t right_pos = INDICATOR_START_X + CONDITION_SCREEN_OFFSET;
    
    if (lvgl_lock(10)) {
        if (push_on_right) {
            // PUSH on right, PULL on left
            lv_obj_set_pos(condition_circle_push, 
                    right_pos - CONDITION_SIZE/2, 
                    INDICATOR_Y_POS - CONDITION_SIZE/2);
            lv_obj_set_pos(condition_circle_pull, 
                    left_pos - CONDITION_SIZE/2, 
                    INDICATOR_Y_POS - CONDITION_SIZE/2);
        } else {
            // PUSH on left, PULL on right
            lv_obj_set_pos(condition_circle_push, 
                    left_pos - CONDITION_SIZE/2, 
                    INDICATOR_Y_POS - CONDITION_SIZE/2);
            lv_obj_set_pos(condition_circle_pull, 
                    right_pos - CONDITION_SIZE/2, 
                    INDICATOR_Y_POS - CONDITION_SIZE/2);
        }
        lvgl_unlock();
    }
    
    // Apply correct colors based on which condition is rewarded
    update_reward_styles();
    
    ESP_LOGI(TAG, "Randomized circle positions: PUSH on %s", push_on_right ? "right" : "left");
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
    uint8_t f1 = 0, f2 = 0;
    bool success = false;
    TickType_t state_start_time = 0;
    
    ESP_LOGI(TAG, "Trial state machine task started");
    
    // Initialize GPIO pins
    setup_gpio();
    
    // Before starting trials, if the reward position is not fixed, randomize positions once.
    if (!session.reward_config.reward_position_fixed) {
        randomize_condition_circle_positions();
    }
    
    trial_state = TRIAL_INIT;
    state_start_time = xTaskGetTickCount();
    set_state_pins(trial_state);  // Set initial state encoding
    
    while (1) {
        switch(trial_state) {
            case TRIAL_INIT:
                { // sends out signal for trial start for encoder counts
                    uint64_t t_us = esp_timer_get_time();  
                    // EVENT,TYPE,trial_number,timestamp_us
                    printf("EVENT,START,%d,%" PRIu64 "\n",
                        session.current_trial,
                        t_us);
                }
                // ESP_LOGI(TAG, "TRIAL_INIT: Resetting lever and hiding condition circles"); // uncomment for debugging
                // Hide both circles
                set_condition_circle_visibility(false, false);
                // Reset any highlighting
                highlight_condition_circle(CONDITION_PUSH, false);
                highlight_condition_circle(CONDITION_PULL, false);
                // Reset lever to center
                start_animation_to_position(0);
                vTaskDelay(pdMS_TO_TICKS(800));
                reset_position();
                
                // Increment trial counter
                if (session.current_trial < session.num_trials) {
                    session.current_trial++;
                    update_trial_counter();
                } else {
                    ESP_LOGI(TAG, "All trials completed, restarting session");
                    session.current_trial = 1;
                    update_trial_counter();
                }
                trial_state = TRIAL_CUE;
                state_start_time = xTaskGetTickCount();
                set_state_pins(trial_state);  // Update state pins
                break;
                
            case TRIAL_CUE:
                // Generate the trial condition from random numbers.
                // Set the cued condition based solely on the reward configuration.
                current_condition = session.reward_config.reward_is_push ? CONDITION_PUSH : CONDITION_PULL;
                ESP_LOGI(TAG, "TRIAL_CUE: Cued condition is: %s",
                        (current_condition == CONDITION_PUSH) ? "PUSH" : "PULL");

                         
                // In this session-level design, the rewarded condition is fixed.
                // Display only the reward circle as the cue.
                if (session.reward_config.reward_is_push) {
                    // Reward is assigned to the push circle.
                    // Display the push circle as cue.
                    set_condition_circle_visibility(true, false);
                    play_tone(440, 1000);
                } else {
                    // Reward is assigned to the pull circle.
                    set_condition_circle_visibility(false, true);
                    play_tone(440, 1000);
                }
                vTaskDelay(pdMS_TO_TICKS(1000));  // Cue visible for 1 second
                
                // Hide cue before moving on.
                set_condition_circle_visibility(false, false);
                ESP_LOGI(TAG, "TRIAL_CUE complete; entering blank period");
                vTaskDelay(pdMS_TO_TICKS(500));  // Blank phase of 500ms
                trial_state = TRIAL_SETUP;
                state_start_time = xTaskGetTickCount();
                set_state_pins(trial_state);  // Update state pins
                break;
                
            case TRIAL_SETUP:
                ESP_LOGI(TAG, "TRIAL_SETUP: Showing both condition circles");
                // At this point, both condition circles should be visible.
                // If fixed, their positions were set in TRIAL_INIT; if random, they've been randomized.
                set_condition_circle_visibility(true, true);
                // Update reward colors and styles based on session settings.
                update_reward_styles();
                
                // Give a brief delay for subject to see both targets before unlocking lever.
                vTaskDelay(pdMS_TO_TICKS(500));
                
                // Unlock lever (allow subject to move)
                unlock_lever(500);
                
                trial_state = TRIAL_ACTIVE;
                state_start_time = xTaskGetTickCount();
                set_state_pins(trial_state);  // Update state pins
                break;
                
            case TRIAL_ACTIVE: {
                int32_t encoder_val = 0;
                if (encoder_mutex != NULL) {
                    xSemaphoreTake(encoder_mutex, portMAX_DELAY);
                    encoder_val = current_encoder_value;
                    xSemaphoreGive(encoder_mutex);
                }
                
                int32_t indicator_x = 0, indicator_y = 0;
                int32_t push_x = 0, push_y = 0, pull_x = 0, pull_y = 0;
                if (lvgl_lock(2)) {
                    indicator_x = lv_obj_get_x(lever_indicator) + INDICATOR_SIZE/2;
                    indicator_y = lv_obj_get_y(lever_indicator) + INDICATOR_SIZE/2;
                    
                    push_x = lv_obj_get_x(condition_circle_push) + CONDITION_SIZE/2;
                    push_y = lv_obj_get_y(condition_circle_push) + CONDITION_SIZE/2;
                    
                    pull_x = lv_obj_get_x(condition_circle_pull) + CONDITION_SIZE/2;
                    pull_y = lv_obj_get_y(condition_circle_pull) + CONDITION_SIZE/2;
                    lvgl_unlock();
                }
                
                // Check collision between lever indicator and condition circles.
                bool push_collision = check_circle_collision(indicator_x, indicator_y, INDICATOR_SIZE/2,
                                                               push_x, push_y, CONDITION_SIZE/2);
                bool pull_collision = check_circle_collision(indicator_x, indicator_y, INDICATOR_SIZE/2,
                                                               pull_x, pull_y, CONDITION_SIZE/2);
                if (push_collision) {
                    success = (current_condition == CONDITION_PUSH);
                    trial_success = success;
                    ESP_LOGI(TAG, "TRIAL_ACTIVE: PUSH collision detected: %s", success ? "CORRECT" : "INCORRECT");
                    trial_state = TRIAL_COMPLETE;
                    state_start_time = xTaskGetTickCount();
                    set_state_pins(trial_state);  // Update state pins
                } else if (pull_collision) {
                    success = (current_condition == CONDITION_PULL);
                    trial_success = success;
                    ESP_LOGI(TAG, "TRIAL_ACTIVE: PULL collision detected: %s", success ? "CORRECT" : "INCORRECT");
                    trial_state = TRIAL_COMPLETE;
                    state_start_time = xTaskGetTickCount();
                    set_state_pins(trial_state);  // Update state pins
                }
                
                // Safety: if encoder is out-of-bounds, reset the trial.
                if (encoder_val > 510 || encoder_val < -510) {
                    ESP_LOGW(TAG, "Encoder out-of-bounds (%" PRId32 "), resetting trial", encoder_val);
                    trial_state = TRIAL_RESET;
                    state_start_time = xTaskGetTickCount();
                    set_state_pins(trial_state);  // Update state pins
                }
            }
                vTaskDelay(pdMS_TO_TICKS(STATE_TASK_PERIOD));
                break;
                
            case TRIAL_COMPLETE:
                // Show the outcome on screen.
                if (trial_success) {
                    ESP_LOGI(TAG, "TRIAL_COMPLETE: Correct response");
                    printf("cor\n");
                    // Highlight the correct condition circle.
                    highlight_condition_circle(current_condition, true);
                    trial_state = REWARD_PERIOD;  // Move to reward period
                } else {
                    ESP_LOGI(TAG, "TRIAL_COMPLETE: Incorrect response");
                    printf("inc\n");
                    highlight_condition_circle(current_condition, true);
                    trial_state = NON_REWARD_PERIOD;  // Move to non-reward period
                }
                state_start_time = xTaskGetTickCount();
                set_state_pins(trial_state);  // Update state pins
                vTaskDelay(pdMS_TO_TICKS(1500));  // Keep result visible for 1.5 seconds
                // Reset highlighting.
                highlight_condition_circle(CONDITION_PUSH, false);
                highlight_condition_circle(CONDITION_PULL, false);
                
                reset_position();
                break;

            case REWARD_PERIOD:
                ESP_LOGI(TAG, "Dispensing Reward");
                gpio_set_level(GPIO_REWARD_SIGNAL, 1);  // Activate reward signal
                vTaskDelay(pdMS_TO_TICKS(REWARD_DURATION_MS));
                gpio_set_level(GPIO_REWARD_SIGNAL, 0);  // Turn off reward signal
                { // send UART to PC log for end of trial
                    uint64_t t_us = esp_timer_get_time();
                    printf("EVENT,END,%d,%" PRIu64 "\n",
                           session.current_trial,
                           t_us);
                }
                trial_state = TRIAL_RESET;
                state_start_time = xTaskGetTickCount();
                set_state_pins(trial_state);  // Update state pins
                break;
            
            case NON_REWARD_PERIOD:
                ESP_LOGI(TAG, "No reward dispensed.");
                vTaskDelay(pdMS_TO_TICKS(REWARD_DURATION_MS));  // Same delay as reward period
                trial_state = TRIAL_RESET;
                state_start_time = xTaskGetTickCount();
                { // send UART to PC log for end of trial
                    uint64_t t_us = esp_timer_get_time();
                    printf("EVENT,END,%d,%" PRIu64 "\n",
                           session.current_trial,
                           t_us);
                }
                set_state_pins(trial_state);  // Update state pins
                break;
                
            case TRIAL_RESET:
                ESP_LOGI(TAG, "TRIAL_RESET: Resetting lever");
                // Hide condition circles during reset.
                set_condition_circle_visibility(false, false);
                start_animation_to_position(0);
                vTaskDelay(pdMS_TO_TICKS(800));
                trial_state = TRIAL_INIT;
                state_start_time = xTaskGetTickCount();

                set_state_pins(trial_state);  // Update state pins
                break;
                
            default:
                ESP_LOGE(TAG, "Unknown trial state; resetting trial");
                trial_state = TRIAL_INIT;
                state_start_time = xTaskGetTickCount();
                set_state_pins(trial_state);  // Update state pins
                break;
        }
        
        // Safety: if a state takes too long, reset the trial.
        TickType_t current_time = xTaskGetTickCount();
        if ((current_time - state_start_time) > pdMS_TO_TICKS(10000)) {
            ESP_LOGW(TAG, "State stuck for too long in state %d; resetting trial", trial_state);
            trial_state = TRIAL_INIT;
            state_start_time = current_time;
            set_state_pins(trial_state);  // Update state pins
        }
        
        vTaskDelay(pdMS_TO_TICKS(STATE_TASK_PERIOD));
    }
}

//----------------------
// Main application entry point
void app_main(void) {
    ESP_LOGI(TAG, "Starting lever test application");
    
    // Initialize UART for motor communication
    ESP_ERROR_CHECK(uart_param_config(uart_num, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(uart_num, 25, 24, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(uart_num, 256, 0, 0, NULL, 0));
    
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
    
    // Create tasks with appropriate priorities
    ESP_LOGI(TAG, "Creating tasks");
    
    // Create encoder reading task (highest priority)
    xTaskCreate(encoder_read_task, "encoder_task", STACK_SIZE, NULL, 
                configMAX_PRIORITIES - 4, NULL);
    
    // Create UI update task
    xTaskCreate(ui_update_task, "ui_task", STACK_SIZE, NULL, 
                configMAX_PRIORITIES - 3, NULL);
    
    // Create trial state machine task (lower priority)
    xTaskCreate(trial_state_task, "trial_state_task", STACK_SIZE, NULL, 
                configMAX_PRIORITIES - 6, NULL);
    
    ESP_LOGI(TAG, "Initialization complete, starting main loop");
}