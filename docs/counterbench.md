# Bare Pico 2 counter bench, 2026-10-25

Board D5E72DD9B609A5A9, RP2350 A2, USB COM20. GPIO12 -> GPIO5
(physical16 ->7). Main ceilometer board was not flashed.

Standalone target tests/counterbench reuses src/sipm_counter.c and test_pulse.c.
It runs from RAM and boots at150MHz/1.1V. Serial single-character commands:
b=baseline sweep, o=500MHz/1.5V sweep, s=identify/status. o explicitly calls
vreg_disable_voltage_limit, sets VREG_VOLTAGE_1_50, waits20ms, then sets500MHz.
Completed runs stop GP12 pulses and restore150MHz/1.1V. A3s watchdog resets hangs.
Do not run this overclock target on the attached laser mainboard.

Baseline verified rising-edge PWM2 count to75MHz. At500MHz verified to250MHz;
50ms rate measurements differ from generated rate by <=4ppm in observed runs,
consistent with software window-boundary uncertainty. Frequency counter reports
500000..500001kHz against clk_ref. Regulator selector18 verified in software;
actual core voltage was not measured externally.

Ten repeated overclock sweeps plus30 DMA captures completed without resets or
timeouts. At10MHz bin sampling and250MHz input, all10 captures had102375 edges
across4095 intervals, exactly25edges/bin. At10MHz input,4095edges (one/bin).
At1MHz input,409edges vs409.5phase-dependent expectation. Counter wrapping is
handled modulo65536; foreground polling had observed maxgap1us.

DMA bench reproduces PWM0 DREQ -> DMA32bit reads of PWM2 counter to incrementing
RAM, but does not run the production PRN/correlation or concurrent peripheral load.
Both pulse source and counter use clk_sys: this establishes synchronous loopback
consistency, not independently calibrated external pulse timing or asynchronous
input acceptance at250MHz. No long-duration/thermal stability claim.

Existing PRN code uses15cycles/chip and requires exact integer divider and integer
chip_hz.500MHz is not divisible by15, so this target does not claim production
PRN firmware runs unchanged at500MHz.480MHz permits e.g.10MHz with divider48/15
only if the PIO implementation supports that divider; production requires integer
PIO divider, so an exact divisor15*chip_hz must be selected (e.g.8MHz at480MHz).

Build example:
cmake -S tests/counterbench -B build-counterbench -G Ninja -DPICO_SDK_PATH=C:/Users/MarianKeller/.pico-sdk/sdk/2.3.0 -DPICO_TOOLCHAIN_PATH=C:/Users/MarianKeller/.pico-sdk/toolchain/15_2_Rel1
cmake --build build-counterbench

Full measured output: counterbench-results.txt.
Manufacturer operating limits: https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf
500MHz/1.5V is outside rated operation. Default restored after testing.
