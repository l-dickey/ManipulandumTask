#include "encoder.h"
#include <stdio.h>
#include "driver/pcnt.h"
#include "driver/gpio.h"
#include "esp_err.h"

#define ENC_A_GPIO   32
#define ENC_B_GPIO   33
#define PCNT_UNIT    PCNT_UNIT_0

//–– module-local state ––  
static int32_t totalCount = 0;
static int16_t lastCnt    = 0;
static int16_t rawCnt     = 0;

void init_encoder(void) {
    // pull-ups so A/B never float  
    gpio_set_pull_mode(ENC_A_GPIO, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(ENC_B_GPIO, GPIO_PULLUP_ONLY);

    // channel 0: A pulses, B selects +/–  
    pcnt_config_t cfg = {
        .pulse_gpio_num = ENC_A_GPIO,
        .ctrl_gpio_num  = ENC_B_GPIO,
        .channel        = PCNT_CHANNEL_0,
        .unit           = PCNT_UNIT,
        .pos_mode       = PCNT_COUNT_INC,
        .neg_mode       = PCNT_COUNT_DEC,
        .lctrl_mode     = PCNT_MODE_KEEP,
        .hctrl_mode     = PCNT_MODE_REVERSE,
        .counter_h_lim  = INT16_MAX,
        .counter_l_lim  = INT16_MIN,
    };
    ESP_ERROR_CHECK( pcnt_unit_config(&cfg) );

    // channel 1: swap A/B for full quadrature  
    cfg.channel        = PCNT_CHANNEL_1;
    cfg.pulse_gpio_num = ENC_B_GPIO;
    cfg.ctrl_gpio_num  = ENC_A_GPIO;
    cfg.lctrl_mode     = PCNT_MODE_REVERSE;
    cfg.hctrl_mode     = PCNT_MODE_KEEP;
    ESP_ERROR_CHECK( pcnt_unit_config(&cfg) );

    // optional glitch filter  
    pcnt_set_filter_value(PCNT_UNIT, 100);
    pcnt_filter_enable(PCNT_UNIT);

    // clear & start  
    pcnt_counter_pause (PCNT_UNIT);
    pcnt_counter_clear (PCNT_UNIT);
    pcnt_counter_resume(PCNT_UNIT);

    // software init  
    lastCnt    = 0;
    totalCount = 0;
}

int32_t read_encoder(void) {
    pcnt_get_counter_value(PCNT_UNIT, &rawCnt);
    int16_t delta   = rawCnt - lastCnt;
    totalCount     += delta;
    lastCnt         = rawCnt;
    return totalCount;
}

// debugging task to read the encoder counts. Not necessary if you have the readout of the encoder counts
void encoder_task(void *arg) {
    const TickType_t sample_period = pdMS_TO_TICKS(2);
    const TickType_t print_period  = pdMS_TO_TICKS(100);
    TickType_t next_sample = xTaskGetTickCount();
    TickType_t next_print  = next_sample;

    while (1) {
        int32_t pos = read_encoder();
        TickType_t now = xTaskGetTickCount();

        if (now >= next_print) {
            printf("Encoder: %ld\n", pos);
            next_print += print_period;
        }
        vTaskDelayUntil(&next_sample, sample_period);
    }
}
