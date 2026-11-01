#include "parameter_storage.h"
#include <string.h>
static uint8_t flash[STORAGE_SIZE];
static parameter_journal journal;
static bool initialized;
static bool erase(void *ctx,unsigned sector) { (void)ctx;memset(flash+sector*STORAGE_SECTOR_SIZE,255,STORAGE_SECTOR_SIZE);return true; }
static bool program(void *ctx,unsigned page,const uint8_t *p) { (void)ctx;memcpy(flash+page*STORAGE_PAGE_SIZE,p,STORAGE_PAGE_SIZE);return true; }
void parameter_storage_init(void) {
    if(!initialized) { memset(flash,255,sizeof flash);initialized=true; }
    storage_io io={.data=flash,.erase=erase,.program=program};
    (void)storage_journal_load(&journal,io,&parameters_current);
}
bool parameter_storage_load(parameter_values *p) { return storage_journal_load(&journal,journal.io,p); }
bool parameter_storage_save(const parameter_values *p) { return storage_journal_save(&journal,p); }
bool parameter_storage_dirty(const parameter_values *p) { return storage_journal_dirty(&journal,p); }
const parameter_journal *parameter_storage_state(void) { return &journal; }
