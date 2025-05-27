#include "encoder_out.h"
#include "driver/i2c.h"
#include "esp_log.h"

#define I2C_MASTER_NUM              I2C_NUM_0
#define I2C_MASTER_SDA_IO           21
#define I2C_MASTER_SCL_IO           22
#define I2C_MASTER_FREQ_HZ          100000
#define I2C_MASTER_TX_BUF_DISABLE   0
#define I2C_MASTER_RX_BUF_DISABLE   0

#define MCP4725_ADDR                0x62
#define MCP4725_CMD_WRITEDAC        0x40

#define ENCODER_MAX_RANGE           200

static const char *TAG = "ENCODER_OUT";

esp_err_t encoder_out_init(void) {
    i2c_config_t conf = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = I2C_MASTER_SDA_IO,
        .scl_io_num       = I2C_MASTER_SCL_IO,
        .sda_pullup_en    = GPIO_PULLUP_ENABLE,
        .scl_pullup_en    = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_MASTER_NUM, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_MASTER_NUM, conf.mode,
                                       I2C_MASTER_RX_BUF_DISABLE,
                                       I2C_MASTER_TX_BUF_DISABLE, 0));
    ESP_LOGI(TAG, "I2C initialized for MCP4725");
    return ESP_OK;
}

static uint16_t scale_encoder_to_dac(int32_t encoder_val) {
    if (encoder_val > ENCODER_MAX_RANGE) encoder_val = ENCODER_MAX_RANGE;
    if (encoder_val < -ENCODER_MAX_RANGE) encoder_val = -ENCODER_MAX_RANGE;
    uint32_t shifted = encoder_val + ENCODER_MAX_RANGE;  
    return (shifted * 4095) / (2 * ENCODER_MAX_RANGE);
}

esp_err_t encoder_out_update(int32_t encoder_val) {
    uint16_t dac_value = scale_encoder_to_dac(encoder_val);

    uint8_t packet[3];
    packet[0] = MCP4725_CMD_WRITEDAC;
    packet[1] = dac_value >> 4;            // Upper 8 bits (D11-D4)
    packet[2] = (dac_value & 0x0F) << 4;   // Lower 4 bits shifted left

    return i2c_master_write_to_device(
        I2C_MASTER_NUM,
        MCP4725_ADDR,
        packet, sizeof(packet),
        pdMS_TO_TICKS(100)
    );
}
