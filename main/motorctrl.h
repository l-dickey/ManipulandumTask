#ifndef MOTORCTRL_H
#define MOTORCTRL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ================================================================
//  Viscous Control
// ================================================================
void motorctrl_init_viscous(float dt_call,
                            float tau_vel_call,
                            float B_call);

void motorctrl_set_viscous_B(float B_coeff);
float motorctrl_get_viscous_B(void);
float motorctrl_viscous(int32_t encoder_count);

// ================================================================
//  PID Control
// ================================================================
void pid_init(float kp_call,
              float ki_call,
              float kd_call,
              float integral0,
              float deriv0,
              float dt_call,
              int   deadzone_call);

void pid_step(int32_t encoder_count, int32_t target_count);
void pid_set_gains(float new_kp, float new_ki, float new_kd);
void pid_set_deadzone(int new_deadzone);
void pid_clear_state(void);

// ================================================================
//  Motor Ramp Control
// ================================================================

void motor_ramp_start(uint32_t tick_ms);            // call once at boot (e.g., 1 or 2 ms)
void motor_ramp_set(float pct, uint32_t ramp_ms);   // smooth to pct over ~ramp_ms


#ifdef __cplusplus
}
#endif

#endif // MOTORCTRL_H
