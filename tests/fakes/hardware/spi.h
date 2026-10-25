#pragma once
#include <stdint.h>
#include <stddef.h>
typedef struct { int unused; } spi_inst_t;
extern spi_inst_t *spi1;
#define SPI_CPOL_0 0
#define SPI_CPHA_0 0
#define SPI_MSB_FIRST 0
void spi_init(spi_inst_t *,unsigned);
void spi_set_format(spi_inst_t *,unsigned,int,int,int);
int spi_write_read_blocking(spi_inst_t *,const uint8_t *,uint8_t *,size_t);
