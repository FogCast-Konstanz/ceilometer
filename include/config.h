#pragma once
#include <stdint.h>
#define PRN_LENGTH 1023u
#define CAPTURE_MAX_FRAMES 16u
#define CAPTURE_MAX_SAMPLES (PRN_LENGTH * CAPTURE_MAX_FRAMES + 1u)
#define TX_WORDS PRN_LENGTH /* 32 exact frames, no 1023->1024 padding */
#define CHIP_CYCLES 10u /* Allowed chip-period quantum in clk_sys cycles. */
#define DEFAULT_CHIP_HZ 10000000u
#ifndef SYSTEM_KHZ
#define SYSTEM_KHZ 500000u
#endif
#define PIN_LD_SDA 0u
#define PIN_LD_SCL 1u
#define PIN_RX_SDA 2u
#define PIN_RX_SCL 3u
#define PIN_LD_GATE 4u
#define PIN_SIPM 5u
#define PIN_CHIP_SYNC 6u
#define PIN_FRAME_SYNC 7u
#define PIN_SPI_MISO 8u
#define PIN_SPI_CS 9u
#define PIN_SPI_SCK 10u
#define PIN_SPI_MOSI 11u
#define PIN_TEST_PULSE 12u
#define PIN_TEST_PRN 13u
#define PIN_LD_ERROR 16u
#define PIN_TEC_SWEN 17u /* schematic misleadingly calls this ~TEC_FAULT */
#define PIN_BIAS_ENABLE 18u
#define PIN_ONEWIRE 19u
#define PIN_LD_ADC 26u
#define PIN_TEC_ADC 27u
#define PIN_BIAS_ADC 28u /* LT3571 MON current, NOT divided bias voltage */
#define PWM_COUNTER_SLICE 2u
#define PWM_TIMER_SLICE 3u
#define I2C_LD_ADDRESS 0x4cu
#define I2C_BIAS_ADDRESS 0x4eu /* AD5693R A0 is address bit 1, bit 0 is fixed zero */
#define I2C_THRESHOLD_ADDRESS 0x4eu
#define I2C_TEMPERATURE_ADDRESS 0x48u
