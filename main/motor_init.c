// main/motor_init.c

#include "motor_init.h"
#include "driver/gpio.h"
#include "driver/mcpwm.h"
#include "esp_err.h"
#include <math.h>

// motor driver board pins
#define PWM_GPIO    33   // PWM → MCPWM0A
#define INA_GPIO    53   // direction A
#define INB_GPIO    23  // direction B

#define MCPWM_UNIT   MCPWM_UNIT_0
#define MCPWM_TIMER  MCPWM_TIMER_0
#define MCPWM_OP     MCPWM_OPR_A

void init_mcpwm_highres(void) {
    // 1) Direction pins
    gpio_set_direction(INA_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_direction(INB_GPIO, GPIO_MODE_OUTPUT);

    // 2) Route PWM pin into MCPWM0A
    mcpwm_gpio_init(MCPWM_UNIT, MCPWM_OP, PWM_GPIO);

    // 3) Configure MCPWM timer for 18 kHz, but 0% on startup
    mcpwm_config_t cfg = {
        .frequency    = 18000,        // 18 kHz
        .cmpr_a       = 50,            // ← 0% duty (motor off)
        .cmpr_b       = 50,
        .duty_mode    = MCPWM_DUTY_MODE_0,
        .counter_mode = MCPWM_UP_COUNTER
    };
    ESP_ERROR_CHECK(mcpwm_init(MCPWM_UNIT, MCPWM_TIMER, &cfg));

    // 4) Stop the timer entirely so no PWM until you call apply_control_mcpwm
    ESP_ERROR_CHECK(mcpwm_stop(MCPWM_UNIT, MCPWM_TIMER));
}

void apply_control_mcpwm(float u) {
    // 1) Set direction pins
    if      (u > 0) { gpio_set_level(INA_GPIO, 1); gpio_set_level(INB_GPIO, 0); }
    else if (u < 0) { gpio_set_level(INA_GPIO, 0); gpio_set_level(INB_GPIO, 1); }
    else            { gpio_set_level(INA_GPIO, 0); gpio_set_level(INB_GPIO, 0); }

    // 2) If u==0, brake (stop PWM) and return immediately
    if (u == 0.0f) {
        ESP_ERROR_CHECK(mcpwm_stop(MCPWM_UNIT, MCPWM_TIMER));
        return;
    }

    // 3) Nonzero drive: ensure PWM timer is running
    ESP_ERROR_CHECK(mcpwm_start(MCPWM_UNIT, MCPWM_TIMER));

    // 4) Compute magnitude 0–100%
    float mag = fabsf(u);
    if (mag > 100.0f) mag = 100.0f;

    // 5) Update duty cycle
    ESP_ERROR_CHECK(
        mcpwm_set_duty(
            MCPWM_UNIT,
            MCPWM_TIMER,
            MCPWM_OP,
            mag      // 0.0–100.0%
        )
    );
}
