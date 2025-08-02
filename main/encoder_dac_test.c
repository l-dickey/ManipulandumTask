#include <stdio.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "encoder.h"
#include "encoder_out.h"

// Simple task: Read encoder, write to DAC
void encoder_to_dac_task(void *pv) {
    while (1) {
        int32_t encoder_val = read_encoder();
        printf("Encoder: %ld\n", encoder_val);
        encoder_out_update(200);
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void app_main(void)
{
    // Initialize encoder hardware
    init_encoder();

    // Initialize MCP4725/I2C
    encoder_out_init();
    

    // Start test task
    xTaskCreate(encoder_to_dac_task, "encoder_to_dac", 2048, NULL, 5, NULL);
}
