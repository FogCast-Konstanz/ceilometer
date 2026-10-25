# Local verification record

The parameter/telemetry update adds validated persistent settings, a reserved
8 KiB CRC journal, and a complete telemetry inventory. See parameter-storage.md
and telemetry-inventory.md. Native persistence fault injection and simulated
bus tests accompany default/commissioned Pico 2 builds; hardware is untested.

The initial acquisition build record below is historical. The PID/interlock
update also passes four native suites (core, thermal, scripted sensor driver,
and real command/interlock wiring with fake hardware), plus default and
TEC/laser-enabled Pico 2 cross-builds. See temperature-control.md. No hardware tests were run.

Date: 2026-10-25 (Europe/Berlin).

- Fresh schematic netlist export: KiCad 10.0.6, all five child sheets. Export
  completed; a registry-key access warning was logged. Source hashes are in
  schematic-sha256.txt.
- Pico 2 default build: Pico SDK 2.3.0, Arm GNU 15.2.1, CMake 4.3.4, Ninja 1.13.2,
  `rp2350-arm-s`, Release, 150 MHz. PIO programs assembled and ELF/UF2 generated.
- Commissioning branches: separately compiled with laser, bias, and TEC options
  enabled, using dummy DAC ceilings of 1 for compile coverage only. Those values
  are not calibration data. The delivered build/ceilometer.uf2 uses disabled
  default output options.
- Default image size reported by arm-none-eabi-size: text 48644 bytes, initialized
  data 0 bytes, BSS 89736 bytes. Binary/tool metadata and UF2 transport add overhead.
- Native algorithm/protocol/codec tests: GNU 13.2.0, all checks passed under CTest.
- Python host and benchmark tools: Python 3.14 syntax compilation passed.
- No board was flashed, no hardware was connected, and no optical measurement or
  physical DMA timing qualification was performed.

Build artifacts and logs are ignored by Git. Reproduce the checks using README.md;
this record does not certify electrical performance.


Current parameter/telemetry validation: six native suites pass (core, thermal,
sensor-driver, real command/interlock integration, flash journal fault injection,
and actual ADC/DAC/LT8722 drivers against simulated buses). Both default and
TEC/laser-enabled SDK 2.3.0 builds succeed with compiler warnings as errors.
The generated linker FLASH region is 4186112 bytes, leaving the final 8192 bytes
for parameters. Python host tools pass syntax compilation. Flash interruption
tests are simulated; no physical flash write, reboot or instrument measurement
was performed on connected hardware.


SiPM bias compensation validation (firmware 0.4.0, schema 2): all six native
suites pass with warnings as errors. Tests cover every millidegree -40..85 C,
nominal 46.5 V at 25 C, 70 mV/C slope, the inclusive 50 V boundary and shutdown
above it, adjustable overvoltage/breakdown/ceiling, conversion warmup, bad sensor
configuration, missing/stale feedback during DMA, clock wrap, DAC readback
failure, raw-command bypass rejection, cached telemetry and schema-1 migration.
Default and laser/TEC/bias-enabled Pico 2 cross-builds generate ELF and UF2.
The separate build-enabled configuration uses dummy laser ceiling 1 and bias
ceiling 60000 for compile coverage; neither is commissioning calibration.
The default build retains all output enables OFF and both code ceilings zero.
Current default ELF size: text 71296 bytes, data 0, BSS 92840 bytes. No hardware was flashed or physically measured. The
software target ceiling does not qualify a physical 50 V ceiling; see sipm-bias.md.
