#include "test_pulse.h"
#include "test_pulse.pio.h"
#include "config.h"
#include "hardware/clocks.h"
static uint offset;
static uint32_t actual;
void test_pulse_init(void) {
    pio_sm_claim(pio1, 0); offset = pio_add_program(pio1, &test_pulse_program);
    gpio_init(PIN_TEST_PULSE); gpio_set_dir(PIN_TEST_PULSE, GPIO_OUT);
}
bool test_pulse_set(uint32_t hz) {
    pio_sm_set_enabled(pio1, 0, false);
    gpio_put(PIN_TEST_PULSE, 0); gpio_set_function(PIN_TEST_PULSE, GPIO_FUNC_SIO);
    actual = 0;
    if (!hz) return true;
    uint32_t sys = clock_get_hz(clk_sys);
    if (hz > sys / 2 || hz < 2000) return false;
    /* Quantize to an integer divider so pulse high/low times are constant. */
    uint32_t div = sys / (2u * hz);
    pio_sm_config c = test_pulse_program_get_default_config(offset);
    sm_config_set_set_pins(&c, PIN_TEST_PULSE, 1);
    sm_config_set_clkdiv_int_frac(&c, (uint16_t)div, 0);
    pio_sm_init(pio1, 0, offset, &c);
    pio_sm_set_consecutive_pindirs(pio1, 0, PIN_TEST_PULSE, 1, true);
    pio_gpio_init(pio1, PIN_TEST_PULSE);
    pio_sm_set_enabled(pio1, 0, true);
    actual = sys / (2u * div); return true;
}
uint32_t test_pulse_actual_hz(void) { return actual; }
