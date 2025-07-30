// motorctrl.c

#include "motorctrl.h"
#include "motor_init.h"
#include <math.h>
#include <stdlib.h>    // for abs()

#define MAX_OUTPUT 100.0f
#define MIN_OUTPUT -100.0f

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

