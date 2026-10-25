# Local verification record

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
