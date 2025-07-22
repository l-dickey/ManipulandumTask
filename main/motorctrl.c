#include "motorctrl.h"
#include "motor_init.h"
#include <math.h>

// variables for speed and control
static float dt, tau_vel, B, last_vel_filt;
static float kp, ki, kd, integral, deriv;
static int last_pos, targCnt, lastError, deadzone;

void motorctrl_init_viscous(float dt_call, float tau_vel_call, float B_call){
        dt           = dt_call;
        tau_vel      = tau_vel;
        B            = B_call;
        last_pos     = 0;
        last_vel_filt= 0.0f;
}

float motorctrl_viscous(int encoder_count){
    int pos = (int)encoder_count;
    float v_raw = (pos-last_pos)/dt;
    last_pos = pos;

    // lowpass filter for the velocity v[n] = α·v_raw + (1-α)·v[n-1]
    float alpha = dt/(tau_vel+dt);
    last_vel_filt = alpha *v_raw + (1.0f-alpha) * last_vel_filt;


    float u = -B * last_vel_filt;

    if (u > 100.0f) u = 100.0f;
    if (u < -100.0f) u = -100.0f;

    return u;
}

void pid_init(float kp_call, float ki_call, float kd_call, float integral_call, float deriv_call, float dt_call, int deadzone_call){
       kp            = kp_call;
       ki            = ki_call;
       kd            = kd_call;
       integral      = integral_call;
       deriv         = deriv_call;
       dt            = dt_call;
       deadzone      = deadzone_call;      
}

void pid_step(int encoder_count, int target_count){
    int pos = (int)encoder_count;
    int error = (int)target_count - pos;

    if (abs(error)>deadzone){ // if the abs(error) is greater than the deadzone, act
    
    float P = kp * error; // P term

    integral += error * dt;
    float I = ki * integral; // I term

    deriv = (error - lastError)/dt; // D term
    float D = kd * deriv;

    float u = P + I + D; // sum the PID to get the pwm value

    lastError = error; // set the last error to next timestamp (t-1 -> t(k-1) where k is time step)

    apply_control_mcpwm(u); // send the value for control
    }
}