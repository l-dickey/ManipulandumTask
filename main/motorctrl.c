// motorctrl.c

#include "motorctrl.h"
#include "motor_init.h"
#include <math.h>
#include <stdint.h>
#include <stdlib.h>    // for abs()
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include <string.h>

#define MAX_OUTPUT 100.0f
#define MIN_OUTPUT -100.0f

#define MAX_STEPS  16   // 16 * 0.125ms = 2ms @ 125us steps, tune as needed


// viscous controller state
static float dt, tau_vel, B, last_vel_filt;
static int   last_pos;

// PID controller state
static float kp, ki, kd, integral, deriv;
static int   lastError, deadzone;

void motorctrl_init_viscous(float dt_call,
                            float tau_vel_call,
                            float B_call)
{
    dt            = dt_call;
    tau_vel       = tau_vel_call;
    B             = B_call;
    last_pos      = 0;
    last_vel_filt = 0.0f;
}

void motorctrl_set_viscous_B(float B_coeff)
{
    B = B_coeff;
}

float motorctrl_get_viscous_B(void)
{
    return B;
}

float motorctrl_viscous(int32_t encoder_count)
{
    int pos    = encoder_count;
    float v_raw = (pos - last_pos)/dt;
    last_pos   = pos;

    // low-pass filter
    float alpha = dt/(tau_vel + dt);
    last_vel_filt = alpha * v_raw + (1.0f - alpha) * last_vel_filt;

    float u = -B * last_vel_filt;
    if (u >  100.0f) u =  100.0f;
    if (u < -100.0f) u = -100.0f;
    return u;
}

void pid_init(float kp_call,
              float ki_call,
              float kd_call,
              float integral0,
              float deriv0,
              float dt_call,
              int   deadzone_call)
{
    kp        = kp_call;
    ki        = ki_call;
    kd        = kd_call;
    integral  = integral0;
    deriv     = deriv0;
    dt        = dt_call;
    deadzone  = deadzone_call;
    lastError = 0;
}

void pid_step(int32_t encoder_count,
              int32_t target_count)
{
    float pos   = (float)encoder_count;
    float error = (float)target_count - pos;

    if (fabsf(error) > (float)deadzone) {
        // P term
        float P = kp * error;

        // I term with tentative update (for windup protection)
        float tentative_integral = integral + error * dt;
        float I = ki * tentative_integral;

        // D term
        deriv = (error - lastError) / dt;
        float D = kd * deriv;

        // Raw output
        float u = P + I + D;

        // Saturation & windup check
        if (u > MAX_OUTPUT) {
            u = MAX_OUTPUT;
            // integral stays at previous value
        } else if (u < MIN_OUTPUT) {
            u = MIN_OUTPUT;
            // integral stays at previous value
        } else {
            // only update integral if not saturated
            integral = tentative_integral;
        }

        lastError = error;
        apply_control_mcpwm(u);
    }
    else {
        // inside deadzone: actively brake and clear integrator
        apply_control_mcpwm(0.0f);
        integral  = 0.0f;
        deriv     = 0.0f;
        lastError = 0;
    }
}

void pid_set_gains(float new_kp,
                   float new_ki,
                   float new_kd)
{
    kp = new_kp;
    ki = new_ki;
    kd = new_kd;
}

void pid_set_deadzone(int new_deadzone)
{
    deadzone = new_deadzone;
}

void pid_clear_state(void) {
    integral  = 0.0f;
    lastError = 0;
    deriv     = 0.0f;
}

// ===== Ramp state (ms tick) =====
static volatile float    s_u_cur = 0.0f;      // currently applied duty
static volatile float    s_u_tgt = 0.0f;      // target duty
static volatile float    s_step_per_tick = 0; // duty % per task period
static uint32_t          s_tick_ms = 1;       // task period in milliseconds
static TickType_t        s_period_ticks = 1;  // task period in FreeRTOS ticks

void motor_ramp_set(float pct, uint32_t ramp_ms)
{
    // Clamp to [0,100]
    if (pct < 0.0f) pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;

    const float u0 = s_u_cur;
    const float du = fabsf(pct - u0);

    s_u_tgt = pct;   // publish new target first (safe to do either order)

    if (du < 1e-6f) { s_step_per_tick = 0.0f; return; }

    // Convert desired ramp_ms into a number of task periods (ceil), min 1 tick.
    // NOTE: s_tick_ms is set by motor_ramp_start() before the task runs.
    uint32_t period_ms = (s_tick_ms == 0) ? 1 : s_tick_ms;
    uint32_t n_ticks   = (ramp_ms == 0) ? 1 : ( (ramp_ms + period_ms - 1) / period_ms );
    if (n_ticks == 0) n_ticks = 1;

    s_step_per_tick = du / (float)n_ticks;
}

static void motor_ramp_task(void *pv)
{
    // Period is fixed from motor_ramp_start(); clamp to >=1 tick
    TickType_t period = s_period_ticks;
    if (period < 1) period = 1;

    TickType_t next = xTaskGetTickCount();

    for (;;) {
        float tgt = s_u_tgt;
        float cur = s_u_cur;

        float diff = tgt - cur;
        float ad   = fabsf(diff);

        // Ensure forward progress even if step got set extremely small
        float step = s_step_per_tick;
        if (step <= 1e-6f && ad > 0.0f) step = ad;

        if (ad <= step) cur = tgt;
        else            cur += (diff > 0 ? step : -step);

        s_u_cur = cur;
        apply_control_mcpwm(cur);          // single writer to hardware

        vTaskDelayUntil(&next, period);    // guarantees idle time; no WDT
    }
}

void motor_ramp_start(uint32_t tick_ms)
{
    if (tick_ms == 0) tick_ms = 1;
    s_tick_ms      = tick_ms;
    s_period_ticks = pdMS_TO_TICKS(s_tick_ms);
    if (s_period_ticks < 1) s_period_ticks = 1;

    // Priority: moderate (e.g., 6) so IDLE and LVGL get time; don't pin to core.
    xTaskCreate(motor_ramp_task, "motor_ramp", 2048, NULL, 6, NULL);
}
