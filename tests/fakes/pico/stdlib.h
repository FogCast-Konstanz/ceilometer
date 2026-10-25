#pragma once
#include <stdint.h>
#include <stdbool.h>
typedef unsigned uint;
#define GPIO_OUT true
#define GPIO_IN false
void gpio_init(uint pin);
void gpio_put(uint pin, bool value);
void gpio_set_dir(uint pin, bool output);
bool gpio_get(uint pin);
void busy_wait_us_32(uint32_t us);
uint32_t time_us_32(void);
#define GPIO_FUNC_SIO 0
#define GPIO_IRQ_EDGE_FALL 4u
#define PICO_ERROR_TIMEOUT -1
void gpio_set_function(uint pin,int function);
void gpio_set_irq_enabled_with_callback(uint pin,uint32_t events,bool enable,void (*callback)(uint,uint32_t));
void stdio_init_all(void);
int getchar_timeout_us(uint32_t timeout);
void tight_loop_contents(void);

#define GPIO_FUNC_I2C 1
#define GPIO_FUNC_SPI 2
void gpio_pull_up(uint pin);
void sleep_ms(uint32_t ms);
void sleep_us(uint32_t us);
