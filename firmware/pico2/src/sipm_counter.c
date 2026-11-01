#include "sipm_counter.h"
#include "config.h"
#include "hardware/pwm.h"
#include "hardware/gpio.h"
void sipm_counter_init(void) {
    hard_assert(pwm_gpio_to_slice_num(PIN_SIPM) == PWM_COUNTER_SLICE);
    hard_assert(pwm_gpio_to_channel(PIN_SIPM) == PWM_CHAN_B);
    gpio_set_function(PIN_SIPM, GPIO_FUNC_PWM);
    gpio_disable_pulls(PIN_SIPM);
    pwm_config c = pwm_get_default_config();
    pwm_config_set_clkdiv_mode(&c, PWM_DIV_B_RISING);
    pwm_config_set_clkdiv_int(&c, 1);
    pwm_config_set_wrap(&c, 65535);
    pwm_init(PWM_COUNTER_SLICE, &c, true);
}
uint16_t sipm_counter_read(void) { return pwm_get_counter(PWM_COUNTER_SLICE); }
