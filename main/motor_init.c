#include "motor_init.h"
#include "driver/gpio.h"
#include "driver/mcpwm.h"
#include "esp_err.h"
#include <math.h>

// motor driver board pins
#define PWM_GPIO    18 // gpio for pwm signaling
#define INA_GPIO    19 // gpio for cw movement  A: 1, B: 0
#define INB_GPIO    21 // gpio for ccw movement A: 0, B: 1

#define MCPWM_UNIT  MCPWM_UNIT_0
#define MCPWM_TIMER MCPWM_TIMER_0
#define MCPWM_OP    MCPWM_OPR_A


void init_mcpwm_highres(void) {
    // Direction pins
    gpio_set_direction(INA_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_direction(INB_GPIO, GPIO_MODE_OUTPUT);

    // Route the PWM pin into MCPWM0A
    mcpwm_gpio_init(MCPWM_UNIT_0, MCPWM0A, PWM_GPIO);

    // Configure MCPWM0 timer 0 for 1 kHz
    mcpwm_config_t cfg = {
        .frequency    = 18000,             // duty cycle freq.
        .cmpr_a       = 50,                // compare value, i.e, howlong the value stays high
        .cmpr_b       = 50,
        .duty_mode    = MCPWM_DUTY_MODE_0,
        .counter_mode = MCPWM_UP_COUNTER
    };
    ESP_ERROR_CHECK(mcpwm_init(MCPWM_UNIT_0, MCPWM_TIMER_0, &cfg));
}

void apply_control_mcpwm(float u) {
    // 1) Direction: sign(u)>0 → forward; <0 → reverse; =0 → coast
    if      (u > 0) { gpio_set_level(INA_GPIO,0); gpio_set_level(INB_GPIO,1); }
    else if (u < 0) { gpio_set_level(INA_GPIO,1); gpio_set_level(INB_GPIO,0); }
    else            { gpio_set_level(INA_GPIO,0); gpio_set_level(INB_GPIO,0); }

    // 2) Magnitude: take absolute value of u (–100…+100 → 0…100)
    float mag = fabsf(u);
    // mag*=mag;
    if (mag > 100.0f) mag = 100.0f;  // clamp at 100%

    // 3) Update duty: 'mag' percent of the 80 000-tick period
    ESP_ERROR_CHECK(
      mcpwm_set_duty(
        MCPWM_UNIT_0,
        MCPWM_TIMER_0,
        MCPWM_OPR_A,
        mag               // 0.0–100.0 maps to 0–80 000 ticks
      )
    );
}
