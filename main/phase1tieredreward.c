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
#define ENCODER_THRESHOLD   -35
#define CUE_DURATION_MS     500
#define TRIAL_TIMEOUT_MS    3000
#define RESET_DELAY_MS      1000
#define STACK_SIZE          16384
#define UI_TASK_PERIOD_MS   10
#define SCREEN_WIDTH        1024
#define SCREEN_HEIGHT       600
#define REWARD_HOLD_MS 50 // how long to hold past encoder count thresh.
#define RESET_THRESHOLD    5    // only consider “home” if within ±5 counts of zero
#define RESET_HOLD_MS     50    // must hold for 20 ms before we call it done


static const float B_level[4] = {0.03f, 0.03f, 0.03f, 0.03f}; // set the levels of B coeff for vsicous force fields
// -----------------------------------------------------------------------------
// global flag for PID‐homing
static volatile bool homing_active = false;
// -----------------------------------------------------------------------------


typedef enum {
    TRIAL_CORRECT = 0,
    TRIAL_TIMEOUT
} trial_outcome_t;

// globals
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

bool motor_locked = false;

// cue frequencies for rewardType = 0..3
static const uint32_t cue_freqs[4] = { 1000, 2000, 3000, 4000 };

// send CSV over UART / printf
static void send_trial_data(trial_outcome_t outcome,
                            uint32_t reaction_time_ms,
                            int32_t encoder_position)
{
    const char *out_str = (outcome==TRIAL_CORRECT) ? "CORRECT" : "TIMEOUT";
    printf("TRIAL,%s,%lu,%ld\n",
           out_str,
           (unsigned long)reaction_time_ms,
           (long)encoder_position);
    ESP_LOGI(TAG,
             "Trial %lu: %s, RT=%lums, Pos=%ld",
             trial_number,
             out_str,
             (long)reaction_time_ms,
             (long)encoder_position);
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
        "Trial: %lu\nCorrect: %lu/%lu\nSuccess: %.1f%%",
        trial_number,
        session_correct,
        session_total,
        success);
    lvgl_unlock();
}

// create a grating container with `stripes` green bars
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

// hide all three gratings
static void hide_all_gratings(void)
{
    if (!lvgl_lock(10)) return;
    lv_obj_add_flag(grating1, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(grating2, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(grating3, LV_OBJ_FLAG_HIDDEN);
    lvgl_unlock();
}

// show only the grating for rewardType 1..3, none for 0
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

// play audio + visual during cue
static void play_audio_and_visual_cue(uint32_t freq, uint32_t ms)
{
    if (lvgl_lock(10)) {
        show_grating_for(0);  // temporarily show something? optional
        lvgl_unlock();
    }
    init_ledc(freq);
    vTaskDelay(pdMS_TO_TICKS(ms));
    ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
    ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, 0);
    if (lvgl_lock(10)) {
        hide_all_gratings();
        lvgl_unlock();
    }
}

static void create_simple_ui(lv_display_t *display) {
    // 1) black background
    lv_obj_t *scr = lv_disp_get_scr_act(display);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);

    // 2) create three gratings for reward levels 1..3
    //    (stripe counts: 3, 7, and 13 are from your earlier design)
    grating1 = create_grating_pattern(scr,  3);
    grating2 = create_grating_pattern(scr,  7);
    grating3 = create_grating_pattern(scr, 13);
    hide_all_gratings();  // start hidden

    // 3) lever indicator in center
    lever_indicator = lv_obj_create(scr);
    lv_obj_remove_style_all(lever_indicator);
    lv_obj_set_size(lever_indicator, 50, 200);
    lv_obj_set_style_bg_color(lever_indicator, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(lever_indicator, LV_OPA_COVER, 0);
    lv_obj_set_pos(lever_indicator,
                   SCREEN_WIDTH/2 - 25,
                   SCREEN_HEIGHT/2 - 100);

    // 4) trial info label at top-left
    trial_info_label = lv_label_create(scr);
    lv_obj_set_pos(trial_info_label, 20, 20);
    lv_obj_set_style_text_color(trial_info_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_color(trial_info_label, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(trial_info_label, LV_OPA_70, 0);
    lv_obj_set_style_pad_all(trial_info_label, 10, 0);
    lv_label_set_text(trial_info_label,
        "Trial: 0\nCorrect: 0/0\nSuccess: 0.0%");
}

static void pid_task(void *pv)
{
    const TickType_t period = pdMS_TO_TICKS(2); // 500 Hz
    TickType_t next = xTaskGetTickCount();

    while (1) {
        // grab the latest encoder count under mutex
        int32_t pos;
        xSemaphoreTake(encoder_mutex, portMAX_DELAY);
          pos = current_encoder_value;
        xSemaphoreGive(encoder_mutex);

        // always home toward zero during RESET state only:
        // (you can gate this with a flag if needed)
        pid_step(pos, 0);

        vTaskDelayUntil(&next, period);
    }
}

void simplified_trial_task(void *pv)
{
    const TickType_t loop_period = pdMS_TO_TICKS(2);  // 500 Hz
    TickType_t next = xTaskGetTickCount();

    sm_state_t   state        = S_INIT;
    TickType_t   state_ts     = next;
    TickType_t   hold_ts      = 0;   // for REWARD_HOLD_MS
    TickType_t   reset_hold_ts= 0;   // for RESET_HOLD_MS
    bool         reset_ready  = false;
    int          rewardType   = 0;
    const int32_t targetPos   = 0;

    while (1) {
        TickType_t now = xTaskGetTickCount();

        // 1) sample encoder
        int32_t pos;
        xSemaphoreTake(encoder_mutex, portMAX_DELAY);
          pos = current_encoder_value;
        xSemaphoreGive(encoder_mutex);

        // 2) update any running reward logic
        reward_update(now);

        switch (state) {
            // ───────────────── INIT ───────────────────
            case S_INIT:
                homing_active = false;
                reset_ready   = false;
                trial_number++;
                session_total++;
                hide_all_gratings();

                rewardType  = rand() % 4;
                motor_locked = true;

                // re‐init viscous model for next MOVING
                motorctrl_init_viscous(0.002f, 0.02f, B_level[rewardType]);

                sm_enter(S_CUE, CUE_EVENT[rewardType]);
                state    = S_CUE;
                state_ts = now;
                break;

            // ───────────────── CUE ────────────────────
            case S_CUE:
                if (rewardType > 0) show_grating_for(rewardType);
                init_ledc(cue_freqs[rewardType]);
                vTaskDelay(pdMS_TO_TICKS(CUE_DURATION_MS));
                ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
                ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, 0);
                hide_all_gratings();

                motor_locked  = false;
                homing_active = false;
                sm_enter(S_MOVING, MOVING);
                state    = S_MOVING;
                state_ts = now;
                hold_ts  = 0;
                break;

            // ───────────────── MOVING ─────────────────
            case S_MOVING: {
                float u = motor_locked ? 0.0f : motorctrl_viscous(pos);
                apply_control_mcpwm(u);

                if (pos < ENCODER_THRESHOLD) {
                    if (hold_ts == 0) {
                        hold_ts = now;
                    } else if (now - hold_ts >= pdMS_TO_TICKS(REWARD_HOLD_MS)) {
                        reward_start(rewardType);
                        session_correct += (rewardType>0);

                        motor_locked = true;
                        homing_active = false;
                        sm_enter(S_REWARD, REW_EVENT[rewardType]);
                        state    = S_REWARD;
                        state_ts = now;
                    }
                } else {
                    hold_ts = 0;
                }

                // timeout?
                if (now - state_ts > pdMS_TO_TICKS(TRIAL_TIMEOUT_MS)) {
                    motor_locked = true;
                    homing_active = false;
                    sm_enter(S_TIMEOUT, TIMEOUT);
                    state    = S_TIMEOUT;
                    state_ts = now;
                }
                break;
            }

            // ───────────────── REWARD ────────────────
            case S_REWARD:
                if (!reward_active()) {
                    sm_enter(S_RESET, RESET);
                    state    = S_RESET;
                    state_ts = now;
                    reset_hold_ts = 0;
                    reset_ready   = false;

                    // arm PID‐homing
                    pid_init(0.21f, 0.01f, 0.001f, 0, 0, 0.002f, 5);
                    homing_active = true;
                    printf(">> RESET: homing started\n");
                }
                break;

            // ───────────────── TIMEOUT ───────────────
            case S_TIMEOUT:
                if (now - state_ts > pdMS_TO_TICKS(500)) {
                    sm_enter(S_RESET, RESET);
                    state    = S_RESET;
                    state_ts = now;
                    reset_hold_ts = 0;
                    reset_ready   = false;

                    // arm PID‐homing
                    pid_init(0.21f, 0.01f, 0.001f, 0, 0, 0.002f, 5);
                    homing_active = true;
                    printf(">> RESET (timeout): homing started\n");
                }
                break;

            // ───────────────── RESET ─────────────────
            case S_RESET:
                // just monitor position — PID is running in pid_task()
                if (fabsf((float)pos - (float)targetPos) <= RESET_THRESHOLD) {
                    if (!reset_ready) {
                        reset_ready = true;
                        reset_hold_ts = now;
                        printf("RESET: within ±%d, starting hold\n", RESET_THRESHOLD);
                    }
                    else if (now - reset_hold_ts >= pdMS_TO_TICKS(RESET_HOLD_MS)) {
                        // done homing
                        homing_active = false;
                        apply_control_mcpwm(0);
                        printf("RESET: homing complete at pos=%ld\n", (long)pos);

                        send_trial_data(
                          (rewardType>0) ? TRIAL_CORRECT : TRIAL_TIMEOUT,
                          pdTICKS_TO_MS(now - state_ts),
                          pos
                        );
                        update_trial_display();

                        // wait RESET_DELAY_MS then re‐INIT
                        if (now - state_ts > pdMS_TO_TICKS(RESET_DELAY_MS)) {
                            sm_enter(S_INIT, INIT);
                            state    = S_INIT;
                            state_ts = now;
                        }
                    }
                } else {
                    // left the tolerance zone
                    if (reset_ready) {
                        printf("RESET: left tolerance, restarting hold\n");
                    }
                    reset_ready = false;
                    reset_hold_ts = 0;
                }
                break;
        } // end switch

        vTaskDelayUntil(&next, loop_period);
    }
}


void app_main(void)
{
    esp_log_level_set(TAG, ESP_LOG_INFO);
    ESP_LOGI(TAG, "Starting behavioral task…");

    // setup reward pin
    reward_init(GPIO_REWARD_SIGNAL);
    ESP_ERROR_CHECK(event_init_rmt(GPIO_EVENT_PIN, 1000000));

    // encoder + DAC
    encoder_mutex = xSemaphoreCreateMutex();
    init_encoder();
    ESP_ERROR_CHECK( encoder_out_init() );

    // motor
    init_mcpwm_highres();
    apply_control_mcpwm(0);
    motorctrl_init_viscous(0.002f, 0.02f, 0.02f);
    pid_init(0.21, 0.01,0.001,0,0,0.002,5);

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
    xTaskCreate(simplified_trial_task,"trial", STACK_SIZE, NULL, 5, NULL);
    xTaskCreatePinnedToCore(
    pid_task,
    "pid",     // name
    4096,      // stack
    NULL,      // arg
    /*prio=*/7, 
    NULL,
    /*core=*/0
);
}
