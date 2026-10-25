#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "hardware/clocks.h"
#include "hardware/watchdog.h"
#include "board.h"
#include "sequence.h"
#include "prn_tx.h"
#include "sipm_counter.h"
#include "bin_acquisition.h"
#include "correlation.h"
#include "protocol.h"
#include "lt8722.h"
#include "sensors.h"
#include "test_pulse.h"
#include "temperature_control.h"
static uint8_t code[PRN_LENGTH];
static uint64_t histogram[PRN_LENGTH];
static int64_t profile[PRN_LENGTH];
static uint32_t chip_hz = DEFAULT_CHIP_HZ, remaining, accumulated, capture_frames;
static uint32_t lease_us, correlation_us;
static bool armed, fault, running, emitting, ready, consumed, tec_on, bias_on;
static uint16_t laser_code;
static temperature_controller thermal;
static uint32_t last_tec_health;
static volatile bool driver_fault;
static void error(const char *cmd, const char *reason) { printf("$NACK,%s,%s\r\n", cmd, reason); }
static void ack(const char *cmd) { printf("$ACK,%s\r\n", cmd); }
static void emergency_stop(bool latch) {
    bin_acquisition_stop(); board_outputs_off(); lt8722_disable(); temperature_stop(&thermal);
    armed = false; running = false; emitting = false; ready = false; tec_on = false; bias_on = false;
    laser_code = 0; remaining = 0;
    if (latch) fault = true;
}
static void fault_irq(uint gpio, uint32_t events) {
    (void)gpio; (void)events;
    /* Immediate pin mux override even while the foreground is blocked on USB. */
    gpio_put(PIN_LD_GATE, 0); gpio_set_function(PIN_LD_GATE, GPIO_FUNC_SIO);
    driver_fault = true;
}
static bool laser_temperature_ok(void) {
    return temperature_laser_allowed(sensors_laser_sample(), time_us_32());
}
static bool thermal_service(bool poll_sensor) {
    if (poll_sensor) sensors_poll();
    temperature_sample sample = sensors_laser_sample();
    uint32_t now = time_us_32();
    if ((armed || tec_on) && !temperature_tec_allowed(sample, now)) {
        emergency_stop(true); thermal.fault = true; return false;
    }
    if (armed && !temperature_laser_allowed(sample, now)) {
        /* Keep valid-feedback TEC regulation running to recover temperature.
           Laser remains disarmed until an explicit arm command succeeds. */
        bin_acquisition_stop(); (void)board_dac(0, 0);
        armed=false; emitting=false; running=false; ready=false;
        laser_code=0; remaining=0;
    }
    if (tec_on && poll_sensor) {
        uint32_t previous=thermal.last_sequence;
        if (!temperature_update(&thermal, sample, now) ||
            (previous != thermal.last_sequence && !lt8722_set_mv(thermal.output_mv))) {
            emergency_stop(true); thermal.fault=true; return false;
        }
        if (now-last_tec_health >= 100000) {
            last_tec_health=now;
            if (!lt8722_healthy()) { emergency_stop(true); thermal.fault=true; return false; }
        }
    }
    return true;
}
static bool page(char *arg, uint32_t max, uint32_t *offset, uint32_t *count) {
    if (!arg) return false;
    char *comma = strchr(arg, ','); if (!comma) return false;
    *comma++ = 0;
    return protocol_uint(arg, max - 1, offset) && protocol_uint(comma, 16, count) &&
           *count && *offset + *count <= max;
}
static bool begin_capture(void) {
    /* Service temperature only in the dead time between finite bursts. */
    if (!thermal_service(true) || !running) return false;
    if (emitting && (!armed || !laser_temperature_ok())) return false;
    capture_frames = remaining > CAPTURE_MAX_FRAMES ? CAPTURE_MAX_FRAMES : remaining;
    /* Limit sensor-service delay at slow chip rates (one frame minimum). */
    if (armed || tec_on) {
        unsigned cap = chip_hz / (20u * PRN_LENGTH);
        if (!cap) cap = 1;
        if (capture_frames > cap) capture_frames = cap;
    }
    consumed = false;
    return bin_acquisition_start(capture_frames, chip_hz, emitting);
}
static void command(char *cmd, char *arg, void *context) {
    (void)context;
    uint32_t n = 0, count = 0;
    bool numeric = protocol_uint(arg, UINT32_MAX, &n);
    lease_us = time_us_32();
    if (!strcmp(cmd, "stop") && !arg) { emergency_stop(false); ack(cmd); return; }
    if (!strcmp(cmd, "get_info") && !arg) {
        printf("$ACK,get_info,ceilometer,0.2.0,pico2,%lu,%lu,%u,%d,%d,%d\r\n",
            (unsigned long)clock_get_hz(clk_sys), (unsigned long)chip_hz, PRN_LENGTH,
            CEIL_ENABLE_LASER, CEIL_ENABLE_BIAS, CEIL_ENABLE_TEC); return;
    }
    if (!strcmp(cmd, "get_state") && !arg) {
        acquisition_status s = bin_acquisition_status();
        printf("$ACK,get_state,%s,%lu,%lu,%lu,%d,%d,%d,%lu\r\n",
            fault ? "FAULT" : running ? "ACQUIRING" : ready ? "READY" : "IDLE",
            (unsigned long)accumulated, (unsigned long)remaining, (unsigned long)s.elapsed_us,
            s.tx_starved, armed, bias_on, (unsigned long)correlation_us); return;
    }
    if (!strcmp(cmd, "heartbeat") && !arg) { ack(cmd); return; }
    if (!strcmp(cmd, "get_count") && !arg) {
        printf("$ACK,get_count,%u\r\n", sipm_counter_read()); return;
    }
    if (!strcmp(cmd, "get_temperature_control") && !arg) {
        temperature_sample s=sensors_laser_sample();
        printf("$ACK,%s,%d,%ld,%d,%ld,%d,%d,%d\r\n", cmd, tec_on,
            (long)thermal.target_mc, temperature_fresh(s,time_us_32()), (long)s.mc,
            thermal.output_mv, thermal.polarity, thermal.fault); return;
    }
    if (running) { error(cmd, "BUSY"); return; }
    if (!strcmp(cmd, "clear_fault") && !arg) {
        if (board_laser_fault()) error(cmd, "LD_FAULT_ACTIVE");
        else if (thermal.fault && !temperature_tec_allowed(sensors_laser_sample(),time_us_32())) error(cmd,"TEMPERATURE_INVALID");
        else { driver_fault = false; fault = false; thermal.fault=false; ack(cmd); } return;
    }
    if (!strcmp(cmd, "sensors") && !arg) {
        int32_t ld = 0, rx = 0;
        bool ld_valid = sensors_laser_mc(&ld), rx_valid = sensors_receiver_mc(&rx);
        printf("$ACK,sensors,%u,%u,%u,%d,%ld,%d,%ld\r\n", board_adc(0), board_adc(1), board_adc(2),
            ld_valid, (long)ld, rx_valid, (long)rx); return;
    }
    if (!strcmp(cmd, "tec_read") && numeric && n < 8) {
        uint32_t v;
        if (lt8722_read((uint8_t)n, &v)) printf("$ACK,tec_read,%lu,%lu\r\n", (unsigned long)n, (unsigned long)v);
        else error(cmd, "SPI_CRC_ACK");
        return;
    }
    if (!strcmp(cmd, "raw") || !strcmp(cmd, "histogram") || !strcmp(cmd, "profile")) {
        bool raw = !strcmp(cmd, "raw");
        uint32_t max = raw ? bin_acquisition_status().samples : PRN_LENGTH;
        if (!ready) { error(cmd, "NO_DATA"); return; }
        if (!page(arg, max, &n, &count)) { error(cmd, "INVALID_PARAM"); return; }
        printf("$ACK,%s,%lu", cmd, (unsigned long)n);
        for (unsigned i = n; i < n + count; ++i) {
            if (raw) printf(",%lu", (unsigned long)(bin_acquisition_data()[i] & 65535));
            else if (!strcmp(cmd, "histogram")) printf(",%" PRIu64, histogram[i]);
            else printf(",%" PRId64, profile[i]);
        }
        printf("\r\n"); return;
    }
    if (!strcmp(cmd, "set_rate") && numeric) {
        uint32_t sys = clock_get_hz(clk_sys);
        if (n < 10000 || n > DEFAULT_CHIP_HZ || sys % n || (sys / n) % CHIP_CYCLES) {
            error(cmd, "INVALID_RATE"); return;
        }
        chip_hz = n; ready = false; ack(cmd); return;
    }
    if (!strcmp(cmd, "test_pulse") && numeric) {
        if (!test_pulse_set(n)) error(cmd, "INVALID_RATE");
        else printf("$ACK,test_pulse,%lu\r\n", (unsigned long)test_pulse_actual_hz());
        return;
    }
    if (!strcmp(cmd, "set_threshold") && numeric && n <= 65535) {
        if (board_dac(2, (uint16_t)n)) ack(cmd); else error(cmd, "I2C"); return;
    }
    if (!strcmp(cmd, "arm") && numeric && n <= 1) {
        if (!n) { emergency_stop(false); ack(cmd); return; }
        if (!CEIL_ENABLE_LASER || !CEIL_LASER_MAX_CODE) error(cmd, "LASER_BUILD_DISABLED");
        else if (fault || board_laser_fault()) error(cmd, "FAULT");
        else if (!laser_temperature_ok()) error(cmd, "LASER_TEMPERATURE_10_TO_30_C");
        else { armed = true; ack(cmd); } return;
    }
    if (!strcmp(cmd, "laser_dac") && numeric && n <= 65535) {
        if (n && (!armed || fault || !laser_temperature_ok() || n > CEIL_LASER_MAX_CODE)) { error(cmd, "NOT_ARMED_OR_LIMIT"); return; }
        if (board_dac(0, (uint16_t)n)) { laser_code = (uint16_t)n; ack(cmd); }
        else { emergency_stop(true); error(cmd, "I2C"); } return;
    }
    if (!strcmp(cmd, "bias_dac") && numeric && n <= 65535) {
        if (n && (!CEIL_ENABLE_BIAS || fault || n > CEIL_BIAS_MAX_CODE)) {
            error(cmd, "BIAS_BUILD_DISABLED_OR_LIMIT"); return;
        }
        if (board_dac(1, (uint16_t)n)) ack(cmd);
        else { emergency_stop(true); error(cmd, "I2C"); } return;
    }
    if (!strcmp(cmd, "bias_enable") && numeric && n <= 1) {
        if (n && (!CEIL_ENABLE_BIAS || !CEIL_BIAS_MAX_CODE || fault)) error(cmd, "BIAS_BUILD_DISABLED_OR_FAULT");
        else { bias_on = n; board_bias_enable(bias_on); ack(cmd); } return;
    }
    if (!strcmp(cmd, "tec_target") && numeric && n >= 10000 && n <= 30000) {
        thermal.target_mc=(int32_t)n; pid_reset(&thermal.pid); ack(cmd); return;
    }
    if (!strcmp(cmd, "tec_polarity") && arg && (!strcmp(arg,"1") || !strcmp(arg,"-1"))) {
        if (tec_on || armed) error(cmd,"OUTPUTS_ACTIVE");
        else { thermal.polarity=!strcmp(arg,"1") ? 1 : -1; ack(cmd); } return;
    }
    if (!strcmp(cmd, "tec_pid") && arg) {
        char *i=strchr(arg,','), *d=i ? strchr(i+1,',') : NULL;
        uint32_t kp,ki,kd;
        if (i) *i++=0;
        if (d) *d++=0;
        if (tec_on || armed) error(cmd,"OUTPUTS_ACTIVE");
        else if (!i || !d || !protocol_uint(arg,10000,&kp) ||
                 !protocol_uint(i,10000,&ki) || !protocol_uint(d,10000,&kd)) error(cmd,"INVALID_PARAM");
        else { pid_init(&thermal.pid,(float)kp,(float)ki,(float)kd,-500,500,-500,500); ack(cmd); }
        return;
    }
    if (!strcmp(cmd, "tec_enable") && numeric && n <= 1) {
        if (!n) { lt8722_disable(); temperature_stop(&thermal); tec_on=false; ack(cmd); }
        else if (tec_on) { ack(cmd); }
        else if (!CEIL_ENABLE_TEC || fault || !temperature_start(&thermal,sensors_laser_sample(),time_us_32()))
            error(cmd,"TEC_DISABLED_TEMPERATURE_OR_POLARITY");
        else if (lt8722_enable()) {
            tec_on=true;
            if (thermal_service(true)) ack(cmd); else error(cmd,"TEC_FEEDBACK_OR_SPI");
        } else { emergency_stop(true); thermal.fault=true; error(cmd,"TEC_SPI_FAULT"); }
        return;
    }
    if (!strcmp(cmd, "tec_mv")) { error(cmd,"PID_OWNS_OUTPUT"); return; }
    if ((!strcmp(cmd, "capture") || !strcmp(cmd, "start")) && numeric && n && n <= 1000000) {
        emitting = !strcmp(cmd, "start");
        if (fault || (emitting && (!armed || !laser_code || board_laser_fault() || !laser_temperature_ok()))) { error(cmd, "NOT_ARMED_OR_FAULT"); return; }
        memset(histogram, 0, sizeof histogram);
        ready = false; accumulated = 0; remaining = n; running = true;
        if (!begin_capture()) { emergency_stop(true); error(cmd, "ACQUISITION"); }
        else ack(cmd);
        return;
    }
    error(cmd, "UNKNOWN_COMMAND_OR_INVALID_PARAM");
}
int main(void) {
    set_sys_clock_khz(SYSTEM_KHZ, true);
    board_init(); lt8722_init(); sensors_init(); temperature_init(&thermal);
    prn_generate(code); prn_tx_init(code); sipm_counter_init();
    bin_acquisition_init(); test_pulse_init();
    gpio_set_irq_enabled_with_callback(PIN_LD_ERROR, GPIO_IRQ_EDGE_FALL, true, fault_irq);
    stdio_init_all(); watchdog_enable(1000, true);
    protocol_parser parser = {0};
    lease_us = time_us_32();
    for (;;) {
        watchdog_update(); bin_acquisition_poll();
        bool connected = stdio_usb_connected();
        if (driver_fault || ((armed || bias_on || tec_on || running) &&
            (!connected || time_us_32() - lease_us > 2000000))) {
            emergency_stop(true); driver_fault = false;
        }
        acquisition_status s = bin_acquisition_status();
        (void)thermal_service(!s.busy);
        if (running && s.failed) emergency_stop(true);
        if (running && s.ready && !consumed) {
            consumed = true;
            if (!histogram_add(histogram, bin_acquisition_data(), s.samples)) emergency_stop(true);
            else {
                accumulated += capture_frames; remaining -= capture_frames;
                if (remaining) {
                    if (!begin_capture() && running) emergency_stop(true);
                } else {
                    running = false; emitting = false;
                    uint32_t t = time_us_32(); correlate(histogram, code, profile);
                    correlation_us = time_us_32() - t; ready = true;
                }
            }
        }
        for (unsigned budget = 0; connected && budget < 32; ++budget) {
            int c = getchar_timeout_us(0); if (c == PICO_ERROR_TIMEOUT) break;
            protocol_feed(&parser, (char)c, command, NULL);
        }
        tight_loop_contents();
    }
}
