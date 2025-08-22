// main/event.c

#include "event.h"
#include "driver/rmt_tx.h"    // new TX-only API
#include "esp_check.h"        // for ESP_RETURN_ON_ERROR
#include "esp_err.h"

static const char *TAG = "EVENT";

static rmt_channel_handle_t   s_tx_chan   = NULL;
static rmt_encoder_handle_t   s_copy_enc  = NULL;
static gpio_num_t             s_pin       = GPIO_NUM_NC;

// Pulse widths (µs) for each event_state_t
static const uint32_t EVENT_WIDTH_US[EVENT_STATE_COUNT] = {
    [INIT]     =  10000,
    [CUE_0]    = 30000,
    [CUE_1]    = 40000,
    [CUE_2]    = 50000,
    [CUE_3]    = 60000,
    [MOVING]   = 16000,
    [REWARD_0] = 70000,
    [REWARD_1] = 80000,
    [REWARD_2] = 90000,
    [REWARD_3] = 100000,
    [TIMEOUT]  = 160000,
    [RESET]    = 12000
};

esp_err_t event_init_rmt(gpio_num_t pin, uint32_t resolution_hz)
{
    s_pin = pin;

    // 1) Create TX channel
    rmt_tx_channel_config_t tx_cfg = {
        .clk_src           = RMT_CLK_SRC_DEFAULT,
        .gpio_num          = pin,
        .mem_block_symbols = 64,            // at least 1 symbol; 64 is fine
        .resolution_hz     = resolution_hz, // ticks per second (e.g. 1 000 000 for 1 µs)
        .trans_queue_depth = 4,
        .flags              = { .invert_out = false, .with_dma = false }
    };
    ESP_RETURN_ON_ERROR(rmt_new_tx_channel(&tx_cfg, &s_tx_chan), TAG, "rmt_new_tx_channel failed");
    ESP_RETURN_ON_ERROR(rmt_enable(s_tx_chan),                TAG, "rmt_enable failed");

    // 2) Create a copy-encoder so we can send raw rmt_symbol_word_t directly :contentReference[oaicite:0]{index=0}
    rmt_copy_encoder_config_t copy_cfg = { 0 };
    ESP_RETURN_ON_ERROR(rmt_new_copy_encoder(&copy_cfg, &s_copy_enc),
                        TAG, "rmt_new_copy_encoder failed");

    return ESP_OK;
}

esp_err_t event_send_state(event_state_t st)
{
    if (st >= EVENT_STATE_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_tx_chan == NULL || s_copy_enc == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    // Build one RMT symbol
    rmt_symbol_word_t pulse = {
        .level0    = 1,
        .duration0 = EVENT_WIDTH_US[st],
        .level1    = 0,
        .duration1 = 1
    };

    // One-shot send
    rmt_transmit_config_t cfg = {
        .loop_count = 0,
        .flags      = { .eot_level = 0 }
    };

    return rmt_transmit(
        s_tx_chan,
        s_copy_enc,       // use the copy encoder, not NULL
        &pulse,
        sizeof(pulse),
        &cfg
    );
}


