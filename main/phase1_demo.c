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

// ── Trial timing/visuals ──────────────────────────────────────────────────────
#define CUE_DURATION_MS     500
#define AUTOCOMPLETE_DELAY_MS 100   // wait this long after cue before moving
#define TRIAL_TIMEOUT_MS    3000
#define RESET_DELAY_MS      1000
#define STACK_SIZE          16384
#define UI_TASK_PERIOD_MS   10
#define SCREEN_WIDTH        1024
#define SCREEN_HEIGHT       600

// ── Reward / reset logic ──────────────────────────────────────────────────────
#define REWARD_HOLD_MS      50
#define RESET_THRESHOLD     5
#define RESET_HOLD_MS       100

// ── Autocomplete specs (your requests) ────────────────────────────────────────
#define AUTOCOMPLETE_ENABLED      1
#define AUTOTARGET_COUNTS         50     // threshold to reach (encoder counts)
#define AUTOCOMPLETE_SPEED_CPS    80.0f  // counts per second (~0.5–0.625 s to 50)

// If your encoder sign is opposite, set this to -1 to flip the target comparison.
#define ENCODER_SIGN              (+1)

// PID gains (unchanged unless you want tweaks)
static float kp = 0.21f;
static float ki = 0.001f;
static float kd = 0.003f;

// (Not used during MOVING—viscous field is disabled as requested)
static const float B_level[4] = {0.003f, 0.003f, 0.003f, 0.003f};

// ── Types / globals ───────────────────────────────────────────────────────────
typedef enum { TRIAL_CORRECT=0, TRIAL_TIMEOUT } trial_outcome_t;

static SemaphoreHandle_t encoder_mutex;
static volatile int32_t  current_encoder_value;

static lv_obj_t *grating1, *grating2, *grating3;
static lv_obj_t *lever_indicator;
static lv_obj_t *trial_info_label;
static lv_obj_t *create_grating_pattern(lv_obj_t *parent, int stripes);
static void hide_all_gratings(void);

static uint32_t trial_number;
static uint32_t session_correct;
static uint32_t session_total;

static bool motor_locked = false;

// cue frequencies for rewardType = 0..3
static const uint32_t cue_freqs[4] = { 500, 1000, 2000, 3000 };
static const uint32_t reward_freq = 5000;

// ── Helpers ───────────────────────────────────────────────────────────────────
static void send_trial_data(trial_outcome_t outcome,
                            uint32_t reaction_time_ms,
                            int32_t encoder_position)
{
    const char *out_str = (outcome==TRIAL_CORRECT) ? "CORRECT" : "TIMEOUT";
    printf("TRIAL,%s,%lu,%ld\n", out_str,
           (unsigned long)reaction_time_ms, (long)encoder_position);
    ESP_LOGI(TAG, "Trial %lu: %s, RT=%lums, Pos=%ld",
             trial_number, out_str, (long)reaction_time_ms, (long)encoder_position);
}

static void update_trial_display(void)
{
    if (!trial_info_label) return;
    if (!lvgl_lock(10)) return;

    float success = session_total ? ((float)session_correct/session_total)*100.0f : 0.0f;
    lv_label_set_text_fmt(trial_info_label,
        "Trial: %lu\nCorrect: %lu/%lu\nSuccess: %.1f%%",
        trial_number, session_correct, session_total, success);
    lvgl_unlock();
}

static lv_obj_t *create_grating_pattern(lv_obj_t *parent, int stripes)
{
    int sw = SCREEN_WIDTH / stripes;
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_remove_style_all(cont);
    lv_obj_set_size(cont, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(cont, lv_color_hex(0x000000), 0);

    for (int i = 0; i < stripes; i += 2) {
        lv_obj_t *s = lv_obj_create(cont);
        lv_obj_remove_style_all(s);
        lv_obj_set_size(s, sw, SCREEN_HEIGHT);
        lv_obj_set_pos(s, i*sw, 0);
        lv_obj_set_style_bg_color(s, lv_color_hex(0x00FF00), 0);
        lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
    }
    return cont;
}

static void hide_all_gratings(void)
{
    if (!lvgl_lock(10)) return;
    lv_obj_add_flag(grating1, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(grating2, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(grating3, LV_OBJ_FLAG_HIDDEN);
    lvgl_unlock();
}

static void show_grating_for(int reward)
{
    if (!lvgl_lock(10)) return;
    lv_obj_add_flag(grating1, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(grating2, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(grating3, LV_OBJ_FLAG_HIDDEN);
    if      (reward == 1) lv_obj_clear_flag(grating1, LV_OBJ_FLAG_HIDDEN);
    else if (reward == 2) lv_obj_clear_flag(grating2, LV_OBJ_FLAG_HIDDEN);
    else if (reward == 3) lv_obj_clear_flag(grating3, LV_OBJ_FLAG_HIDDEN);
    lvgl_unlock();
}

// ── Tasks ─────────────────────────────────────────────────────────────────────
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

void ui_update_task(void *pv)
{
    const TickType_t period = pdMS_TO_TICKS(UI_TASK_PERIOD_MS);
    TickType_t next = xTaskGetTickCount();

    while (1) {
        int32_t pos = 0;
        if (encoder_mutex) {
            xSemaphoreTake(encoder_mutex, portMAX_DELAY);
            pos = current_encoder_value * -1; // screen mapping as before
            xSemaphoreGive(encoder_mutex);
        }

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
    lv_obj_t *scr = lv_disp_get_scr_act(display);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);

    grating1 = create_grating_pattern(scr, 13);
    grating2 = create_grating_pattern(scr,  7);
    grating3 = create_grating_pattern(scr,  3);
    hide_all_gratings();

    lever_indicator = lv_obj_create(scr);
    lv_obj_remove_style_all(lever_indicator);
    lv_obj_set_size(lever_indicator, 50, 200);
    lv_obj_set_style_bg_color(lever_indicator, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(lever_indicator, LV_OPA_COVER, 0);
    lv_obj_set_pos(lever_indicator, SCREEN_WIDTH/2 - 25, SCREEN_HEIGHT/2 - 100);

    trial_info_label = lv_label_create(scr);
    lv_obj_set_pos(trial_info_label, 20, 20);
    lv_obj_set_style_text_color(trial_info_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_color(trial_info_label, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(trial_info_label, LV_OPA_70, 0);
    lv_obj_set_style_pad_all(trial_info_label, 10, 0);
    lv_label_set_text(trial_info_label, "Trial: 0\nCorrect: 0/0\nSuccess: 0.0%");
}

// ── Trial state task with AUTOCOMPLETE ────────────────────────────────────────
void simplified_trial_task(void *pv)
{
    const TickType_t loop_period = pdMS_TO_TICKS(2);  // 500 Hz control loop
    TickType_t next = xTaskGetTickCount();

    sm_state_t   state        = S_INIT;
    TickType_t   state_ts     = next;
    TickType_t   hold_ts      = 0;
    int          rewardType   = 0;
    const int32_t resetPos    = 0;
    bool         first_entry  = true;

    // Autocomplete setpoint bookkeeping
    float sp_current = 0.0f;        // moving setpoint (counts)
    float sp_target  = (float)(ENCODER_SIGN * AUTOTARGET_COUNTS);
    TickType_t ac_start_ts = 0;
    bool ac_started = false;

    while(1) {
        TickType_t now = xTaskGetTickCount();

        // reward pulse engine (if you’re using it elsewhere)
        reward_update(now);

        // latest encoder
        int32_t pos_raw;
        xSemaphoreTake(encoder_mutex, portMAX_DELAY);
        pos_raw = current_encoder_value;
        xSemaphoreGive(encoder_mutex);

        // signed position after optional flip
        int32_t pos = ENCODER_SIGN * pos_raw;

        switch(state) {
        // ───────────── INIT ─────────────
        case S_INIT:
            if (first_entry) {
                trial_number++;  session_total++;
                hide_all_gratings();
                rewardType   = rand() % 4;
                motor_locked = true;

                // Disable viscous field for this trial as requested
                motorctrl_init_viscous(0.0f, 0.0f, 0.0f);

                // Prepare PID
                pid_init(kp, ki, kd, 0, 0, 0.002f, 5);

                first_entry  = false;
            }

            // Cue immediately
            if (rewardType > 0) show_grating_for(rewardType);
            init_ledc(cue_freqs[rewardType]);
            state     = S_CUE;
            state_ts  = now;
            first_entry = true;
            break;

        // ───────────── CUE ──────────────
        case S_CUE:
            if (now - state_ts >= pdMS_TO_TICKS(CUE_DURATION_MS)) {
                // end cue audio/visuals
                ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
                ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, 0);
                hide_all_gratings();

                motor_locked = false;

                // Prime autocomplete
                sp_current = (float)pos;           // start where we are
                sp_target  = (float)(ENCODER_SIGN * AUTOTARGET_COUNTS);
                ac_started = false;
                ac_start_ts = now;                 // we’ll wait 100 ms
                state     = S_MOVING;
                state_ts  = now;
                first_entry = true;
            }
            break;

        // ───────────── MOVING (AUTOCOMPLETE) ────────────
        case S_MOVING: {
            // Wait AUTOCOMPLETE_DELAY_MS after cue before beginning ramp
            if (!ac_started && (now - ac_start_ts) >= pdMS_TO_TICKS(AUTOCOMPLETE_DELAY_MS)) {
                ac_started = true;
            }

            // Ramp setpoint toward target at AUTOCOMPLETE_SPEED_CPS
            if (AUTOCOMPLETE_ENABLED && ac_started) {
                float dt_s = (float)loop_period / 1000.0f; // 2 ms = 0.002 s
                float step = AUTOCOMPLETE_SPEED_CPS * dt_s;
                if (fabsf(sp_target - sp_current) <= step) {
                    sp_current = sp_target;
                } else {
                    sp_current += (sp_target > sp_current) ? step : -step;
                }
                // Drive PID toward the moving setpoint
                pid_step(pos, sp_current);
            } else {
                // Idle until ramp starts (no viscous control during moving)
                apply_control_mcpwm(0.0f);
            }

            // Threshold crossing & hold (>= target since target is positive)
            if (pos >= (int32_t)sp_target) {
                if (hold_ts == 0) hold_ts = now;
                else if (now - hold_ts >= pdMS_TO_TICKS(REWARD_HOLD_MS)) {
                    state     = S_REWARD;
                    state_ts  = now;
                    first_entry = true;
                    // Clean stop before reward pulses
                    apply_control_mcpwm(0.0f);
                }
            } else {
                hold_ts = 0;
            }

            // Timeout fallback
            if (now - state_ts > pdMS_TO_TICKS(TRIAL_TIMEOUT_MS)) {
                state     = S_TIMEOUT;
                state_ts  = now;
                first_entry = true;
                apply_control_mcpwm(0.0f);
            }
            break;
        }

        // ───────────── REWARD ────────────
        case S_REWARD: {
            static bool        se = true;
            static int         pulses_done;
            static bool        pin_state;
            static TickType_t  last_toggle;
            const  int         pulse_plus_one = rewardType + 1;

            const TickType_t PHASE = pdMS_TO_TICKS(500); // your existing 500 ms on/off

            if (pulse_plus_one <= 0) { // zero reward case
                if (se) { se=false; last_toggle=now; }
                else if (now - last_toggle >= PHASE) {
                    se=true; state=S_RESET; state_ts=now;
                }
                break;
            }

            if (se) {
                pulses_done = 0;
                pin_state   = true;
                gpio_set_level(GPIO_REWARD_SIGNAL, 1);
                init_ledc(reward_freq);
                last_toggle = now;
                se = false;
                break;
            }

            TickType_t dt = now - last_toggle;
            if (pin_state && dt >= PHASE) {
                gpio_set_level(GPIO_REWARD_SIGNAL, 0);
                ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
                ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, 0);
                pin_state   = false;
                last_toggle = now;
            } else if (!pin_state && dt >= PHASE) {
                pulses_done++;
                if (pulses_done < pulse_plus_one) {
                    gpio_set_level(GPIO_REWARD_SIGNAL, 1);
                    init_ledc(reward_freq);
                    pin_state   = true;
                    last_toggle = now;
                } else {
                    se = true;
                    state = S_RESET;
                    state_ts = now;
                }
            }
            break;
        }

        // ───────────── TIMEOUT ───────────
        case S_TIMEOUT:
            if (now - state_ts >= pdMS_TO_TICKS(500)) {
                state     = S_RESET;
                state_ts  = now;
                first_entry = true;
            }
            break;

        // ───────────── RESET (PID → 0) ─────────────
        case S_RESET: {
            static TickType_t home_hold_ts = 0;
            // ensure PID is targeting zero here
            pid_step(pos, resetPos);

            if (abs(pos - resetPos) <= RESET_THRESHOLD) {
                if (home_hold_ts == 0) home_hold_ts = now;
                if (now - home_hold_ts >= pdMS_TO_TICKS(RESET_HOLD_MS)) {
                    apply_control_mcpwm(0.0f);
                    send_trial_data(
                        (rewardType>0) ? TRIAL_CORRECT : TRIAL_TIMEOUT,
                        pdTICKS_TO_MS(now - state_ts),
                        pos
                    );
                    update_trial_display();

                    if (now - state_ts >= pdMS_TO_TICKS(RESET_DELAY_MS)) {
                        home_hold_ts = 0;
                        state = S_INIT;
                        state_ts = now;
                        first_entry = true;
                    }
                }
            } else {
                home_hold_ts = 0;
            }
            break;
        }
        }

        vTaskDelayUntil(&next, loop_period);
    }
}

// ── App main ──────────────────────────────────────────────────────────────────
void app_main(void)
{
    esp_log_level_set(TAG, ESP_LOG_INFO);
    ESP_LOGI(TAG, "Starting behavioral task (autocomplete)…");

    // TTL event encoder (unchanged)
    ESP_ERROR_CHECK(event_init_rmt(GPIO_EVENT_PIN, 1000000));

    // reward pin
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << GPIO_REWARD_SIGNAL,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    gpio_set_level(GPIO_REWARD_SIGNAL,0);

    // encoder + DAC
    encoder_mutex = xSemaphoreCreateMutex();
    init_encoder();
    ESP_ERROR_CHECK( encoder_out_init() );

    // motor (no viscous field during MOVING)
    init_mcpwm_highres();
    apply_control_mcpwm(0);
    motorctrl_init_viscous(0.0f, 0.0f, 0.0f);
    pid_init(kp, ki, kd, 0, 0, 0.002f, 5);

    // graphics
    lv_display_t *disp = lcd_init();
    bsp_set_lcd_backlight(1);
    if (lvgl_lock(100)) {
        create_simple_ui(disp);
        lv_timer_handler();
        lvgl_unlock();
    }

    // tasks
    xTaskCreate(encoder_read_task,    "enc",   4096, NULL, 6, NULL);
    xTaskCreate(ui_update_task,       "ui",    4096, NULL, 5, NULL);
    xTaskCreate(simplified_trial_task,"trial", STACK_SIZE, NULL, 7, NULL);

    // NOTE: removed the always-on pid_task that chased 0; PID is state-driven now.
}
