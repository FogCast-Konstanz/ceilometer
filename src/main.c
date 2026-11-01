#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "hardware/clocks.h"
#include "hardware/watchdog.h"
#ifdef PICO_ON_DEVICE
#include "hardware/vreg.h"
#include "pico/rand.h"
#include "hardware/structs/qmi.h"
#endif
#include "board.h"
#include "sequence.h"
#include "prn_tx.h"
#include "sipm_counter.h"
#include "bin_acquisition.h"
#include "stream.h"
#include "correlation.h"
#include "protocol.h"
#include "lt8722.h"
#include "sensors.h"
#include "test_pulse.h"
#include "temperature_control.h"
#include "parameters.h"
#include "parameter_storage.h"
#include "telemetry.h"
#include "sipm_bias.h"
static uint8_t code[PRN_LENGTH];
static uint64_t histogram[PRN_LENGTH];
static int64_t profile[PRN_LENGTH];
static uint32_t chip_hz = DEFAULT_CHIP_HZ, remaining, accumulated, capture_frames;
static uint32_t lease_us, correlation_us;
static bool armed, fault, running, emitting, ready, consumed, tec_on, bias_on;
static bool streaming;
static bool boot_watchdog;
static unsigned boot_stage;
static void runtime_stage(unsigned stage) {
#ifdef PICO_ON_DEVICE
    watchdog_hw->scratch[0]=0xce110000u|stage;
#else
    (void)stage;
#endif
}
static uint64_t capture_started_us,capture_wall_ns;
static uint32_t stream_last_blocks,stream_progress_us;
static uint64_t boot_id;
static uint16_t laser_code;
static temperature_controller thermal;
static uint32_t last_tec_health;
static volatile bool driver_fault;
static void error(const char *cmd, const char *reason) { printf("$NACK,%s,%s\r\n", cmd, reason); }
static void ack(const char *cmd) { printf("$ACK,%s\r\n", cmd); }
static void emergency_stop(bool latch) {
    stream_stop();streaming=false;
    bin_acquisition_stop(); sipm_bias_stop(); board_outputs_off(); lt8722_disable(); temperature_stop(&thermal);
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
static bool configuration_idle(void) { return !running && !armed && !tec_on && !bias_on; }
static bool configuration_commit(const parameter_values *candidate) {
    if(!configuration_idle() || !parameters_validate(candidate)) return false;
    if(candidate->values[PARAM_threshold_code]!=PARAM(threshold_code))
        (void)board_dac(2,(uint16_t)candidate->values[PARAM_threshold_code]);
    parameters_current=*candidate;
    /* Config can be recovered with absent peripherals. Telemetry reports/retries readback. */
    telemetry_init();
    /* Changed calibration invalidates cached bias feedback; preserve latched faults. */
    sipm_bias_reconfigure();
    bool thermal_fault=thermal.fault;
    temperature_init(&thermal);thermal.fault=thermal_fault;
    chip_hz=(uint32_t)PARAM(chip_hz);ready=false;
    prn_generate_seed(code,(uint16_t)PARAM(prn_seed));prn_tx_set_code(code);
    return true;
}
static bool legacy_parameter(const char *cmd,const char *name,const char *arg) {
    parameter_values candidate=parameters_current;
    if(!parameter_assign(&candidate,name,arg) || !configuration_commit(&candidate)) {
        error(cmd,configuration_idle() ? "INVALID_CONFIGURATION" : "OUTPUTS_ACTIVE");return false;
    }
    ack(cmd);return true;
}
static bool configuration_command(const char *cmd,char *arg) {
    uint32_t index;
    if(!strcmp(cmd,"parameter_count") && !arg) { printf("$ACK,%s,%u,%u\r\n",cmd,(unsigned)PARAM_COUNT,STORAGE_SCHEMA_VERSION);return true; }
    if(!strcmp(cmd,"parameter_info") && protocol_uint(arg,PARAM_COUNT-1,&index)) {
        const parameter_descriptor *d=&parameter_descriptors[index];
        printf("$ACK,%s,%lu,%s,%s,%ld,%ld,%ld\r\n",cmd,(unsigned long)index,d->name,d->unit,
               (long)d->minimum,(long)d->maximum,(long)d->default_value);return true;
    }
    if(!strcmp(cmd,"get_param") && arg) {
        int i=parameter_find(arg);
        if(i<0) error(cmd,"UNKNOWN_PARAMETER");
        else {
            const parameter_descriptor *d=&parameter_descriptors[i];
            printf("$ACK,%s,%s,%ld,%s,%ld,%ld,%ld\r\n",cmd,d->name,(long)parameters_current.values[i],
                   d->unit,(long)d->minimum,(long)d->maximum,(long)d->default_value);
        }
        return true;
    }
    if(!strcmp(cmd,"get_storage") && !arg) {
        const parameter_journal *j=parameter_storage_state();
        printf("$ACK,%s,%d,%d,%lu,%d,%d,%u\r\n",cmd,j->loaded,parameter_storage_dirty(&parameters_current),
               (unsigned long)j->generation,j->page,j->corrupt,STORAGE_SCHEMA_VERSION);return true;
    }
    if(!strcmp(cmd,"telemetry_count") && !arg) {
        printf("$ACK,%s,%u\r\n",cmd,(unsigned)TELEMETRY_COUNT);return true;
    }
    if(!strcmp(cmd,"telemetry_info") && protocol_uint(arg,TELEMETRY_COUNT-1,&index)) {
        printf("$ACK,%s,%lu,%s,%s\r\n",cmd,(unsigned long)index,
               telemetry_descriptors[index].name,telemetry_descriptors[index].unit);return true;
    }
    if(!strcmp(cmd,"get_telemetry") && arg) {
        int i=telemetry_find(arg);
        if(i<0 && protocol_uint(arg,TELEMETRY_COUNT-1,&index)) i=(int)index;
        telemetry_value value;uint32_t now=time_us_32();
        if(i<0 || !telemetry_read((unsigned)i,now,&value)) error(cmd,"UNKNOWN_TELEMETRY");
        else printf("$ACK,%s,%s,%s,%" PRId64 ",%d,%lu,%lu,%lu\r\n",cmd,telemetry_descriptors[i].name,
                    telemetry_descriptors[i].unit,value.value,value.valid,(unsigned long)value.sampled_us,
                    (unsigned long)(now-value.sampled_us),(unsigned long)value.sequence);
        return true;
    }
    bool set=!strcmp(cmd,"set_param") || !strcmp(cmd,"set_params");
    if(set || !strcmp(cmd,"save") || !strcmp(cmd,"load") || !strcmp(cmd,"defaults")) {
        if(!configuration_idle()) { error(cmd,"OUTPUTS_ACTIVE");return true; }
        if(!strcmp(cmd,"save")) {
            if(arg) error(cmd,"INVALID_PARAM");
            else {
                (void)test_pulse_set(0);watchdog_update();
                if(parameter_storage_save(&parameters_current)) ack(cmd);else error(cmd,"FLASH_VERIFY_FAILED");
                watchdog_update();
            }
            return true;
        }
        parameter_values candidate=parameters_current;
        bool ok=true;
        if(set) {
            if(!arg || !*arg) ok=false;
            unsigned pairs=0;
            while(arg && *arg && ok) {
                char *value=strchr(arg,',');
                if(!value) { ok=false;break; }
                *value++=0;char *next=strchr(value,',');if(next) *next++=0;
                if(++pairs>8 || !parameter_assign(&candidate,arg,value)) ok=false;
                if(next && !*next) ok=false;
                arg=next;
            }
            if(!pairs) ok=false;
        } else if(arg) ok=false;
        else if(!strcmp(cmd,"defaults")) parameters_defaults(&candidate);
        else if(!parameter_storage_load(&candidate)) { error(cmd,"NO_VALID_SAVED_PARAMETERS");return true; }
        if(!ok || !configuration_commit(&candidate)) error(cmd,"INVALID_CONFIGURATION");else ack(cmd);
        return true;
    }
    return false;
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
        stream_stop();streaming=false;
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
static bool bias_service(bool available) {
    (void)sipm_bias_service(available);
    if(bias_on && (!sipm_bias_status().enabled || sipm_bias_status().fault)) {
        emergency_stop(true);return false;
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
    if (!thermal_service(true) || !bias_service(true) || !running) return false;
    if (emitting && (!armed || !laser_temperature_ok())) return false;
    unsigned discard=(unsigned)PARAM(startup_discard_frames);
    if(discard>=CAPTURE_MAX_FRAMES) return false;
    unsigned capacity=CAPTURE_MAX_FRAMES-discard;
    capture_frames = remaining > capacity ? capacity : remaining;
    /* Limit sensor-service delay at slow chip rates (one frame minimum). */
    if (armed || tec_on || bias_on) {
        unsigned cap = chip_hz / (20u * PRN_LENGTH);
        if (!cap) cap = 1;
        if (capture_frames > cap) capture_frames = cap;
    }
    consumed = false;
    prn_tx_set_code(code);
    bool ok=bin_acquisition_start(capture_frames+discard,chip_hz,emitting);
    if(ok && emitting) telemetry_capture_active(true);
    return ok;
}
static void command(char *cmd, char *arg, void *context) {
    (void)context;
    uint32_t n = 0, count = 0;
    bool numeric = protocol_uint(arg, UINT32_MAX, &n);
    if(!arg) {
        if(!strcmp(cmd,"capture") || !strcmp(cmd,"start")) { n=(uint32_t)PARAM(integration_frames);numeric=true; }
        if(!strcmp(cmd,"laser_dac")) { n=(uint32_t)PARAM(laser_dac_code);numeric=true; }
        if(!strcmp(cmd,"bias_dac")) { n=(uint32_t)PARAM(bias_dac_code);numeric=true; }
    }
    lease_us = time_us_32();
    if (!strcmp(cmd, "stop") && !arg) { emergency_stop(false); ack(cmd); return; }
#ifdef PICO_ON_DEVICE
    if(!strcmp(cmd,"get_clock") && !arg) {
        printf("$ACK,%s,%lu,%lu,%u\r\n",cmd,(unsigned long)clock_get_hz(clk_sys),
            (unsigned long)frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_SYS),vreg_get_voltage());return;
    }
#endif
    if(!strcmp(cmd,"get_runtime") && !arg) {
        printf("$ACK,%s,%" PRIu64 ",%" PRIu64 ",%d,%u\r\n",cmd,boot_id,time_us_64(),boot_watchdog,boot_stage);
        return;
    }
    if (!strcmp(cmd, "get_info") && !arg) {
        printf("$ACK,get_info,ceilometer,0.6.4,pico2,%lu,%lu,%u,%d,%d,%d\r\n",
            (unsigned long)clock_get_hz(clk_sys), (unsigned long)chip_hz, PRN_LENGTH,
            CEIL_ENABLE_LASER, CEIL_ENABLE_BIAS, CEIL_ENABLE_TEC); return;
    }
    if (!strcmp(cmd, "get_state") && !arg) {
        acquisition_status s = bin_acquisition_status();
        printf("$ACK,get_state,%s,%lu,%lu,%lu,%d,%d,%d,%lu\r\n",
            fault ? "FAULT" : running ? "ACQUIRING" : ready ? "READY" : "IDLE",
            (unsigned long)accumulated, (unsigned long)remaining, (unsigned long)(streaming ? 0 : s.elapsed_us),
            streaming ? false : s.tx_starved, armed, bias_on, (unsigned long)correlation_us); return;
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
    if(!strcmp(cmd,"get_limits") && !arg) {
        printf("$ACK,%s,%u,%u,500,500000000,1023,4186112\r\n",cmd,
               (unsigned)PARAM(laser_max_code),(unsigned)CEIL_BIAS_MAX_CODE);return;
    }
    if(!strcmp(cmd,"get_test_pulse") && !arg) {
        printf("$ACK,%s,%lu\r\n",cmd,(unsigned long)test_pulse_actual_hz());return;
    }
    if(!strcmp(cmd,"get_bias_test") && !arg) {
        printf("$ACK,%s,%d\r\n",cmd,sipm_bias_test_active());return;
    }
    if(!strcmp(cmd,"get_bias") && !arg) {
        bias_status b=sipm_bias_status();uint32_t now=time_us_32();
        printf("$ACK,%s,%d,%d,%u,%d,%ld,%lu,%" PRId64 ",%" PRId64 ",%" PRId64 ",%u\r\n",
            cmd,b.enabled,b.fault,(unsigned)b.reason,b.valid,(long)b.temperature.mc,
            (unsigned long)(now-b.temperature.sampled_us),b.breakdown_uv,b.target_uv,b.estimated_uv,b.code);
        return;
    }
    if(!strcmp(cmd,"stream_debug") && !arg) {
        uint32_t values[8];stream_debug(values);printf("$ACK,%s",cmd);
        for(unsigned i=0;i<8;++i) printf(",%lu",(unsigned long)values[i]);
        printf("\r\n");return;
    }
    if(!strcmp(cmd,"stream_status") && !arg) {
        stream_status s=stream_get_status();const integration_result *v=integration_pending();
        printf("$ACK,%s,%d,%d,%lu,%lu,%lu,%lu,%lu\r\n",cmd,s.active,s.failed,
            (unsigned long)s.blocks,(unsigned long)s.max_process_us,
            (unsigned long)integration_dropped(),(unsigned long)(v ? v->id : 0),(unsigned long)s.error_flags);return;
    }
    if(!strcmp(cmd,"capture_timing") && !arg) {
        if(!ready || streaming) {error(cmd,"NO_VALID_RESULT");return;}
        uint64_t live=(uint64_t)accumulated*PRN_LENGTH*1000000000ull/chip_hz;
        printf("$ACK,%s,%" PRIu64 ",%" PRIu64 ",%" PRIu64 "\r\n",cmd,
            live,capture_wall_ns,capture_wall_ns>live ? capture_wall_ns-live : 0);
        return;
    }
    if(!strcmp(cmd,"stream_timing") && !arg) {
        const integration_result *v=integration_pending();
        if(!v || stream_get_status().failed) {error(cmd,"NO_VALID_RESULT");return;}
        uint64_t wall=stream_wall_ns();
        printf("$ACK,%s,%lu,finite_burst,%" PRIu64 ",%" PRIu64 "\r\n",cmd,
            (unsigned long)v->id,wall,wall>v->exposure_ns ? wall-v->exposure_ns : 0);
        return;
    }
    if(!strcmp(cmd,"stream_result") && !arg) {
        const integration_result *v=integration_pending();stream_status s=stream_get_status();
        if(!v || s.failed) {error(cmd,"NO_VALID_RESULT");return;}
        printf("$ACK,%s,%lu,%lu,%lu,%lu,%lu,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%lu\r\n",cmd,
            (unsigned long)v->id,(unsigned long)v->length,(unsigned long)v->frames,
            (unsigned long)v->chip_hz,(unsigned long)v->period_us,
            v->first_chip,v->end_chip,v->exposure_ns,s.started_us,boot_id,
            (unsigned long)storage_crc32((const uint8_t *)&parameters_current,sizeof parameters_current));return;
    }
    if(!strcmp(cmd,"stream_release") && numeric) {
        if(integration_release(n)) ack(cmd);else error(cmd,"RESULT_ID_MISMATCH");return;
    }
    if(!strcmp(cmd,"stream_histogram")) {
        uint32_t id;char *p=arg ? strchr(arg,',') : NULL;if(p) *p++=0;
        const integration_result *v=integration_pending();
        if(!v || stream_get_status().failed || !protocol_uint(arg,UINT32_MAX,&id) || id!=v->id ||
           !page(p,v->length,&n,&count)) {error(cmd,"INVALID_RESULT_OR_PAGE");return;}
        printf("$ACK,%s,%lu",cmd,(unsigned long)n);
        for(unsigned i=n;i<n+count;++i) printf(",%" PRIu64,integration_histogram()[i]);
        printf("\r\n");return;
    }
    if(!strcmp(cmd,"stream_binary")) {
        uint32_t id=0,offset=0,count=0;char *p=arg ? strchr(arg,',') : NULL;
        if(p) *p++=0;
        char *q=p ? strchr(p,',') : NULL;if(q) *q++=0;
        const integration_result *v=integration_pending();
        if(!v || stream_get_status().failed || !protocol_uint(arg,UINT32_MAX,&id) || id!=v->id ||
           !protocol_uint(p,v->length-1,&offset) || !protocol_uint(q,128,&count) ||
           !count || offset+count>v->length) {error(cmd,"INVALID_RESULT_OR_PAGE");return;}
        static uint8_t packet[24+128*8+4];
        uint32_t header[6]={0x314c4943u,1u,id,offset,count,v->length};
        /* RP2350 is little-endian. CRC covers header and uint64 payload. */
        memcpy(packet,header,sizeof header);
        memcpy(packet+24,integration_histogram()+offset,count*8u);
        uint32_t bytes=24+count*8u,crc=storage_crc32(packet,bytes);
        memcpy(packet+bytes,&crc,4);bytes+=4;
        printf("$ACK,%s,%lu\r\n",cmd,(unsigned long)bytes);
        for(unsigned i=0;i<bytes;i+=64) {
            watchdog_update();
            if(driver_fault || !stdio_usb_connected() || time_us_32()-lease_us>100000u ||
               time_us_32()-lease_us>(uint32_t)PARAM(usb_lease_ms)*1000u) {
                emergency_stop(true);return; /* discard partial page and reconnect */
            }
            unsigned chunk=bytes-i;if(chunk>64) chunk=64;
            stdio_put_string((const char *)packet+i,(int)chunk,false,false);
        }
        return;
    }
    if(configuration_command(cmd,arg)) return;
    if (running) { error(cmd, "BUSY"); return; }
    if((!strcmp(cmd,"stream_start") || !strcmp(cmd,"stream_capture")) && !arg) {
        bool laser=!strcmp(cmd,"stream_start");
        if((unsigned)PARAM(startup_discard_frames)>=(CAPTURE_MAX_SAMPLES-1u)/(unsigned)PARAM(prn_length)) {
            error(cmd,"STARTUP_DISCARD_EXCEEDS_BURST_CAPACITY");return;
        }
        if(fault || (laser && (!armed || !laser_code || board_laser_fault() || !laser_temperature_ok())) ||
           sipm_bias_test_active()) {error(cmd,"NOT_ARMED_OR_FAULT_OR_TEST_MODE");return;}
        if(!thermal_service(true) || !bias_service(true)) {error(cmd,"FEEDBACK_FAULT");return;}
        ready=false;emitting=laser;accumulated=remaining=correlation_us=0;
        if(!stream_start((unsigned)PARAM(prn_length),(uint16_t)PARAM(prn_seed),chip_hz,
              (uint32_t)PARAM(measurement_period_ms)*1000u,(unsigned)PARAM(startup_discard_frames),laser)) {
            emergency_stop(true);error(cmd,"STREAM_START_FAILED");return;
        }
        running=streaming=true;stream_last_blocks=0;stream_progress_us=time_us_32();
        telemetry_reset_active();ack(cmd);return;
    }
    if(!strcmp(cmd,"count_window")) {
        if(!numeric || n<1000 || n>100000) { error(cmd,"INVALID_PARAM");return; }
        uint32_t start=time_us_32(),previous=start,max_gap=0;
        uint16_t last=sipm_counter_read();uint64_t total=0;
        watchdog_update();
        while(time_us_32()-start<n) {
            uint16_t current=sipm_counter_read();uint32_t now=time_us_32();
            total+=(uint16_t)(current-last);last=current;
            if(now-previous>max_gap) max_gap=now-previous;
            previous=now;
        }
        printf("$ACK,%s,%" PRIu64 ",%lu,%lu\r\n",cmd,total,
            (unsigned long)(time_us_32()-start),(unsigned long)max_gap);
        watchdog_update();return;
    }
    if(!strcmp(cmd,"i2c_recover") && numeric && n<=1) {
        if(!configuration_idle()) { error(cmd,"OUTPUTS_ACTIVE");return; }
        bool ok=board_i2c_recover(n);
        printf("$ACK,%s,%lu,%d,%d,%d\r\n",cmd,(unsigned long)n,ok,
            board_pin(n ? PIN_RX_SDA : PIN_LD_SDA),board_pin(n ? PIN_RX_SCL : PIN_LD_SCL));
        return;
    }
    if(!strcmp(cmd,"i2c_scan") && numeric && n<=1) {
        if(!configuration_idle()) { error(cmd,"OUTPUTS_ACTIVE");return; }
        unsigned sda=n ? PIN_RX_SDA : PIN_LD_SDA,scl=n ? PIN_RX_SCL : PIN_LD_SCL;
        bool sda_before=board_pin(sda),scl_before=board_pin(scl);
        uint8_t addresses[112];unsigned count=0,timeouts=0,nacks=0;
        for(unsigned address=0x08;address<=0x77;++address) {
            int result=board_i2c_probe(n,(uint8_t)address);
            if(result==2) addresses[count++]=(uint8_t)address;
            else if(result==PICO_ERROR_TIMEOUT) ++timeouts;
            else ++nacks;
            if((address&15u)==0) watchdog_update();
        }
        printf("$ACK,%s,%lu,%d,%d,%d,%d,%u,%u,%u",cmd,(unsigned long)n,
            sda_before,scl_before,board_pin(sda),board_pin(scl),timeouts,nacks,count);
        for(unsigned i=0;i<count;++i) printf(",%u",addresses[i]);
        printf("\r\n");return;
    }
    if(!strcmp(cmd,"range_point") && numeric) {
        unsigned group=(unsigned)PARAM(range_average_bins),first=n*group;
        if(!ready || n>=(PRN_LENGTH+group-1)/group) { error(cmd,"NO_DATA_OR_INVALID_INDEX");return; }
        unsigned end=first+group;if(end>PRN_LENGTH) end=PRN_LENGTH;
        int64_t sum=0;
        for(unsigned i=first;i<end;++i) sum+=profile[i]/(int64_t)(end-first);
        int64_t mm=(int64_t)(range_m(first,PARAM(range_offset_bins),chip_hz)*1000.0);
        printf("$ACK,%s,%lu,%u,%" PRId64 ",%" PRId64 "\r\n",cmd,(unsigned long)n,first,mm,sum);return;
    }
    if (!strcmp(cmd, "clear_fault") && !arg) {
        if (board_laser_fault()) error(cmd, "LD_FAULT_ACTIVE");
        else if (thermal.fault && !temperature_tec_allowed(sensors_laser_sample(),time_us_32())) error(cmd,"TEMPERATURE_INVALID");
        else if(!sipm_bias_clear_fault()) error(cmd,"BIAS_FEEDBACK_OR_LIMIT");
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
    if (!strcmp(cmd,"set_rate")) { (void)legacy_parameter(cmd,"chip_hz",arg);return; }
    if (!strcmp(cmd, "test_pulse") && numeric) {
        if (!test_pulse_set(n)) error(cmd, "INVALID_RATE");
        else printf("$ACK,test_pulse,%lu\r\n", (unsigned long)test_pulse_actual_hz());
        return;
    }
    if (!strcmp(cmd,"set_threshold")) {
        if(!numeric || n>65535) error(cmd,"INVALID_PARAM");
        else if(!board_dac(2,(uint16_t)n)) { emergency_stop(true);error(cmd,"I2C"); }
        else { PARAM(threshold_code)=(int32_t)n;ack(cmd); }
        return;
    }
    if (!strcmp(cmd, "arm") && numeric && n <= 1) {
        if (!n) { emergency_stop(false); ack(cmd); return; }
        if (!CEIL_ENABLE_LASER) error(cmd, "LASER_BUILD_DISABLED");
        else if(!PARAM(laser_max_code)) error(cmd,"LASER_LIMIT_ZERO");
        else if (fault || board_laser_fault()) error(cmd, "FAULT");
        else if (!laser_temperature_ok()) error(cmd, "LASER_TEMPERATURE_OUTSIDE_WINDOW");
        else { armed = true; ack(cmd); } return;
    }
    if (!strcmp(cmd, "laser_dac") && numeric && n <= 65535) {
        if (n && (!armed || fault || !laser_temperature_ok() || n > (uint32_t)PARAM(laser_max_code))) { error(cmd, "NOT_ARMED_OR_LIMIT"); return; }
        if (board_dac(0, (uint16_t)n)) { laser_code = (uint16_t)n; PARAM(laser_dac_code)=(int32_t)n; ack(cmd); }
        else { emergency_stop(true); error(cmd, "I2C"); } return;
    }
    if (!strcmp(cmd, "bias_dac") && numeric && n <= 65535) {
        if(n) error(cmd,"TEMPERATURE_COMPENSATION_OWNS_OUTPUT");
        else { sipm_bias_stop();bias_on=false;ack(cmd); } return;
    }
    if(!strcmp(cmd,"bias_test")) {
        int32_t mc;
        if(!configuration_idle()) error(cmd,"OUTPUTS_ACTIVE");
        else if(arg && !strcmp(arg,"off")) { sipm_bias_stop();ack(cmd); }
        else if(!parameter_parse(arg,&mc) || !sipm_bias_test_temperature(mc)) error(cmd,"INVALID_TEST_TEMPERATURE");
        else ack(cmd);
        return;
    }
    if (!strcmp(cmd, "bias_enable") && numeric && n <= 1) {
        if(!n) { sipm_bias_stop();bias_on=false;ack(cmd); }
        else if(fault || !sipm_bias_enable()) {
            if(sipm_bias_status().fault) emergency_stop(true);
            error(cmd,"BIAS_BUILD_TEMPERATURE_DAC_OR_LIMIT");
        } else { bias_on=true;ack(cmd); }
        return;
    }
    if (!strcmp(cmd,"tec_target")) { (void)legacy_parameter(cmd,"tec_target_mc",arg);return; }
    if (!strcmp(cmd,"tec_polarity")) { (void)legacy_parameter(cmd,"tec_polarity",arg);return; }
    if (!strcmp(cmd,"tec_pid") && arg) {
        char *i=strchr(arg,','),*d=i ? strchr(i+1,',') : NULL;
        uint32_t kp,ki,kd;
        if(i) *i++=0;
        if(d) *d++=0;
        if(!i || !d || !protocol_uint(arg,10000,&kp) || !protocol_uint(i,10000,&ki) || !protocol_uint(d,10000,&kd))
            error(cmd,"INVALID_PARAM");
        else {
            parameter_values candidate=parameters_current;
            candidate.values[PARAM_pid_p_milli]=(int32_t)kp*1000;
            candidate.values[PARAM_pid_i_milli]=(int32_t)ki*1000;
            candidate.values[PARAM_pid_d_milli]=(int32_t)kd*1000;
            if(configuration_commit(&candidate)) ack(cmd);else error(cmd,"OUTPUTS_ACTIVE_OR_INVALID");
        }
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
        if((unsigned)PARAM(startup_discard_frames)>=CAPTURE_MAX_FRAMES) {
            error(cmd,"STARTUP_DISCARD_EXCEEDS_BURST_CAPACITY");return;
        }
        emitting = !strcmp(cmd, "start");
        if (fault || (emitting && (!armed || !laser_code || board_laser_fault() || !laser_temperature_ok()))) { error(cmd, "NOT_ARMED_OR_FAULT"); return; }
        PARAM(integration_frames)=(int32_t)n;
        telemetry_reset_active();
        memset(histogram, 0, sizeof histogram);
        ready = false; accumulated = 0; remaining = n; running = true;
        capture_started_us=time_us_64();capture_wall_ns=0;
        if (!begin_capture()) { emergency_stop(true); error(cmd, "ACQUISITION"); }
        else ack(cmd);
        return;
    }
    error(cmd, "UNKNOWN_COMMAND_OR_INVALID_PARAM");
}
int main(void) {
    /* Establish safe levels before scanning persistent settings or starting clocks. */
    gpio_init(PIN_LD_GATE);gpio_put(PIN_LD_GATE,0);gpio_set_dir(PIN_LD_GATE,GPIO_OUT);
    gpio_init(PIN_BIAS_ENABLE);gpio_put(PIN_BIAS_ENABLE,0);gpio_set_dir(PIN_BIAS_ENABLE,GPIO_OUT);
    gpio_init(PIN_TEC_SWEN);gpio_put(PIN_TEC_SWEN,0);gpio_set_dir(PIN_TEC_SWEN,GPIO_OUT);
    parameter_storage_init();
#ifdef PICO_ON_DEVICE
    boot_watchdog=watchdog_caused_reboot();
    uint32_t previous_stage=watchdog_hw->scratch[0];
    boot_stage=(previous_stage&0xffff0000u)==0xce110000u ? previous_stage&0xffffu : 0;
    runtime_stage(10);
    boot_id=get_rand_64();
    watchdog_enable(1000,true);
    vreg_disable_voltage_limit();vreg_set_voltage(VREG_VOLTAGE_1_50);sleep_ms(20);
#endif
#ifdef PICO_ON_DEVICE
    hw_write_masked(&qmi_hw->m[0].timing,10u << QMI_M0_TIMING_CLKDIV_LSB,QMI_M0_TIMING_CLKDIV_BITS);
#endif
    set_sys_clock_khz(SYSTEM_KHZ,true);
    chip_hz=(uint32_t)PARAM(chip_hz);
    board_init(); lt8722_init(); lt8722_disable(); sensors_init(); temperature_init(&thermal); sipm_bias_init();
    prn_generate_seed(code,(uint16_t)PARAM(prn_seed)); prn_tx_init(code); sipm_counter_init();
    (void)board_dac(2,(uint16_t)PARAM(threshold_code));telemetry_init();
    bin_acquisition_init(); test_pulse_init();stream_init();
    gpio_set_irq_enabled_with_callback(PIN_LD_ERROR, GPIO_IRQ_EDGE_FALL, true, fault_irq);
    stdio_init_all(); watchdog_enable(1000, true);
    protocol_parser parser = {0};
    lease_us = time_us_32();
    for (;;) {
        runtime_stage(1);watchdog_update(); bin_acquisition_poll();
        bool connected = stdio_usb_connected();
        if (driver_fault || ((armed || bias_on || tec_on || running) &&
            (!connected || time_us_32() - lease_us > (uint32_t)PARAM(usb_lease_ms)*1000u))) {
            emergency_stop(true); driver_fault = false;
        }
        acquisition_status s = bin_acquisition_status();
        runtime_stage(2);(void)thermal_service(!s.busy);
        runtime_stage(3);(void)bias_service(!s.busy);
        runtime_stage(4);
        if((s.busy || streaming) && emitting && running) telemetry_capture_active(false);
        runtime_stage(5);telemetry_poll(s.busy); /* Slow peripheral work only between finite DMA bursts. */
        runtime_stage(6);if(streaming) stream_poll();
        if(streaming) {
            stream_status st=stream_get_status();
            if(st.blocks!=stream_last_blocks || integration_pending()) {stream_last_blocks=st.blocks;stream_progress_us=time_us_32();}
            if(st.failed || !st.active || (!integration_pending() && time_us_32()-stream_progress_us>100000u)) emergency_stop(true);
        }
        if (running && !streaming && s.failed) emergency_stop(true);
        if (running && !streaming && s.ready && !consumed) {
            consumed = true;
            if (!histogram_add(histogram, bin_acquisition_data()+(unsigned)PARAM(startup_discard_frames)*PRN_LENGTH, capture_frames*PRN_LENGTH+1u)) emergency_stop(true);
            else {
                accumulated += capture_frames; remaining -= capture_frames;
                if (remaining) {
                    if (!begin_capture() && running) emergency_stop(true);
                } else {
                    running = false; emitting = false;
                    capture_wall_ns=(s.completed_us-capture_started_us)*1000u;
                    uint32_t t = time_us_32(); correlate(histogram, code, profile);
                    correlation_us = time_us_32() - t; ready = true;
                }
            }
        }
        runtime_stage(7);
        for (unsigned budget = 0; connected && budget < 32; ++budget) {
            int c = getchar_timeout_us(0); if (c == PICO_ERROR_TIMEOUT) break;
            protocol_feed(&parser, (char)c, command, NULL);
        }
        runtime_stage(8);
        /* The alarm/WFE idle path stalled at 500 MHz (retained watchdog stage 8).
           Keep interrupts enabled and use a bounded timer busy wait instead. */
        if(!running) busy_wait_us_32(100);
        tight_loop_contents();
    }
}
