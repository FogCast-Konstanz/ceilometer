#include "parameter_storage.h"
#include "hardware/flash.h"
#include "hardware/clocks.h"
#include "hardware/structs/qmi.h"
#include "pico/flash.h"
#include "pico/stdlib.h"
#include "hardware/regs/addressmap.h"
/* Pico 2 physical flash is 4 MiB. The linker limit is reduced by STORAGE_SIZE. */
#define STORAGE_OFFSET (4u*1024u*1024u-STORAGE_SIZE)
_Static_assert(FLASH_PAGE_SIZE==STORAGE_PAGE_SIZE && FLASH_SECTOR_SIZE==STORAGE_SECTOR_SIZE,"Flash geometry mismatch");
_Static_assert(PICO_FLASH_SIZE_BYTES==STORAGE_OFFSET+STORAGE_SIZE,"Physical flash capacity mismatch");
static parameter_journal journal;
typedef struct { unsigned offset;const uint8_t *data;bool erase; } flash_operation;
static void __not_in_flash_func(execute_flash)(void *arg) {
    flash_operation *op=arg;
    if(op->erase) flash_range_erase(op->offset,FLASH_SECTOR_SIZE);
    else flash_range_program(op->offset,op->data,FLASH_PAGE_SIZE);
}
/* Flash commands run at the rated CPU clock; caller requires all outputs idle.
   The 1.5V regulator stays unchanged, and the operating clock is restored. */
static bool run_flash(flash_operation *op) {
    uint32_t khz=clock_get_hz(clk_sys)/1000u;
    if(khz>150000u) set_sys_clock_khz(150000,true);
    bool ok=flash_safe_execute(execute_flash,op,100)==PICO_OK;
    hw_write_masked(&qmi_hw->m[0].timing,10u << QMI_M0_TIMING_CLKDIV_LSB,QMI_M0_TIMING_CLKDIV_BITS);
    if(khz>150000u) set_sys_clock_khz(khz,true);
    return ok;
}
static bool erase_sector(void *ctx,unsigned sector) {
    (void)ctx;if(sector>1) return false;
    flash_operation op={.offset=STORAGE_OFFSET+sector*STORAGE_SECTOR_SIZE,.erase=true};
    return run_flash(&op);
}
static bool program_page(void *ctx,unsigned page,const uint8_t data[STORAGE_PAGE_SIZE]) {
    (void)ctx;if(page>=STORAGE_PAGES) return false;
    flash_operation op={.offset=STORAGE_OFFSET+page*STORAGE_PAGE_SIZE,.data=data};
    return run_flash(&op);
}
void parameter_storage_init(void) {
    storage_io io={.data=(const uint8_t *)(XIP_NOCACHE_NOALLOC_BASE+STORAGE_OFFSET),.erase=erase_sector,.program=program_page};
    (void)storage_journal_load(&journal,io,&parameters_current);
}
bool parameter_storage_load(parameter_values *p) { return storage_journal_load(&journal,journal.io,p); }
bool parameter_storage_save(const parameter_values *p) { return storage_journal_save(&journal,p); }
bool parameter_storage_dirty(const parameter_values *p) { return storage_journal_dirty(&journal,p); }
const parameter_journal *parameter_storage_state(void) { return &journal; }
