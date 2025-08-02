#include "encoder_out.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include <math.h>
#include <stdint.h>
#include "encoder.h"

#define I2C_MASTER_NUM              I2C_NUM_0
#define I2C_MASTER_SDA_IO           32
#define I2C_MASTER_SCL_IO           36
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
    esp_err_t ret;

    ret = i2c_param_config(I2C_MASTER_NUM, &conf);
    if (ret != ESP_OK) {
        printf("I2C param config error: %d\n", ret);
        return ret;
    }

    ret = i2c_driver_install(I2C_MASTER_NUM, conf.mode,
                             I2C_MASTER_RX_BUF_DISABLE,
                             I2C_MASTER_TX_BUF_DISABLE, 0);
    // if (ret != ESP_OK) {
    //     printf("I2C driver install error: %d\n", ret);
    //     return ret;
    // }
    // printf("SDA: %d, SCL: %d, FREQ: %d\n", I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO, I2C_MASTER_FREQ_HZ);

    // printf("I2C initialized for MCP4725\n");
    return ESP_OK;
}


static uint16_t scale_encoder_to_dac(int32_t encoder_val) { //  scale the voltage so we can read it on an analog in port on intan
    encoder_val = fminf(fmaxf(encoder_val, -ENCODER_MAX_RANGE),
                       +ENCODER_MAX_RANGE);
    uint32_t shifted = (uint32_t)(encoder_val + ENCODER_MAX_RANGE);
    return (uint16_t)((shifted * 4095) / (2 * ENCODER_MAX_RANGE));
}

// rename to take *no* argument, and read from the encoder module directly
esp_err_t encoder_out_update(int32_t encoder_val) {
    
    uint16_t dac = scale_encoder_to_dac(encoder_val);

    uint8_t packet[3] = {
        MCP4725_CMD_WRITEDAC,
        (uint8_t)(dac >> 4),                     // D11–D4
        (uint8_t)((dac & 0x0F) << 4)             // D3–D0
    };
    // printf("i2c_master_write_to_device: port=%d, addr=0x%02X, buf_size=%d\n",
    //    I2C_MASTER_NUM, MCP4725_ADDR, sizeof(packet));

    esp_err_t ret = i2c_master_write_to_device(
        I2C_MASTER_NUM,
        MCP4725_ADDR,
        packet, sizeof(packet),
        pdMS_TO_TICKS(10) // time out after 10ms
    );
    if (ret != ESP_OK) {
        printf("I2C error: %d\n", ret);
    }
    return ret;
}
