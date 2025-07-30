#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "driver/mcpwm_types_legacy.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/pcnt.h"
#include "driver/gpio.h"
#include "driver/mcpwm.h"
#include "driver/uart.h"
#include "portmacro.h"

// encoder pins
#define ENC_A_GPIO   24 // gpio pin for encode channel A
#define ENC_B_GPIO   25 // gpio pin for ebncoder channel B
#define PCNT_UNIT     PCNT_UNIT_0 // select peripheral channel for counting

// motor driver board pins
#define PWM_GPIO    33 // gpio for pwm signaling
#define INA_GPIO    53 // gpio for cw movement  A: 1, B: 0
#define INB_GPIO    23 // gpio for ccw movement A: 0, B: 1

#define MCPWM_UNIT  MCPWM_UNIT_0
#define MCPWM_TIMER MCPWM_TIMER_0
#define MCPWM_OP    MCPWM_OPR_A

#define U_MIN   0.18f // minimum duty cycle to drive the motor.

// UART settings for serial communication
#define UART_NUM UART_NUM_0
#define BUF_SIZE 1024

// Software accumulator state
static int32_t totalCount = 0; // initialize counter
static int16_t lastCnt   = 0; // initialize t-1 count.
static int16_t raw       = 0; // initialize counter at t
static int lastError     = 0; // error from t-1
static float filtered_pos = 0; // filtered value at t-1
static float integral; // integral of the error
static float dt = 0.002f; // 2ms

//Set the PID constant gains - these will be updated via serial
static float kp = 0.1;
static float ki = 0.00;
static float kd = 0.00; 
static int deadzone = 10;
static int targetPos = 0;

// set up the viscous force field terms
static float last_pos = 0;
static float last_vel_filt = 0; 
const float tau_vel = 0.02f; // 20ms time constant
static float B = 0.00f; // velocity
static float vel_dead = 0.0f; // velocity deadband 

// System control flags
static bool pid_enabled = true;
static bool system_reset_requested = false;

// Read & accumulate the 16-bit PCNT counter into a 32-bit total
static int32_t read_encoder(void) {
    pcnt_get_counter_value(PCNT_UNIT, &raw);
    int16_t delta = raw - lastCnt; // delta is current - last ex. 255 - 256 = -1
    totalCount  += delta; // total count(256) + delat(-1) = 255
    lastCnt      = raw; // set the new t-1 count to be raw
    return totalCount;
}

// Initialize quadrature counting on ENC_A/ENC_B
static void init_encoder(void) {
    // pull-ups so inputs never float
    gpio_set_pull_mode(ENC_A_GPIO, GPIO_PULLUP_ONLY); // each encoder pulse pulls it low
    gpio_set_pull_mode(ENC_B_GPIO, GPIO_PULLUP_ONLY); // same here

    // Channel 0: count A pulses, B selects direction
    pcnt_config_t cfg = {
        .pulse_gpio_num = ENC_A_GPIO, // allow esp to count pulses from this cahn
        .ctrl_gpio_num  = ENC_B_GPIO, // use direction from this pin
        .channel        = PCNT_CHANNEL_0, // use this hardware channel
        .unit           = PCNT_UNIT, //  name of the unit
        .pos_mode       = PCNT_COUNT_INC, // on rising edge, count up
        .neg_mode       = PCNT_COUNT_DEC, // on flaling edge count down
        .lctrl_mode     = PCNT_MODE_KEEP, // if B is low, use the count method above
        .hctrl_mode     = PCNT_MODE_REVERSE, // if B is high, reverse the count method
        .counter_h_lim  = INT16_MAX, // 16bit wrap if the hardware counter resets, unlilkely given our range
        .counter_l_lim  = INT16_MIN, // same but oppostie
    };
    ESP_ERROR_CHECK( pcnt_unit_config(&cfg) );

    // Channel 1: swap A/B for full quadrature
    cfg.channel        = PCNT_CHANNEL_1; // set up another channel for full quadrature
    cfg.pulse_gpio_num = ENC_B_GPIO; // read pulses of B
    cfg.ctrl_gpio_num  = ENC_A_GPIO; // use A for direction
    cfg.lctrl_mode     = PCNT_MODE_REVERSE; // as written above but opposite, if A is low, use above count method
    cfg.hctrl_mode     = PCNT_MODE_KEEP; // if A is high, reverse is
    ESP_ERROR_CHECK( pcnt_unit_config(&cfg) );

    // Optional noise filter (in APB clock cycles) Advanced Periopheral Bus
    pcnt_set_filter_value(PCNT_UNIT, 100); // any signal shorter than this val ~12.5ns, don't count
    pcnt_filter_enable    (PCNT_UNIT); // enable the filerter

    // Start from zero
    pcnt_counter_pause (PCNT_UNIT); 
    pcnt_counter_clear (PCNT_UNIT);
    pcnt_counter_resume(PCNT_UNIT);

    // Initialize software state
    lastCnt    = 0;
    totalCount = 0;
}

void init_mcpwm_highres(void) {
    // Direction pins
    gpio_set_direction(INA_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_direction(INB_GPIO, GPIO_MODE_OUTPUT);

    // Route the PWM pin into MCPWM0A
    mcpwm_gpio_init(MCPWM_UNIT_0, MCPWM0A, PWM_GPIO);

    // Configure MCPWM0 timer 0 for 18 kHz
    mcpwm_config_t cfg = {
        .frequency    = 18000,             // 18 kHz
        .cmpr_a       = 50,                // start at 0%
        .cmpr_b       = 50,
        .duty_mode    = MCPWM_DUTY_MODE_0,
        .counter_mode = MCPWM_UP_COUNTER
    };
    ESP_ERROR_CHECK(mcpwm_init(MCPWM_UNIT_0, MCPWM_TIMER_0, &cfg));
}

void apply_control_mcpwm(float u) {
    // 1) Direction: sign(u)>0 → forward; <0 → reverse; =0 → coast
    if      (u > 0) { gpio_set_level(INA_GPIO,1); gpio_set_level(INB_GPIO,0); }
    else if (u < 0) { gpio_set_level(INA_GPIO,0); gpio_set_level(INB_GPIO,1); }
    else            { gpio_set_level(INA_GPIO,0); gpio_set_level(INB_GPIO,0); }

    // 2) Magnitude: take absolute value of u (–100…+100 → 0…100)
    float mag = fabsf(u);
    if (mag > 100.0f) mag = 100.0f;  // clamp at 100%

    // 3) Update duty: 'mag' percent
    ESP_ERROR_CHECK(
      mcpwm_set_duty(
        MCPWM_UNIT_0,
        MCPWM_TIMER_0,
        MCPWM_OPR_A,
        mag               // 0.0–100.0
      )
    );
}

static float apply_lowpass_filter(void){ // this is for the PID to function
    float tau = 0.8;
    float raw = read_encoder();
    float alpha = dt/(tau+dt);
    filtered_pos = alpha*raw + (1-alpha)*filtered_pos;
    return filtered_pos;
}

static float read_velocity(void){
    int pos =  read_encoder();
    float v_raw = (pos - last_pos)/dt;
    last_pos = pos;
    float alpha_v = dt/(tau_vel + dt);
    last_vel_filt = alpha_v *v_raw + (1-alpha_v) * last_vel_filt;
    return last_vel_filt;
}

// Handle incoming serial commands from the GUI
void handle_serial_commands(void) {
    uint8_t data[BUF_SIZE];
    int len = uart_read_bytes(UART_NUM, data, BUF_SIZE, 10 / portTICK_PERIOD_MS);
    
    if (len > 0) {
        data[len] = '\0'; // Null terminate
        char* command = (char*)data;
        
        // Remove newline characters
        char* newline = strchr(command, '\n');
        if (newline) *newline = '\0';
        newline = strchr(command, '\r');
        if (newline) *newline = '\0';
        
        printf("Received command: %s\n", command);
        
        // Parse commands from GUI
        if (strncmp(command, "SET_KP_", 7) == 0) {
            kp = atof(command + 7);
            printf("Updated Kp: %.3f\n", kp);
        }
        else if (strncmp(command, "SET_KI_", 7) == 0) {
            ki = atof(command + 7);
            printf("Updated Ki: %.3f\n", ki);
        }
        else if (strncmp(command, "SET_KD_", 7) == 0) {
            kd = atof(command + 7);
            printf("Updated Kd: %.3f\n", kd);
        }
        else if (strncmp(command, "SET_TARGET_", 11) == 0) {
            targetPos = atoi(command + 11);
            printf("Updated Target: %d\n", targetPos);
        }
        else if (strncmp(command, "SET_DEADZONE_", 13) == 0) {
            deadzone = atoi(command + 13);
            printf("Updated Deadzone: %d\n", deadzone);
        }
        else if (strncmp(command, "SET_VISCOUS_", 12) == 0) {
            B = atof(command + 12);
            printf("Updated Viscous B: %.3f\n", B);
        }
        else if (strcmp(command, "STOP") == 0) {
            pid_enabled = false;
            apply_control_mcpwm(0);
            printf("Emergency stop activated\n");
        }
        else if (strcmp(command, "RESET") == 0) {
            system_reset_requested = true;
            printf("System reset requested\n");
        }
        else if (strcmp(command, "START") == 0) {
            pid_enabled = true;
            printf("PID control resumed\n");
        }
    }
}

static void viscous_task(void *arg){
    TickType_t next = xTaskGetTickCount();
    while (1){
        if (!pid_enabled) {
            vTaskDelayUntil(&next, pdMS_TO_TICKS(2));
            continue;
        }
        
        float vel = read_velocity();

        float u;
        if (fabsf(vel) < vel_dead) {
            u = 0.0f;
        } else {
            u = -B * vel;
        }

        // Only apply viscous control if PID is not active
        // apply_control_mcpwm(u);
        
        vTaskDelayUntil(&next, pdMS_TO_TICKS(2));
    }
}

void pid_step(void){
    if (!pid_enabled) {
        apply_control_mcpwm(0);
        return;
    }
    
    if (system_reset_requested) {
        // Reset all PID states
        integral = 0;
        lastError = 0;
        filtered_pos = 0;
        last_pos = 0;
        last_vel_filt = 0;
        system_reset_requested = false;
        printf("System reset completed\n");
        return;
    }
    
    int pos = read_encoder();
    // 2) Compute error
    int error = targetPos - pos;

    if (abs(error) < deadzone){ // sets the deadband of the motor 
        integral = 0;
        lastError = 0;
        apply_control_mcpwm(0.000f);
        // Still send data even when in deadzone
        printf("POS:%d,ERR:%d\n", pos, error);
        return;
    }

    // 3) PID math
    float P      = kp * error;
    integral    += error * dt;            // dt = 0.002f
    float I      = ki * integral;
    float deriv  = (error - lastError)/dt;
    float D      = kd * deriv;
    float u      = P + I + D;
    lastError    = error;

    // Clean data output for GUI parsing - only position and error
    printf("POS:%d,ERR:%d\n", pos, error);

    // 4) Drive the motor
    apply_control_mcpwm(u);
}

static void pid_task(void *arg){
    const TickType_t period = pdMS_TO_TICKS(2); // 2ms == 500Hz
    TickType_t next = xTaskGetTickCount();

    while(1){
        pid_step();
        vTaskDelayUntil(&next, period);
    }
}

// Task: sample encoder every 2 ms, print every 100 ms
static void encoder_task(void *arg) {
    const TickType_t sample_period = pdMS_TO_TICKS(2); // sampling freq for encoder counts
    TickType_t next_sample = xTaskGetTickCount();

    while (1) {
        int32_t pos = read_encoder();
        // Remove periodic printing to reduce serial spam
        vTaskDelayUntil(&next_sample, sample_period);
    }
}

// Serial command handling task
static void serial_task(void *arg) {
    while (1) {
        handle_serial_commands();
        vTaskDelay(pdMS_TO_TICKS(10)); // Check every 10ms
    }
}

// Initialize UART for serial communication on the ESP32-P4 board
void init_uart(void) {
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
#ifdef CONFIG_IDF_TARGET_ESP32P4
        .source_clk = UART_SCLK_XTAL,  // ESP32-P4 uses XTAL clock
#else
        .source_clk = UART_SCLK_APB,   // ESP32, ESP32-S2, ESP32-S3 use APB clock
#endif
    };
    
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM, BUF_SIZE * 2, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, 
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
}

void app_main(void) {
    printf("ESP32 Motor PID Controller Starting...\n");
    
    // Initialize hardware
    init_uart();
    init_encoder();
    init_mcpwm_highres();
    
    printf("Hardware initialized. Ready for commands.\n");
    printf("Current parameters: Kp=%.3f, Ki=%.3f, Kd=%.3f\n", kp, ki, kd);
    printf("Target=%d, Deadzone=%d, Viscous B=%.3f\n", targetPos, deadzone, B);

    // Create tasks
    xTaskCreate(pid_task, "pid", 4096, NULL, 5, NULL);
    xTaskCreate(encoder_task, "enc", 2048, NULL, 4, NULL);
    xTaskCreate(viscous_task, "visc", 4096, NULL, 3, NULL);
    xTaskCreate(serial_task, "serial", 4096, NULL, 6, NULL);
    
    printf("All tasks created. System ready.\n");
}