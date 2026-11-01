/* CRC/page-journal approach inspired by crossing/Drehrichtungssensor parameter_storage.c.
   Two sectors preserve the previous valid record across a torn rollover erase. */
#include "parameter_storage.h"
#include <string.h>
#define STORAGE_MAGIC 0x4345494cu
#define STORAGE_VERSION STORAGE_SCHEMA_VERSION
_Static_assert(20u+PARAM_COUNT*4u<=252u,"Settings no longer fit one flash page; bump schema/layout");
static void put32(uint8_t *p,uint32_t v) {
    for(unsigned i=0;i<4;++i) p[i]=(uint8_t)(v>>(8*i));
}
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
uint32_t storage_crc32(const uint8_t *p,size_t n) {
    uint32_t crc=0xffffffffu;
    while(n--) {
        crc^=*p++;
        for(unsigned i=0;i<8;++i) crc=(crc>>1)^(0xedb88320u & (0u-(crc&1u)));
    }
    return ~crc;
}
void storage_record_encode(uint8_t r[STORAGE_PAGE_SIZE],const parameter_values *p,uint32_t generation) {
    memset(r,255,STORAGE_PAGE_SIZE);
    put32(r,STORAGE_MAGIC);put32(r+4,STORAGE_VERSION);put32(r+8,PARAM_COUNT);
    put32(r+12,generation);put32(r+16,PARAM_COUNT*4u);
    for(unsigned i=0;i<PARAM_COUNT;++i) put32(r+20+4*i,(uint32_t)p->values[i]);
    put32(r+252,storage_crc32(r,252));
}
bool storage_record_decode(const uint8_t r[STORAGE_PAGE_SIZE],parameter_values *p,uint32_t *generation) {
    uint32_t version=get32(r+4),count=get32(r+8);
    bool legacy=version==1u && count==40u;
    bool schema2=version==2u && count==48u;
    bool schema3=version==3u && count==49u;
    if(get32(r)!=STORAGE_MAGIC || (!legacy && !schema2 && !schema3 && (version!=STORAGE_VERSION || count!=PARAM_COUNT)) ||
       get32(r+16)!=count*4u || get32(r+252)!=storage_crc32(r,252)) return false;
    parameter_values candidate;parameters_defaults(&candidate);
    for(unsigned i=0;i<count;++i) candidate.values[i]=(int32_t)get32(r+20+4*i);
    /* Raw bias actuation was retired. Validate the old field before discarding it. */
    if(legacy) {
        if(candidate.values[PARAM_bias_dac_code]<0 || candidate.values[PARAM_bias_dac_code]>65535) return false;
        candidate.values[PARAM_bias_dac_code]=0;
    }
    if(!parameters_validate(&candidate)) return false;
    *p=candidate;*generation=get32(r+12);return true;
}
static bool erased(const uint8_t *p,unsigned size) {
    for(unsigned i=0;i<size;++i) if(p[i]!=255) return false;
    return true;
}
bool storage_journal_load(parameter_journal *j,storage_io io,parameter_values *p) {
    *j=(parameter_journal){.io=io,.page=-1}; parameters_defaults(p);
    for(unsigned i=0;i<STORAGE_PAGES;++i) {
        parameter_values candidate;uint32_t gen;
        const uint8_t *r=io.data+i*STORAGE_PAGE_SIZE;
        if(!storage_record_decode(r,&candidate,&gen)) {
            if(!erased(r,STORAGE_PAGE_SIZE)) j->corrupt=true;
            continue;
        }
        uint32_t diff=gen-j->generation;
        if(!j->loaded || (diff && diff<0x80000000u)) {
            j->loaded=true;j->generation=gen;j->page=(int)i;*p=candidate;
        }
    }
    return j->loaded;
}
bool storage_journal_dirty(const parameter_journal *j,const parameter_values *p) {
    parameter_values saved;uint32_t gen;
    return !j->loaded || get32(j->io.data+j->page*STORAGE_PAGE_SIZE+4)!=STORAGE_VERSION || !storage_record_decode(j->io.data+j->page*STORAGE_PAGE_SIZE,&saved,&gen) ||
           memcmp(saved.values,p->values,sizeof saved.values)!=0;
}
bool storage_journal_save(parameter_journal *j,const parameter_values *p) {
    if(!parameters_validate(p)) return false;
    if(!storage_journal_dirty(j,p)) return true; /* No wear on identical save. */
    unsigned per=STORAGE_SECTOR_SIZE/STORAGE_PAGE_SIZE;
    unsigned sector=j->loaded ? (unsigned)j->page/per : 0;
    unsigned first=j->loaded ? (unsigned)j->page+1u : 0;
    unsigned page=STORAGE_PAGES;
    for(unsigned i=first;i<(sector+1u)*per;++i)
        if(erased(j->io.data+i*STORAGE_PAGE_SIZE,STORAGE_PAGE_SIZE)) { page=i;break; }
    if(page==STORAGE_PAGES) {
        sector=1u-sector;page=sector*per;
        if(!j->io.erase(j->io.context,sector) ||
           !erased(j->io.data+sector*STORAGE_SECTOR_SIZE,STORAGE_SECTOR_SIZE)) return false;
    }
    uint8_t record[STORAGE_PAGE_SIZE];uint32_t gen=j->generation+1u;
    storage_record_encode(record,p,gen);
    if(!j->io.program(j->io.context,page,record) ||
       memcmp(j->io.data+page*STORAGE_PAGE_SIZE,record,STORAGE_PAGE_SIZE)!=0) return false;
    parameter_values check;uint32_t actual;
    if(!storage_record_decode(j->io.data+page*STORAGE_PAGE_SIZE,&check,&actual) || actual!=gen) return false;
    j->page=(int)page;j->generation=gen;j->loaded=true;return true;
}
