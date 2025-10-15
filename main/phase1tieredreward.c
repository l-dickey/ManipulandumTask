#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <math.h>
#include "driver/pcnt_types_legacy.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_rom_sys.h"
#include "driver/pcnt.h"

#include "hal/gpio_types.h"
#include "lvgl.h"
#include "lv_conf.h"
#include "graphics.h"
#include "audio_pwm.c"
#include "peripheral_config.c"

#include "motor_init.h"
#include "motorctrl.h"
#include "encoder.h"
#include "encoder_out.h"
#include "event.h"
#include "reward.h"
#include "state_machine.h"

#include "driver/gpio.h"

#define TAG                 "PHASE1_TASK"
#define GPIO_REWARD_SIGNAL  3
#define GPIO_EVENT_PIN      4
#define ENCODER_THRESHOLD   -27
#define INTERTRIAL_DELAY    500
#define CUE_TONE_MS         500
#define CUE_DELAY_MIN_MS    250
#define CUE_DELAY_MAX_MS    450
#define GO_CUE_MS           100
#define GO_CUE_FREQ         4000
#define TRIAL_TIMEOUT_MS    3000
#define RESET_DELAY_MS      1000
#define STACK_SIZE          16384
#define UI_TASK_PERIOD_MS   10
#define SCREEN_WIDTH        1024
#define SCREEN_HEIGHT       600
#define REWARD_HOLD_MS      100
#define RESET_THRESHOLD     5
#define RESET_HOLD_MS       100
#define HANDLE_EARLY_CUE_REWARD 1 // yes
#define HIGH_TORQUE         85.0f
#define LOW_TORQUE          5.0f

// Grating parameters
#define GRATING_SPACING     200  // pixels between stripes
#define GRATING_WIDTH       120  // width of each stripe in pixels


typedef enum {
    TRIAL_CORRECT = 0,
    TRIAL_TIMEOUT
} trial_outcome_t;

// globals
static SemaphoreHandle_t encoder_mutex;
static volatile int32_t  current_encoder_value;

// Three separate canvas objects - one per grating
static lv_obj_t *grating_canvas[3];  // 0=90°, 1=45°, 2=135°
static lv_obj_t *lever_indicator;
static lv_obj_t *trial_info_label;

static uint32_t trial_number;
static uint32_t session_correct;
static uint32_t session_total;
static int  g_thresh_x = -1;     // pixel column of the encoder threshold
static bool g_mask_left = true;  // true = hide left of boundary; false = hide right

bool motor_locked = false;

// cue frequencies for rewardType = 1..3 (index 0..2)
static const uint32_t cue_freqs[3] = { 500, 1000, 2000 };
static const uint32_t reward_freq = 5000;

// send CSV over UART / printf
static void send_trial_data(trial_outcome_t outcome,
                            uint32_t reaction_time_ms,
                            int32_t encoder_position,
                            int reward_level)  // NEW PARAMETER
{
    const char *out_str = (outcome==TRIAL_CORRECT) ? "CORRECT" : "TIMEOUT";
    
    // NEW FORMAT: Added reward_level at the end
    printf("TRIAL,%s,%lu,%ld,%d\n",
           out_str,
           (unsigned long)reaction_time_ms,
           (long)encoder_position,
           reward_level);
           
    ESP_LOGI(TAG,
             "Trial %lu: %s, RT=%lums, Pos=%ld, Reward=%d",
             trial_number,
             out_str,
             (long)reaction_time_ms,
             (long)encoder_position,
             reward_level);
}

// update the on-screen stats
static void update_trial_display(void)
{
    if (!trial_info_label) return;
    if (!lvgl_lock(10)) return;

    float success = session_total
                  ? ((float)session_correct / session_total)*100.0f
                  : 0.0f;
    lv_label_set_text_fmt(trial_info_label,
        "Trial: %lu\nCorrect: %lu/%lu\n",
        trial_number,
        session_correct,
        session_total
        );
    lvgl_unlock();
}

static inline int enc_to_screen_x(int enc_counts) {
    const int32_t center = SCREEN_WIDTH/2;
    const int32_t span   = SCREEN_WIDTH/2 - 25;
    int32_t pos = -enc_counts;
    int32_t x   = center + (pos * span) / 200;
    if (x < 0) x = 0;
    if (x > SCREEN_WIDTH-1) x = SCREEN_WIDTH-1;
    return (int)x;
}

static void set_grating_threshold_from_counts(int threshold_counts, bool hide_left_side) {
    g_thresh_x  = enc_to_screen_x(threshold_counts);
    g_mask_left = hide_left_side;
}

static void draw_threshold_marker(lv_obj_t *canvas) {
    if (g_thresh_x < 0) return;
    lv_color_t green = lv_color_hex(0x00FF00);
    // A 2-px line for visibility
    for (int x = g_thresh_x; x <= g_thresh_x + 1 && x < SCREEN_WIDTH; x++) {
        for (int y = 0; y < SCREEN_HEIGHT; y++) {
            lv_canvas_set_px(canvas, x, y, green, LV_OPA_COVER);
        }
    }
}

// Pre-render a grating at a specific angle onto a canvas (called once at startup)
static void render_grating(lv_obj_t *canvas, int angle_deg)
{
    if (!canvas) return;

    // 1) Full black background
    lv_canvas_fill_bg(canvas, lv_color_hex(0x000000), LV_OPA_COVER);

    // 2) Draw angled grating, clipped at g_thresh_x
    float angle_rad  = (angle_deg * M_PI) / 180.0f;
    float cos_angle  = cosf(angle_rad);
    float sin_angle  = sinf(angle_rad);
    int   max_dim    = (int)sqrtf(SCREEN_WIDTH * SCREEN_WIDTH + SCREEN_HEIGHT * SCREEN_HEIGHT);
    int   num_lines  = (max_dim / GRATING_SPACING) * 2;
    int   cx         = SCREEN_WIDTH / 2;
    int   cy         = SCREEN_HEIGHT / 2;
    lv_color_t green = lv_color_hex(0x00FF00);
    int   half_width = GRATING_WIDTH / 2;

    float line_angle = angle_rad + M_PI / 2.0f;
    float perp_cos   = cosf(line_angle);
    float perp_sin   = sinf(line_angle);

    for (int i = -num_lines/2; i <= num_lines/2; i++) {
        int offset  = i * GRATING_SPACING;
        int start_x = cx + (int)(offset * cos_angle) - (int)(max_dim * perp_cos);
        int start_y = cy + (int)(offset * sin_angle) - (int)(max_dim * perp_sin);
        int end_x   = cx + (int)(offset * cos_angle) + (int)(max_dim * perp_cos);
        int end_y   = cy + (int)(offset * sin_angle) + (int)(max_dim * perp_sin);

        int dx = abs(end_x - start_x);
        int dy = abs(end_y - start_y);
        int sx = (start_x < end_x) ? 1 : -1;
        int sy = (start_y < end_y) ? 1 : -1;
        int err = dx - dy;

        int x = start_x, y = start_y;
        while (1) {
            // thicken line
            for (int w = -half_width; w <= half_width; w++) {
                int px = (dx > dy) ? x : (x + w);
                int py = (dx > dy) ? (y + w) : y;

                if (px >= 0 && px < SCREEN_WIDTH && py >= 0 && py < SCREEN_HEIGHT) {
                    bool on_hidden_side = g_mask_left ? (px < g_thresh_x) : (px > g_thresh_x);
                    if (!on_hidden_side) {
                        lv_canvas_set_px(canvas, px, py, green, LV_OPA_COVER);
                    }
                }
            }

            if (x == end_x && y == end_y) break;
            int e2 = 2 * err;
            if (e2 > -dy) { err -= dy; x += sx; }
            if (e2 <  dx) { err += dx; y += sy; }
        }
    }
    
    // Draw threshold marker
    draw_threshold_marker(canvas);
}

// Show a specific pre-rendered grating (0=90°, 1=45°, 2=135°)
static void show_grating(int grating_index)
{
    if (grating_index < 0 || grating_index >= 3) return;
    if (!lvgl_lock(10)) return;
    
    // Hide all gratings first
    for (int i = 0; i < 3; i++) {
        lv_obj_add_flag(grating_canvas[i], LV_OBJ_FLAG_HIDDEN);
    }
    
    // Show the requested one
    lv_obj_clear_flag(grating_canvas[grating_index], LV_OBJ_FLAG_HIDDEN);
    
    lvgl_unlock();
}

// Hide all gratings
static void hide_all_gratings(void)
{
    if (!lvgl_lock(10)) return;
    for (int i = 0; i < 3; i++) {
        lv_obj_add_flag(grating_canvas[i], LV_OBJ_FLAG_HIDDEN);
    }
    lvgl_unlock();
}

// hide the lever indicator
static void hide_lever_indicator(void)
{
    if (!lvgl_lock(10)) return;
    lv_obj_add_flag(lever_indicator, LV_OBJ_FLAG_HIDDEN);
    lvgl_unlock();
}

// show the lever indicator
static void show_lever_indicator(void)
{
    if (!lvgl_lock(10)) return;
    lv_obj_clear_flag(lever_indicator, LV_OBJ_FLAG_HIDDEN);
    lvgl_unlock();
}

// sample the PCNT every 5 ms and push to the DAC
void encoder_read_task(void *pv)
{
    const TickType_t period = pdMS_TO_TICKS(5);
    TickType_t next = xTaskGetTickCount();
    while (1) {
        int32_t val = read_encoder();
        if (encoder_mutex) {
            xSemaphoreTake(encoder_mutex, portMAX_DELAY);
            current_encoder_value = val;
            encoder_out_update(val);
            xSemaphoreGive(encoder_mutex);
        }
        vTaskDelayUntil(&next, period);
    }
}

// update lever graphic every UI_TASK_PERIOD_MS
void ui_update_task(void *pv)
{
    const TickType_t period = pdMS_TO_TICKS(UI_TASK_PERIOD_MS);
    TickType_t next = xTaskGetTickCount();

    while (1) {
        int32_t pos = 0;
        if (encoder_mutex) {
            xSemaphoreTake(encoder_mutex, portMAX_DELAY);
            pos = current_encoder_value*-1;
            xSemaphoreGive(encoder_mutex);
        }

        // map pos → screen X
        int32_t center = SCREEN_WIDTH/2;
        int32_t span   = SCREEN_WIDTH/2 - 25;
        int32_t x = center + (pos*span)/200;
        if (x < 25) x = 25;
        if (x > SCREEN_WIDTH-25) x = SCREEN_WIDTH-25;

        if (lvgl_lock(10)) {
            lv_obj_set_x(lever_indicator, x-25);
            lv_timer_handler();
            lvgl_unlock();
        }

        vTaskDelayUntil(&next, period);
    }
}


static void create_simple_ui(lv_display_t *display) {
    ESP_LOGI(TAG, "Creating UI with pre-rendered gratings...");
    
    // 1) black background
    lv_obj_t *scr = lv_disp_get_scr_act(display);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);

    // 2) Set up threshold position
    set_grating_threshold_from_counts(ENCODER_THRESHOLD, /*hide_left_side=*/false);
    
    // 3) Create THREE separate canvas objects with separate buffers
    int angles[3] = {90, 45, 135};
    for (int i = 0; i < 3; i++) {
        lv_color_t *buf = heap_caps_malloc(SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(lv_color_t), 
                                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!buf) {
            ESP_LOGE(TAG, "Failed to allocate canvas buffer %d!", i);
            return;
        }
        
        grating_canvas[i] = lv_canvas_create(scr);
        lv_canvas_set_buffer(grating_canvas[i], buf, SCREEN_WIDTH, SCREEN_HEIGHT, LV_COLOR_FORMAT_RGB565);
        
        // Pre-render the grating NOW
        ESP_LOGI(TAG, "Rendering grating %d (%d degrees)...", i, angles[i]);
        render_grating(grating_canvas[i], angles[i]);
        
        // Hide it initially
        lv_obj_add_flag(grating_canvas[i], LV_OBJ_FLAG_HIDDEN);
    }
    ESP_LOGI(TAG, "All gratings pre-rendered!");

    // 4) lever indicator in center (start hidden)
    lever_indicator = lv_obj_create(scr);
    lv_obj_remove_style_all(lever_indicator);
    lv_obj_set_size(lever_indicator, 50, 200);
    lv_obj_set_style_bg_color(lever_indicator, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(lever_indicator, LV_OPA_COVER, 0);
    lv_obj_set_pos(lever_indicator,
                   SCREEN_WIDTH/2 - 25,
                   SCREEN_HEIGHT/2 - 100);
    lv_obj_add_flag(lever_indicator, LV_OBJ_FLAG_HIDDEN);

    // 5) trial info label at top-left
    trial_info_label = lv_label_create(scr);
    lv_obj_set_pos(trial_info_label, 20, 15);
    lv_obj_set_style_text_color(trial_info_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_color(trial_info_label, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(trial_info_label, LV_OPA_70, 0);
    lv_obj_set_style_pad_all(trial_info_label, 5, 0);
    lv_label_set_text(trial_info_label,
        "Trial: 0\nCorrect: 0/0");
}


void simplified_trial_task(void *pv)
{
    const TickType_t loop_period = pdMS_TO_TICKS(2);  // 500 Hz
    TickType_t next = xTaskGetTickCount();

    sm_state_t   state        = S_INIT;
    TickType_t   state_ts     = next;
    TickType_t   hold_ts      = 0;
    int          rewardType   = 0;  // Now 1-3
    uint32_t     cue_delay_ms = 0;  // Random delay duration
    bool         first_entry  = true;
    
    // Track trial outcome and timing
    trial_outcome_t trial_outcome = TRIAL_CORRECT;
    TickType_t go_start_time = 0;
    TickType_t threshold_cross_time = 0;

    while(1) {
        TickType_t now = xTaskGetTickCount();

        // Always update the reward‐TTL engine first
        reward_update(now);

        // Sample encoder once per loop
        int32_t pos;
        xSemaphoreTake(encoder_mutex, portMAX_DELAY);
          pos = current_encoder_value;
        xSemaphoreGive(encoder_mutex);

        switch(state) {
        // ───────────── INIT ─────────────
        case S_INIT:
            if (first_entry) {
                trial_number++;  
                session_total++;
                hide_all_gratings();
                hide_lever_indicator();
                
                trial_outcome = TRIAL_CORRECT;
                go_start_time = 0;
                threshold_cross_time = 0;
                
                rewardType = rand() % 3; // reward 1-3 on 0 indexing
                cue_delay_ms = CUE_DELAY_MIN_MS + 
                               (esp_random() % (CUE_DELAY_MAX_MS - CUE_DELAY_MIN_MS + 1));
                
                motor_locked = true;
                first_entry  = false;
            }
            
            if (now - state_ts >= pdMS_TO_TICKS(250)) {
                sm_enter(S_CUE, CUE_EVENT[rewardType]);
                state     = S_CUE;
                state_ts  = now;
                first_entry = true;
            }
            break;

        // ───────────── CUE ──────────────
        case S_CUE:
            if (first_entry) {
                // Simply show the pre-rendered grating (rewardType 1-3 maps to index 0-2)
                show_grating(rewardType);
                
                // Start audio
                init_ledc(cue_freqs[rewardType]);
                first_entry = false;
            }

        #if HANDLE_EARLY_CUE_REWARD
            // Early-response path during entire CUE period
            if (pos < ENCODER_THRESHOLD) {
                if (hold_ts == 0) hold_ts = now;
                else if (now - hold_ts >= pdMS_TO_TICKS(REWARD_HOLD_MS)) {
                    threshold_cross_time = now;
                    
                    // Stop audio
                    ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
                    ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, 0);
                    
                    show_lever_indicator();
                    (void)event_send_state_immediate(REW_EVENT[rewardType]);

                    sm_enter_no_emit(S_REWARD);
                    state       = S_REWARD;
                    state_ts    = now;
                    first_entry = true;
                    hold_ts     = 0;
                    break;
                }
            } else {
                hold_ts = 0;
            }
        #endif

            // After CUE_TONE_MS, stop audio
            if (now - state_ts >= pdMS_TO_TICKS(CUE_TONE_MS)) {
                ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
                ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, 0);
            }

            // After tone + delay, transition to GO
            if (now - state_ts >= pdMS_TO_TICKS(CUE_TONE_MS + cue_delay_ms)) {
                motor_locked = false;
                
                sm_enter(S_GO, GO);
                state       = S_GO;
                state_ts    = now;
                go_start_time = now;
                first_entry = true;
            }
            break;

        // ───────────── GO ────────────
        case S_GO:
            if (first_entry) {
                // Play go cue tone
                init_ledc(GO_CUE_FREQ);
                show_lever_indicator();
                first_entry = false;
            }
            
            // Stop go cue tone after GO_CUE_MS
            if (now - state_ts >= pdMS_TO_TICKS(GO_CUE_MS)) {
                ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
                ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, 0);
            }
            motor_ramp_set(LOW_TORQUE, 2);
             
            // Check for threshold crossing
            if (pos < ENCODER_THRESHOLD) {
                if (hold_ts == 0) hold_ts = now;
                else if (now - hold_ts >= pdMS_TO_TICKS(REWARD_HOLD_MS)) {
                    threshold_cross_time = now;
                    trial_outcome = TRIAL_CORRECT;
                    
                    sm_enter(S_REWARD, REW_EVENT[rewardType]);
                    state     = S_REWARD;
                    state_ts  = now;
                    first_entry = true;
                    hold_ts   = 0;
                }
            } else {
                hold_ts = 0;
            }
            
            // Timeout check
            if (now - state_ts > pdMS_TO_TICKS(TRIAL_TIMEOUT_MS)) {
                trial_outcome = TRIAL_TIMEOUT;
                threshold_cross_time = 0;
                
                sm_enter(S_TIMEOUT, TIMEOUT);
                state     = S_TIMEOUT;
                state_ts  = now;
                first_entry = true;
            }
            break;

       // ───────────── REWARD ────────────
        case S_REWARD: {
            static int        pulses_done;
            static bool       pin_high;
            static TickType_t last_toggle;

            const int        pulses_plus_one = rewardType +1 ;
            const TickType_t PHASE_MS        = pdMS_TO_TICKS(500);

            if (first_entry) {
                first_entry = false;

                pulses_done = 0;
                pin_high    = true;
                gpio_set_level(GPIO_REWARD_SIGNAL, 1);
                init_ledc(reward_freq);
                last_toggle = now;
                
                session_correct++;
                break;
            }

            TickType_t dt = now - last_toggle;

            if (pin_high && dt >= PHASE_MS) {
                gpio_set_level(GPIO_REWARD_SIGNAL, 0);
                ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
                ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, 0);
                pin_high   = false;
                last_toggle = now;
            } else if (!pin_high && dt >= PHASE_MS) {
                pulses_done++;
                if (pulses_done < pulses_plus_one) {
                    gpio_set_level(GPIO_REWARD_SIGNAL, 1);
                    init_ledc(reward_freq);
                    pin_high    = true;
                    last_toggle = now;
                } else {
                    first_entry = true;
                    sm_enter(S_RESET, RESET);
                    state     = S_RESET;
                    state_ts  = now;
                }
            }
            break;
        }

        // ───────────── TIMEOUT ───────────
        case S_TIMEOUT:
            if (first_entry) {
                first_entry = false;
            }
            
            if (now - state_ts >= pdMS_TO_TICKS(500)) {
                sm_enter(S_RESET, RESET);
                state     = S_RESET;
                state_ts  = now;
                first_entry = true;
            }
            break;

        // ───────────── RESET ─────────────
        case S_RESET:
            if (first_entry) {
                hide_all_gratings();
                
                uint32_t reaction_time_ms = 0;
                if (trial_outcome == TRIAL_CORRECT && go_start_time > 0 && threshold_cross_time > 0) {
                    reaction_time_ms = pdTICKS_TO_MS(threshold_cross_time - go_start_time);
                }
                
                send_trial_data(trial_outcome, reaction_time_ms, pos, rewardType);
                update_trial_display();
                
                motor_ramp_set(HIGH_TORQUE, 100);
                first_entry = false;
            }
            
            if (pos >= 0) {
                if (now - state_ts >= pdMS_TO_TICKS(RESET_DELAY_MS)) {
                    sm_enter(S_INIT, INIT);
                    state     = S_INIT;
                    state_ts  = now;
                    first_entry = true;
                }
            }
            break;
        }

        vTaskDelayUntil(&next, loop_period);
    }
}

void app_main(void)
{
    esp_log_level_set(TAG, ESP_LOG_INFO);
    ESP_LOGI(TAG, "Starting behavioral task…");

    // Setup reward pin
    ESP_ERROR_CHECK(event_init_rmt(GPIO_EVENT_PIN, 1000000));

    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << GPIO_REWARD_SIGNAL,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    gpio_set_level(GPIO_REWARD_SIGNAL, 0);
    
    // Encoder + DAC
    encoder_mutex = xSemaphoreCreateMutex();
    init_encoder();
    ESP_ERROR_CHECK(encoder_out_init());

    // Motor
    init_mcpwm_highres();
    motor_ramp_start(1);
    apply_control_mcpwm(0);
    
    // Graphics - This will pre-render all three gratings
    ESP_LOGI(TAG, "Initializing display and pre-rendering gratings...");
    lv_display_t *disp = lcd_init();
    bsp_set_lcd_backlight(1);
    if (lvgl_lock(100)) {
        create_simple_ui(disp);
        lv_timer_handler();
        lvgl_unlock();
    }
    ESP_LOGI(TAG, "Display initialized, gratings ready!");

    // Tasks
    xTaskCreate(encoder_read_task,    "enc",   4096, NULL, 6, NULL);
    xTaskCreate(ui_update_task,       "ui",    4096, NULL, 5, NULL);
    xTaskCreate(simplified_trial_task,"trial", STACK_SIZE, NULL, 7, NULL);
}