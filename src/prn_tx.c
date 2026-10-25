#include "prn_tx.h"
#include "sequence.h"
#include "prn_tx.pio.h"
#include "hardware/dma.h"
#include "hardware/clocks.h"
#include "hardware/structs/pio.h"
static uint32_t words[TX_WORDS + 1u]; /* all-zero tail leaves output low on planned exhaustion */
static int tx_dma;
static uint tx_offset, frame_offset;
void prn_tx_init(const uint8_t code[PRN_LENGTH]) {
    gpio_init(PIN_TEST_PRN); gpio_put(PIN_TEST_PRN, 0); gpio_set_dir(PIN_TEST_PRN, GPIO_OUT);
    prn_pack(code, words);
    pio_sm_claim(pio0, 0); pio_sm_claim(pio0, 1);
    tx_offset = pio_add_program(pio0, &prn_tx_program);
    frame_offset = pio_add_program(pio0, &frame_sync_program);
    tx_dma = dma_claim_unused_channel(true);
}
void prn_tx_stop(void) {
    /* Force SIO low before stopping DMA: abort cannot leave the diode high. */
    gpio_put(PIN_LD_GATE, 0);
    gpio_set_function(PIN_LD_GATE, GPIO_FUNC_SIO);
    gpio_set_dir(PIN_LD_GATE, GPIO_OUT);
    gpio_put(PIN_TEST_PRN, 0); gpio_set_function(PIN_TEST_PRN, GPIO_FUNC_SIO);
    pio_set_sm_mask_enabled(pio0, 3, false);
    dma_channel_abort(tx_dma);
    pio_sm_set_pins_with_mask(pio0, 0, 0, 1u << PIN_CHIP_SYNC);
    pio_sm_set_pins_with_mask(pio0, 1, 0, 1u << PIN_FRAME_SYNC);
}
bool prn_tx_prepare(uint32_t hz, bool laser) {
    prn_tx_stop();
    uint32_t sys = clock_get_hz(clk_sys);
    if (!hz || sys % hz || sys / hz < CHIP_CYCLES) return false;
    /* Integer SM divider prevents fractional-divider chip jitter. */
    if ((sys / hz) % CHIP_CYCLES) return false;
    uint32_t div = sys / hz / CHIP_CYCLES;
    if (div > 65535) return false;
    pio_sm_config c = prn_tx_program_get_default_config(tx_offset);
    uint data_pin = laser ? PIN_LD_GATE : PIN_TEST_PRN;
    sm_config_set_out_pins(&c, data_pin, 1);
    sm_config_set_sideset_pins(&c, PIN_CHIP_SYNC);
    sm_config_set_out_shift(&c, true, true, 32);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
    sm_config_set_clkdiv_int_frac(&c, (uint16_t)div, 0);
    pio_sm_init(pio0, 0, tx_offset, &c);
    pio_gpio_init(pio0, PIN_CHIP_SYNC);
    pio_gpio_init(pio0, data_pin);
    pio_sm_set_consecutive_pindirs(pio0, 0, data_pin, 1, true);
    pio_sm_set_consecutive_pindirs(pio0, 0, PIN_CHIP_SYNC, 1, true);
    c = frame_sync_program_get_default_config(frame_offset);
    sm_config_set_sideset_pins(&c, PIN_FRAME_SYNC);
    sm_config_set_clkdiv_int_frac(&c, (uint16_t)div, 0);
    pio_sm_init(pio0, 1, frame_offset, &c);
    pio_gpio_init(pio0, PIN_FRAME_SYNC);
    pio_sm_set_consecutive_pindirs(pio0, 1, PIN_FRAME_SYNC, 1, true);
    pio_sm_put_blocking(pio0, 1, PRN_LENGTH * CHIP_CYCLES - 2u);
    pio_sm_exec(pio0, 1, pio_encode_pull(false, true));
    pio_sm_exec(pio0, 1, pio_encode_mov(pio_y, pio_osr));
    dma_channel_config dc = dma_channel_get_default_config(tx_dma);
    channel_config_set_transfer_data_size(&dc, DMA_SIZE_32);
    channel_config_set_read_increment(&dc, true);
    channel_config_set_write_increment(&dc, false);
    channel_config_set_dreq(&dc, pio_get_dreq(pio0, 0, true));
    dma_channel_configure(tx_dma, &dc, &pio0->txf[0], words, TX_WORDS + 1u, true);
    /* Fill FIFO before enabling; exclude expected initial empty from diagnostics. */
    while (pio_sm_is_tx_fifo_empty(pio0, 0)) tight_loop_contents();
    pio0->fdebug = 1u << PIO_FDEBUG_TXSTALL_LSB;
    return true;
}
void prn_tx_start(void) { pio_enable_sm_mask_in_sync(pio0, 3); }
bool prn_tx_starved(void) { return (pio0->fdebug & (1u << PIO_FDEBUG_TXSTALL_LSB)) != 0; }
