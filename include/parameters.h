#pragma once
#include <stdbool.h>
#include <stdint.h>
#define PARAMETER_LIST(X) \
 X(laser_min_mc,"mC",-10000,50000,10000) \
 X(laser_max_mc,"mC",-10000,50000,30000) \
 X(tec_target_mc,"mC",-10000,50000,24000) \
 X(tec_min_mc,"mC",-10000,50000,-10000) \
 X(tec_max_mc,"mC",-10000,50000,50000) \
 X(temperature_age_ms,"ms",1000,10000,2000) \
 X(ds_period_ms,"ms",800,5000,1000) \
 X(pid_p_milli,"milli_mV_per_C",0,10000000,100000) \
 X(pid_i_milli,"milli_mV_per_C_s",0,10000000,10000) \
 X(pid_d_milli,"milli_mV_s_per_C",0,10000000,0) \
 X(tec_polarity,"sign",-1,1,0) \
 X(tec_output_mv,"mV",0,500,500) \
 X(pid_integral_mv,"mV",0,500,500) \
 X(tec_ilimn_code,"code",48,64,64) \
 X(tec_ilimp_code,"code",448,462,448) \
 X(tec_frequency,"selector",0,5,5) \
 X(laser_dac_code,"code",0,65535,0) \
 X(bias_dac_code,"retired",0,0,0) \
 X(threshold_code,"code",0,65535,0) \
 X(chip_hz,"Hz",10000,10000000,10000000) \
 X(integration_frames,"frames",1,1000000,16) \
 X(prn_seed,"state",1,1023,1023) \
 X(range_offset_bins,"bins",-1022,1022,0) \
 X(range_average_bins,"bins",1,1023,1) \
 X(usb_lease_ms,"ms",500,10000,2000) \
 X(telemetry_period_ms,"ms",20,2000,100) \
 X(active_adc_period_us,"us",100,10000,1000) \
 X(amux_settle_us,"us",100,2000,1000) \
 X(adc_average,"samples",1,64,8) \
 X(adc_reference_uv,"uV",2800000,3100000,3000000) \
 X(laser_sense_uv_per_ma,"uV_per_mA",0,10000,0) \
 X(laser_sense_offset_uv,"uV",-100000,100000,0) \
 X(bias_sense_uv_per_ua,"uV_per_uA",0,1000000,0) \
 X(bias_sense_offset_uv,"uV",-100000,100000,0) \
 X(laser_temp_offset_mc,"mC",-5000,5000,0) \
 X(receiver_temp_offset_mc,"mC",-5000,5000,0) \
 X(receiver_resolution,"bits",9,12,12) \
 X(laser_dac_ref_uv,"uV",2400000,2600000,2500000) \
 X(bias_dac_ref_uv,"uV",2400000,2600000,2500000) \
 X(threshold_dac_ref_uv,"uV",2400000,2600000,2500000) \
 X(sipm_overvoltage_mv,"mV",100,12000,10000) \
 X(sipm_breakdown_mv,"mV_at_reference",35000,38000,36500) \
 X(sipm_reference_mc,"mC",-40000,85000,25000) \
 X(sipm_tempco_uv_c,"uV_per_C",0,100000,70000) \
 X(bias_limit_mv,"mV",10000,50000,50000) \
 X(bias_temperature_age_ms,"ms",800,5000,1000) \
 X(bias_gain_ppm,"ppm_of_nominal",800000,1200000,1000000) \
 X(bias_offset_uv,"uV",-6000000,-4000000,-5000000)
#define PARAM_ENUM(name,unit,lo,hi,def) PARAM_##name,
typedef enum { PARAMETER_LIST(PARAM_ENUM) PARAM_COUNT } parameter_id;
#undef PARAM_ENUM
typedef struct { int32_t values[PARAM_COUNT]; } parameter_values;
typedef struct { const char *name,*unit; int32_t minimum,maximum,default_value; } parameter_descriptor;
extern parameter_values parameters_current;
extern const parameter_descriptor parameter_descriptors[PARAM_COUNT];
#define PARAM(name) (parameters_current.values[PARAM_##name])
void parameters_defaults(parameter_values *p);
bool parameters_validate(const parameter_values *p);
int parameter_find(const char *name);
bool parameter_parse(const char *text,int32_t *value);
bool parameter_assign(parameter_values *p,const char *name,const char *text);
