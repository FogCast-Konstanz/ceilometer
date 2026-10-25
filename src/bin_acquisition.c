#include "bin_acquisition.h"
#include "config.h"
#include "prn_tx.h"
#include "hardware/dma.h"
#include "hardware/pwm.h"
#include "hardware/irq.h"
#include "hardware/clocks.h"
#include "hardware/sync.h"
#include "hardware/structs/dma_debug.h"
#include "pico/time.h"
static uint32_t samples[CAPTURE_MAX_SAMPLES];
static int rx_dma;
static volatile acquisition_status state;
static uint32_t started_us, deadline_us;
static void dma_done(void) {
    if (!(dma_hw->ints0 & (1u << rx_dma))) return;
    dma_hw->ints0 = 1u << rx_dma;
    pwm_set_enabled(PWM_TIMER_SLICE, false);
    state.tx_starved = prn_tx_starved();
    prn_tx_stop();
    state.elapsed_us = time_us_32() - started_us;
    uint32_t ctrl = dma_hw->ch[rx_dma].ctrl_trig;
    state.failed = state.tx_starved || (ctrl & DMA_CH0_CTRL_TRIG_AHB_ERROR_BITS);
    state.busy = false;
    __dmb();
    state.ready = !state.failed;
}
void bin_acquisition_init(void) {
    rx_dma = dma_claim_unused_channel(true);
    irq_set_exclusive_handler(DMA_IRQ_0, dma_done);
    dma_channel_set_irq0_enabled(rx_dma, true);
    irq_set_enabled(DMA_IRQ_0, true);
}
void bin_acquisition_stop(void) {
    uint32_t irq = save_and_disable_interrupts();
    pwm_set_enabled(PWM_TIMER_SLICE, false);
    prn_tx_stop();
    dma_channel_set_irq0_enabled(rx_dma, false);
    dma_channel_abort(rx_dma);
    dma_hw->ints0 = 1u << rx_dma;
    state.busy = false;
    state.ready = false;
    dma_channel_set_irq0_enabled(rx_dma, true);
    restore_interrupts(irq);
}
bool bin_acquisition_start(unsigned frames, uint32_t hz, bool laser) {
    uint32_t sys = clock_get_hz(clk_sys);
    if (state.busy || frames < 1 || frames > CAPTURE_MAX_FRAMES || !hz ||
        sys % hz || sys / hz < CHIP_CYCLES || sys / hz > 65536) return false;
    if (!prn_tx_prepare(hz, laser)) return false;
    state = (acquisition_status){.samples = PRN_LENGTH * frames + 1u};
    pwm_config pc = pwm_get_default_config();
    pwm_config_set_clkdiv_int(&pc, 1);
    pwm_config_set_wrap(&pc, (uint16_t)(sys / hz - 1u));
    pwm_init(PWM_TIMER_SLICE, &pc, false);
    dma_channel_config dc = dma_channel_get_default_config(rx_dma);
    /* Full-width peripheral reads: no assumption about narrow PWM bus access. */
    channel_config_set_transfer_data_size(&dc, DMA_SIZE_32);
    channel_config_set_read_increment(&dc, false);
    channel_config_set_write_increment(&dc, true);
    channel_config_set_dreq(&dc, pwm_get_dreq(PWM_TIMER_SLICE));
    channel_config_set_high_priority(&dc, true);
    dma_hw->ints0 = 1u << rx_dma;
    dma_channel_configure(rx_dma, &dc, samples,
                          &pwm_hw->slice[PWM_COUNTER_SLICE].ctr, state.samples, false);
    /* Discard accumulated pacing credit from the preceding finite transfer. */
    dma_debug_hw->ch[rx_dma].dbg_ctdreq = 0;
    uint32_t irq = save_and_disable_interrupts();
    state.busy = true;
    started_us = time_us_32();
    deadline_us = (uint32_t)(((uint64_t)state.samples * 1000000u + hz - 1u) / hz) + 10000;
    dma_start_channel_mask(1u << rx_dma);
    /* These writes are ordered, NOT a shared hardware start strobe.
       Fixed phase and run-to-run spread MUST be calibrated externally. */
    prn_tx_start();
    pwm_set_enabled(PWM_TIMER_SLICE, true);
    restore_interrupts(irq);
    return true;
}
void bin_acquisition_poll(void) {
    if (state.busy && time_us_32() - started_us > deadline_us) {
        bin_acquisition_stop(); state.failed = true;
    }
}
acquisition_status bin_acquisition_status(void) {
    uint32_t irq = save_and_disable_interrupts();
    acquisition_status result = state;
    restore_interrupts(irq);
    return result;
}
const uint32_t *bin_acquisition_data(void) { return samples; }
