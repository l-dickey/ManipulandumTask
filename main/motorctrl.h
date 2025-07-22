// motorctrl.h
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Initialize just the viscous‐field generator.
 * @param  dt_s        Control‐loop interval, in seconds (e.g. 0.002f)  
 * @param  tau_vel_s   Time constant for the velocity low‐pass, in seconds  
 * @param  B_coeff     Viscosity coefficient (output %·s per count)  
 */
void motorctrl_init_viscous(float dt_s,
                            float tau_vel_s,
                            float B_coeff);

/**
 * @brief  Given the latest encoder count, compute a pure‐viscous torque.
 * @param  encoder_count   32-bit accumulated count from your encoder driver  
 * @returns Signed effort in –100…+100 (%) to pass to apply_control_mcpwm()
 */
float motorctrl_viscous(int32_t encoder_count);


/**
 * @brief  Initialize the position‐PID generator.
 * @param  kp            Proportional gain (% per count)  
 * @param  ki            Integral gain (%·s per count)  
 * @param  kd            Derivative gain (%·s per count)  
 * @param  integral0     Initial integral state (usually 0)  
 * @param  deriv0        Initial derivative state (usually 0)  
 * @param  dt_s          Control‐loop interval, in seconds  
 * @param  deadzone_cnt  Absolute error threshold (in encoder counts)  
 *                       inside which PID does nothing  
 */
void pid_init(float kp,
              float ki,
              float kd,
              float integral0,
              float deriv0,
              float dt_s,
              int   deadzone_cnt);

/**
 * @brief  Run one PID update and drive the motor for you.
 * @param  encoder_count  Latest encoder count  
 * @param  target_count   Desired setpoint (same units)  
 *
 * If |error| ≤ deadzone, this will hold integrator and output zero.
 * Otherwise it computes P+I+D and calls apply_control_mcpwm(u).
 */
void pid_step(int32_t encoder_count,
              int32_t target_count);

#ifdef __cplusplus
}
#endif
