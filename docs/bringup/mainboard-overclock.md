# Mainboard overclock diagnostic, 2026-10-26

Pico2 2ED26056C38A563E, COM19. Diagnostic built with
CEIL_OVERCLOCK_BENCH=ON, all three power-enable options OFF and DAC ceilings0.
This RAM-executed build raises core voltage to1.5V before selecting500MHz.
Normal firmware is restored after the test; saved flash calibration preserved.

Hardware frequency counter:500000kHz. Regulator selector18 (1.5V requested;
not externally measured).457 successful replies including90 polling rounds
spaced500ms apart (roughly46s of monitoring, following startup/initial checks).
No observed reboot, timeout or NACK in the monitored sequence.
DS18B20 valid after startup conversion; receiver temperature valid; both I2C
scans found expected addresses (main4c/4e, receiver48/4e), DAC readbacks valid0,
LT8722 SPI register0 repeatedly565780. MCU nominal temperature61.865..62.717C.
Power outputs remained disabled throughout. No receiver dark-count, external
loopback, loaded TEC/laser/bias, production PRN or flash-execution qualification.

A software reboot retained the high voltage selector; normal startup now explicitly
sets150MHz before restoringVREG_VOLTAGE_1_10. After final restoration hardware
get_clock returned150000000,150000,11, stateIDLE with outputs off. Calibration
989241ppm/-4925156uV remains saved. Seven host suites passed.

get_clock protocol returns sys_hz,frequency_counter_khz,vreg_selector.
Test raw replies: mainboard-overclock-results.txt.
This overclock is outside rated operation and is not a long-term stability claim.
