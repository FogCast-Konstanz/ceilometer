#pragma once
#include "parameters.h"
#include <stddef.h>
#define STORAGE_PAGE_SIZE 256u
#define STORAGE_SECTOR_SIZE 4096u
#define STORAGE_SIZE (2u*STORAGE_SECTOR_SIZE)
#define STORAGE_PAGES (STORAGE_SIZE/STORAGE_PAGE_SIZE)
typedef struct {
    void *context;
    const uint8_t *data;
    bool (*erase)(void *context,unsigned sector);
    bool (*program)(void *context,unsigned page,const uint8_t data[STORAGE_PAGE_SIZE]);
} storage_io;
typedef struct {
    storage_io io;
    uint32_t generation;
    int page;
    bool loaded,corrupt;
} parameter_journal;
uint32_t storage_crc32(const uint8_t *data,size_t n);
void storage_record_encode(uint8_t record[STORAGE_PAGE_SIZE],const parameter_values *p,uint32_t generation);
bool storage_record_decode(const uint8_t record[STORAGE_PAGE_SIZE],parameter_values *p,uint32_t *generation);
bool storage_journal_load(parameter_journal *j,storage_io io,parameter_values *p);
bool storage_journal_save(parameter_journal *j,const parameter_values *p);
bool storage_journal_dirty(const parameter_journal *j,const parameter_values *p);
/* Pico backend; commands only call save/reload with acquisition and outputs off. */
void parameter_storage_init(void);
bool parameter_storage_load(parameter_values *p);
bool parameter_storage_save(const parameter_values *p);
bool parameter_storage_dirty(const parameter_values *p);
const parameter_journal *parameter_storage_state(void);
