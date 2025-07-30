#include "reward.h"
#include "driver/gpio.h"
#include "freertos/freertos.h"

static int        s_gpio;
static int        s_phase, s_total;
static TickType_t s_last;
static const TickType_t S_DUR = pdMS_TO_TICKS(500);

void reward_init(int gpio_num) {
    s_gpio = gpio_num;
    gpio_set_direction(s_gpio, GPIO_MODE_OUTPUT);
    gpio_set_level    (s_gpio, 0);
}

void reward_start(int pulses) {
    s_total = pulses * 2;      // on/off counts
    s_phase = 0;               // start in “on” phase
    s_last  = xTaskGetTickCount();
    gpio_set_level(s_gpio, 1);
}

void reward_update(TickType_t now) {
    if (s_phase < s_total && now - s_last >= S_DUR) {
        s_last = now;
        s_phase++;
        gpio_set_level(s_gpio, s_phase & 1);
    }
}

bool reward_active(void) {
    return (s_phase < s_total);
}
