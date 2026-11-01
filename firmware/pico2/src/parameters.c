#include "parameters.h"
#include "config.h"
#include <string.h>
#include <limits.h>
#define PARAM_DEFAULT(name,unit,lo,hi,def) def,
parameter_values parameters_current={{PARAMETER_LIST(PARAM_DEFAULT)}};
#undef PARAM_DEFAULT
#define PARAM_DESC(name,unit,lo,hi,def) {#name,unit,lo,hi,def},
const parameter_descriptor parameter_descriptors[PARAM_COUNT]={PARAMETER_LIST(PARAM_DESC)};
#undef PARAM_DESC
void parameters_defaults(parameter_values *p) {
    for(unsigned i=0;i<PARAM_COUNT;++i) p->values[i]=parameter_descriptors[i].default_value;
}
int parameter_find(const char *name) {
    for(unsigned i=0;i<PARAM_COUNT;++i) if(!strcmp(name,parameter_descriptors[i].name)) return (int)i;
    return -1;
}
bool parameter_parse(const char *s,int32_t *value) {
    if(!s || !*s) return false;
    bool neg=*s=='-'; if(neg) ++s;
    if(!*s) return false;
    uint32_t n=0,max=neg ? 2147483648u : INT32_MAX;
    for(;*s;++s) {
        if(*s<'0'||*s>'9') return false;
        uint32_t digit=(uint32_t)(*s-'0');
        if(n>(max-digit)/10u) return false;
        n=n*10u+digit;
    }
    *value=neg ? (n==2147483648u ? INT32_MIN : -(int32_t)n) : (int32_t)n;
    return true;
}
bool parameter_assign(parameter_values *p,const char *name,const char *text) {
    int i=parameter_find(name); int32_t value;
    if(i<0 || !parameter_parse(text,&value) || value<parameter_descriptors[i].minimum ||
       value>parameter_descriptors[i].maximum) return false;
    p->values[i]=value; return true;
}
bool parameters_validate(const parameter_values *p) {
    for(unsigned i=0;i<PARAM_COUNT;++i)
        if(p->values[i]<parameter_descriptors[i].minimum || p->values[i]>parameter_descriptors[i].maximum) return false;
#define V(name) p->values[PARAM_##name]
    if(V(tec_min_mc)>=V(tec_max_mc) || V(laser_min_mc)>=V(laser_max_mc) ||
       V(laser_min_mc)<V(tec_min_mc) || V(laser_max_mc)>V(tec_max_mc) ||
       V(tec_target_mc)<V(laser_min_mc) || V(tec_target_mc)>V(laser_max_mc) ||
       V(temperature_age_ms)<V(ds_period_ms)+850 || V(pid_integral_mv)>V(tec_output_mv)) return false;
    if(V(prn_length)!=1023 && V(prn_length)!=4095) return false;
    if((SYSTEM_KHZ*1000u)%(uint32_t)V(chip_hz) || ((SYSTEM_KHZ*1000u)/(uint32_t)V(chip_hz))%CHIP_CYCLES) return false;
#undef V
    return true;
}
