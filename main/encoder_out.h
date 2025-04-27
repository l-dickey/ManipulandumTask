#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Call once at startup
esp_err_t encoder_out_init(void);

// Call whenever you have an updated encoder count
esp_err_t encoder_out_update(int32_t encoder_val);

#ifdef __cplusplus
}
#endif
