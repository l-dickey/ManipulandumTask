#ifndef ENCODER_H
#define ENCODER_H

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Call once at startup to wire up PCNT for your A/B pins.
void init_encoder(void);

// Returns the 32-bit accumulated count (can be +/–).
int32_t read_encoder(void);

// Optional: a FreeRTOS task that samples and (optionally) prints.
// If you want the sampling & printing inside the module, expose this.
void encoder_task(void *arg);

#endif // ENCODER_H
