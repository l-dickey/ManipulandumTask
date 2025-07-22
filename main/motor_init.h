#pragma once
#include <stdint.h>

void init_mcpwm_highres(void);
/**
    @brief initialize the MCPWM peripheral for motor driving via the VNH 5019A-E board
 */

void apply_control_mcpwm(float u);
 /**
    @brief drive the motor via the VNH 5019A-E board with direction and PWM
    @param u signed value of the duty cycle 0.00-100.0% 
  */