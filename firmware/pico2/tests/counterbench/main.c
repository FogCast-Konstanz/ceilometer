#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/unique_id.h"
#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "hardware/watchdog.h"
#include "hardware/pwm.h"
#include "hardware/dma.h"
#include "sipm_counter.h"
#include "test_pulse.h"
static uint32_t bins[4096];
static void rate(uint32_t wanted) {
    test_pulse_set(wanted);sleep_ms(2);
    uint32_t start=time_us_32(),last=sipm_counter_read(),maxgap=0,prev=start;
    uint64_t total=0;
    while(time_us_32()-start<50000) {
        uint32_t n=sipm_counter_read(),t=time_us_32();
        total+=(n-last)&65535u;last=n;
        if(t-prev>maxgap) maxgap=t-prev;prev=t;
    }
    uint32_t elapsed=time_us_32()-start;test_pulse_set(0);
    double measured=total*1000000.0/elapsed,actual=test_pulse_actual_hz();
    /* test_pulse_set(0) clears reported frequency; recompute integer divider. */
    uint32_t sys=clock_get_hz(clk_sys);actual=sys/(2u*(sys/(2u*wanted)));
    printf("RATE sys=%lu wanted=%lu actual=%.0f counted=%llu us=%lu measured=%.1f ratio=%.6f maxgap_us=%lu\n",(unsigned long)sys,(unsigned long)wanted,actual,(unsigned long long)total,(unsigned long)elapsed,measured,measured/actual,(unsigned long)maxgap);
}
static void capture(uint32_t hz) {
    test_pulse_set(hz);sleep_ms(2);
    uint32_t pulse_hz=test_pulse_actual_hz();
    pwm_config p=pwm_get_default_config();pwm_config_set_wrap(&p,clock_get_hz(clk_sys)/10000000-1);
    pwm_init(0,&p,false);
    int ch=dma_claim_unused_channel(true);dma_channel_config c=dma_channel_get_default_config(ch);
    channel_config_set_transfer_data_size(&c,DMA_SIZE_32);channel_config_set_read_increment(&c,false);
    channel_config_set_write_increment(&c,true);channel_config_set_dreq(&c,pwm_get_dreq(0));
    dma_channel_configure(ch,&c,bins,&pwm_hw->slice[2].ctr,4096,false);
    dma_start_channel_mask(1u<<ch);pwm_set_enabled(0,true);
    uint32_t start=time_us_32();while(dma_channel_is_busy(ch) && time_us_32()-start<100000) tight_loop_contents();
    bool timeout=dma_channel_is_busy(ch);pwm_set_enabled(0,false);dma_channel_abort(ch);dma_channel_unclaim(ch);test_pulse_set(0);
    uint32_t total=0,max=0;for(int i=1;i<4096;i++){uint32_t d=(bins[i]-bins[i-1])&65535u;total+=d;if(d>max)max=d;}
    printf("DMA bins=4095 bin_hz=10000000 pulse_hz=%lu total=%lu expected_approx=%.3f max_bin=%lu timeout=%d\n",(unsigned long)pulse_hz,(unsigned long)total,4095.0*pulse_hz/10000000,(unsigned long)max,timeout);
}
int main(void) {
    stdio_init_all();sipm_counter_init();test_pulse_init();watchdog_enable(3000,true);
    while(1){watchdog_update();int c=getchar_timeout_us(1000);if(c!='b' && c!='o' && c!='s')continue;
        char id[PICO_UNIQUE_BOARD_ID_SIZE_BYTES*2+1];pico_get_unique_board_id_string(id,sizeof id);
        printf("BOARD %s sys=%lu vreg_selector=%u commands=b(baseline),o(500MHz1.5V),s(status)\n",id,(unsigned long)clock_get_hz(clk_sys),vreg_get_voltage());
        if(c=='s')continue;
        if(c=='o'){vreg_disable_voltage_limit();vreg_set_voltage(VREG_VOLTAGE_1_50);sleep_ms(20);if(!set_sys_clock_khz(500000,false)){printf("PLL_FAILED\n");continue;}}
        printf("FC0_sys_khz=%lu\n",(unsigned long)frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_SYS));
        printf("BEGIN sys=%lu vreg_selector=%u\n",(unsigned long)clock_get_hz(clk_sys),vreg_get_voltage());
        for(uint32_t f=1000000;f<=clock_get_hz(clk_sys)/2;f*=2){watchdog_update();rate(f);}
        rate(clock_get_hz(clk_sys)/2);capture(1000000);capture(10000000);capture(clock_get_hz(clk_sys)/2);
        set_sys_clock_khz(150000,true);vreg_set_voltage(VREG_VOLTAGE_1_10);
        printf("DONE restored sys=%lu vreg_selector=%u\n",(unsigned long)clock_get_hz(clk_sys),vreg_get_voltage());
    }
}
