#include "parameter_storage.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"FAIL %s:%d %s\n",__FILE__,__LINE__,#x);exit(1); } } while(0)
static uint8_t flash[STORAGE_SIZE],backup[STORAGE_SIZE];
static unsigned programs,erases;
static unsigned program_cut=256,erase_cut=4096;
static bool erase(void *ctx,unsigned sector) {
    (void)ctx;CHECK(sector<2);++erases;memset(flash+sector*4096,255,erase_cut);return erase_cut==4096;
}
static bool program(void *ctx,unsigned page,const uint8_t *p) {
    (void)ctx;CHECK(page<32);++programs;
    for(unsigned i=0;i<program_cut;++i) {
        CHECK((flash[page*256+i]&p[i])==p[i]);flash[page*256+i]&=p[i];
    }
    return program_cut==256;
}
static storage_io io(void) { return (storage_io){.data=flash,.erase=erase,.program=program}; }
int main(void) {
    parameter_values p,candidate,loaded;parameters_defaults(&p);CHECK(parameters_validate(&p));
    int32_t number;
    CHECK(parameter_parse("-2147483648",&number)&&number==INT32_MIN);
    CHECK(!parameter_parse("2147483648",&number)&&!parameter_parse("-2147483649",&number));
    CHECK(!parameter_parse("1junk",&number)&&!parameter_parse("",&number));
    for(unsigned i=0;i<PARAM_COUNT;++i) {
        const parameter_descriptor *d=&parameter_descriptors[i];CHECK(parameter_find(d->name)==(int)i);
        candidate=p;candidate.values[i]=d->minimum-1;CHECK(!parameters_validate(&candidate));
        candidate=p;candidate.values[i]=d->maximum+1;CHECK(!parameters_validate(&candidate));
    }
    candidate=p;candidate.values[PARAM_laser_min_mc]=31000;CHECK(!parameters_validate(&candidate));
    candidate=p;candidate.values[PARAM_chip_hz]=9999999;CHECK(!parameters_validate(&candidate));
    candidate=p;candidate.values[PARAM_temperature_age_ms]=1000;CHECK(!parameters_validate(&candidate));
    candidate=p;candidate.values[PARAM_laser_min_mc]=5000;candidate.values[PARAM_laser_max_mc]=35000;
    candidate.values[PARAM_tec_target_mc]=32000;CHECK(parameters_validate(&candidate));
    CHECK(storage_crc32((const uint8_t *)"123456789",9)==0xcbf43926u);
    uint8_t record[256];uint32_t gen;
    storage_record_encode(record,&p,42);
    for(unsigned byte=0;byte<256;++byte) for(unsigned bit=0;bit<8;++bit) {
        record[byte]^=(uint8_t)(1u<<bit);
        CHECK(!storage_record_decode(record,&loaded,&gen));
        record[byte]^=(uint8_t)(1u<<bit);
    }
    candidate=p;candidate.values[PARAM_laser_min_mc]=40000;
    storage_record_encode(record,&candidate,43);
    CHECK(!storage_record_decode(record,&loaded,&gen)); /* even CRC-valid unsafe configurations are rejected */
    memset(flash,255,sizeof flash);parameter_journal j;
    CHECK(!storage_journal_load(&j,io(),&loaded)&&!j.corrupt);
    CHECK(storage_journal_save(&j,&p)&&j.generation==1);
    unsigned before=programs;CHECK(storage_journal_save(&j,&p)&&programs==before);
    CHECK(!storage_journal_dirty(&j,&p));
    candidate=p;candidate.values[PARAM_threshold_code]=1234;
    CHECK(storage_journal_dirty(&j,&candidate));memcpy(backup,flash,sizeof flash);
    for(unsigned cut=0;cut<256;++cut) {
        memcpy(flash,backup,sizeof flash);program_cut=cut;
        CHECK(storage_journal_load(&j,io(),&loaded));
        CHECK(!storage_journal_save(&j,&candidate));
        CHECK(storage_journal_load(&j,io(),&loaded));
        CHECK(loaded.values[PARAM_threshold_code]==0 || loaded.values[PARAM_threshold_code]==1234);
    }
    program_cut=256;memcpy(flash,backup,sizeof flash);
    CHECK(storage_journal_load(&j,io(),&loaded));CHECK(storage_journal_save(&j,&candidate));
    flash[256+12]^=1;CHECK(storage_journal_load(&j,io(),&loaded));
    CHECK(j.generation==1 && j.corrupt && loaded.values[PARAM_threshold_code]==0); /* metadata CRC protected */
    memset(flash,255,sizeof flash);CHECK(!storage_journal_load(&j,io(),&loaded));
    for(unsigned i=0;i<32;++i) { p.values[PARAM_threshold_code]=(int32_t)i;CHECK(storage_journal_save(&j,&p)); }
    CHECK(j.page==31);memcpy(backup,flash,sizeof flash);
    for(unsigned cut=0;cut<4096;cut+=97) {
        memcpy(flash,backup,sizeof flash);erase_cut=cut;
        CHECK(storage_journal_load(&j,io(),&loaded));p.values[PARAM_threshold_code]=32;
        CHECK(!storage_journal_save(&j,&p));CHECK(storage_journal_load(&j,io(),&loaded));
        CHECK(loaded.values[PARAM_threshold_code]==31); /* rollover never erases newest sector */
    }
    erase_cut=4096;memcpy(flash,backup,sizeof flash);CHECK(storage_journal_load(&j,io(),&loaded));
    CHECK(storage_journal_save(&j,&p)&&j.page==0&&j.generation==33);
    memset(flash,255,sizeof flash);parameters_defaults(&p);
    storage_record_encode(flash,&p,UINT32_MAX);p.values[PARAM_threshold_code]=17;
    storage_record_encode(flash+256,&p,0);CHECK(storage_journal_load(&j,io(),&loaded));
    CHECK(j.page==1 && j.generation==0 && loaded.values[PARAM_threshold_code]==17);
    flash[4]^=1;flash[256+4]^=1;CHECK(!storage_journal_load(&j,io(),&loaded)&&j.corrupt);
    CHECK(loaded.values[PARAM_laser_dac_code]==0 && loaded.values[PARAM_tec_polarity]==0);
    printf("PASS: %u descriptors, cross-validation, metadata CRC, every torn-program point, sector rollover failures, wear avoidance and generation wrap (%u programs/%u erases)\n",PARAM_COUNT,programs,erases);
    return 0;
}
