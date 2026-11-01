#include "prn_tx.h"
#include "sequence.h"
#include "integration.h"
#include "hardware/pwm.h"
#include "prn_tx.pio.h"
#include "hardware/dma.h"
#include "hardware/clocks.h"
#include "hardware/structs/pio.h"
static uint32_t words[PRN_MAX_LENGTH+1u];
static uint32_t tx_words=PRN_LENGTH;
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
    dma_channel_set_config(tx_dma,&(dma_channel_config){.ctrl=0},false);
    dma_channel_abort(tx_dma);
    gpio_put(PIN_CHIP_SYNC,0);gpio_set_function(PIN_CHIP_SYNC,GPIO_FUNC_SIO);gpio_set_dir(PIN_CHIP_SYNC,GPIO_OUT);
    pio_sm_set_pins_with_mask(pio0, 1, 0, 1u << PIN_FRAME_SYNC);
}
bool prn_tx_prepare(uint32_t hz, bool laser) {
    prn_tx_stop();
    uint32_t sys = clock_get_hz(clk_sys);
    if (!hz || sys % hz || sys / hz < CHIP_CYCLES) return false;
    /* Integer SM divider prevents fractional-divider chip jitter. */
    if ((sys / hz) % CHIP_CYCLES) return false;
    /* Run WAIT and instruction issue at clk_sys, avoiding divided-clock
       start-phase ambiguity. OUT + MOV + (Y+1) JMP cycles = sys/hz. */
    const uint32_t div=1;
    pio_sm_config c = prn_tx_program_get_default_config(tx_offset);
    uint data_pin = laser ? PIN_LD_GATE : PIN_TEST_PRN;
    sm_config_set_out_pins(&c, data_pin, 1);

    sm_config_set_out_shift(&c, true, false, 32);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
    sm_config_set_clkdiv_int_frac(&c, (uint16_t)div, 0);
    pio_sm_init(pio0, 0, tx_offset, &c);
    pio_sm_put_blocking(pio0,0,sys/hz-3u);
    pio_sm_exec(pio0,0,pio_encode_pull(false,true));
    pio_sm_exec(pio0,0,pio_encode_mov(pio_y,pio_osr));
    /* Autopull is still off: discard timing setup word and mark OSR empty. */
    pio_sm_exec(pio0,0,pio_encode_out(pio_null,32));
    sm_config_set_out_shift(&c,true,true,32);pio_sm_set_config(pio0,0,&c);
    gpio_set_input_enabled(PIN_CHIP_SYNC,true);
    /* Clear the retained PIO output before remuxing after every restart. */
    pio_sm_set_pins_with_mask(pio0,0,0,1u<<data_pin);
    pio_gpio_init(pio0, data_pin);
    pio_sm_set_consecutive_pindirs(pio0, 0, data_pin, 1, true);

    c = frame_sync_program_get_default_config(frame_offset);
    sm_config_set_sideset_pins(&c, PIN_FRAME_SYNC);
    sm_config_set_clkdiv_int_frac(&c, (uint16_t)div, 0);
    pio_sm_init(pio0, 1, frame_offset, &c);
    pio_gpio_init(pio0, PIN_FRAME_SYNC);
    pio_sm_set_consecutive_pindirs(pio0, 1, PIN_FRAME_SYNC, 1, true);
    pio_sm_put_blocking(pio0, 1, tx_words * (sys/hz) - 6u);
    pio_sm_exec(pio0, 1, pio_encode_pull(false, true));
    pio_sm_exec(pio0, 1, pio_encode_mov(pio_y, pio_osr));
    dma_channel_config dc = dma_channel_get_default_config(tx_dma);
    channel_config_set_transfer_data_size(&dc, DMA_SIZE_32);
    channel_config_set_read_increment(&dc, true);
    channel_config_set_write_increment(&dc, false);
    channel_config_set_dreq(&dc, pio_get_dreq(pio0, 0, true));
    channel_config_set_high_priority(&dc,true);
    words[tx_words]=0; /* finite source: 32 complete periods plus a low tail */
    dma_channel_configure(tx_dma,&dc,&pio0->txf[0],words,tx_words+1u,false);
    dma_start_channel_mask(1u<<tx_dma);
    /* Fill FIFO before enabling; exclude expected initial empty from diagnostics. */
    while (pio_sm_is_tx_fifo_empty(pio0, 0)) tight_loop_contents();
    pio0->fdebug = 1u << PIO_FDEBUG_TXSTALL_LSB;
    return true;
}
void prn_tx_start(void) { pio_enable_sm_mask_in_sync(pio0, 3); }
bool prn_tx_starved(void) { return (pio0->fdebug & (1u << PIO_FDEBUG_TXSTALL_LSB)) != 0; }

void prn_tx_set_sequence(const uint8_t *code,unsigned length) {
    tx_words=length;prn_pack_length(code,length,words);
}
void prn_tx_set_code(const uint8_t code[PRN_LENGTH]) {prn_tx_set_sequence(code,PRN_LENGTH);}
