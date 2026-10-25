# Pico 2 ceilometer firmware

Pico SDK C firmware for the RP2350 PRN-CW lidar mainboard. This is an incremental
peripheral-validation and finite-burst acquisition implementation. It compiles to
a USB CDC controlled Pico 2 image; atmospheric accuracy and 10 MHz DMA timing
have **not** been qualified on hardware.

Implemented: continuous GP5/PWM2 rising-edge counting, PWM0-paced 32-bit DMA
snapshots, exact 1023-chip PIO PRN at 10 Mchip/s, GP6 chip and GP7 frame strobes,
finite 1-16-frame bursts, integration across bursts, modulo differences, 64-bit
DC-rejected correlation, paged raw/histogram/profile retrieval, test pulses,
AD5693R DACs, MCP9800, externally powered single-drop DS18B20, and LT8722 SPI
control with CRC/ACK checks and soft start. UART stdio is disabled.

The schematics override the brainstorm pin suggestions. Read
[hardware.md](docs/hardware.md), [protocol.md](docs/protocol.md), and
[validation.md](docs/validation.md) before bring-up.

## Build

Install Pico SDK >=2.2 with TinyUSB, Arm GNU toolchain, CMake, and Ninja. Tested
locally with SDK 2.3.0 and Arm GNU 15.2.1, targeting `pico2` / `rp2350-arm-s`.

```powershell
$env:PICO_SDK_PATH = 'C:/Users/MarianKeller/.pico-sdk/sdk/2.3.0'
cmake -S . -B build -G Ninja -DPICO_BOARD=pico2 -DPICO_TOOLCHAIN_PATH=C:/Users/MarianKeller/.pico-sdk/toolchain/15_2_Rel1
cmake --build build
```

If CMake cannot locate picotool, pass
`-Dpicotool_DIR=C:/Users/MarianKeller/.pico-sdk/picotool/2.3.0/picotool`.
Load `build/ceilometer.uf2` using BOOTSEL. No board was flashed during development.

Default builds keep laser, bias, and TEC power control disabled. After electrical
qualification, opt in separately using `-DCEIL_ENABLE_LASER=ON`,
`-DCEIL_ENABLE_BIAS=ON`, or `-DCEIL_ENABLE_TEC=ON`. Laser and bias also require
nonzero, **bench calibrated** `CEIL_LASER_MAX_CODE` and `CEIL_BIAS_MAX_CODE` values
(0-65535). They are raw DAC ceilings, not current or voltage calibrations. Never
use the ADC's nominal INA293 gain as a validated laser-current measurement.

## First connection

Use the Pico 2 board's actual USB connector. The mainboard's proprietary J4/J5
USB-C shaped receiver link is not a PC USB port.

```powershell
python -m pip install -r tools/requirements.txt
python tools/ceilometer.py COM7 command get_info
python tools/ceilometer.py COM7 capture --frames 16 --output profile.csv
```

`capture` keeps GP4 low, emits diagnostic PRN data on GP13, and exercises the digital receiver path. To measure the
counter without an attached receiver, use a correctly isolated GP12-to-GP5 bench
loopback and `python tools/benchmark.py COM7`. The client closes each CLI session
with `stop`. For a commissioned emitting session, use the `Ceilometer` class in
one persistent process, wait for valid temperature, arm, set the calibrated laser
DAC, start, and poll state at least every second; always stop in `finally`.

## Portable tests

```powershell
cmake -S . -B build-host -G Ninja -DCEIL_HOST_TESTS=ON
cmake --build build-host
ctest --test-dir build-host --output-on-failure
```

Tests check the full PRN state cycle, all autocorrelation lags, packing across 32
frames, counter wrap, histogram overflow rejection, delayed return polarity,
background rejection, range scale, parser framing/recovery/numeric bounds, and
LT8722 packet CRC/ACK handling. They do not substitute for board measurements.

## Current engineering limits

PIO SMs share a synchronized start. PIO and PWM use **separate ordered software
writes**, with interrupts masked, so their relative phase is unqualified. DMA
reads the live PWM counter after arbitration; it does not latch at the bin edge.
Profiles and range are provisional until phase and latency jitter are measured.
Integration restarts every <=16 frames, with dead time for processing and sensor
service. It is not continuous or a calibrated wall-clock integration period.

Runtime chip rates must divide 150 MHz with an integer 15-cycle PIO divider;
10, 5, 2, and 1 MHz are supported examples. Firmware stays at rated 150 MHz.
500 MHz and 20 MHz bins are not implemented. No automatic cloud classification,
optical overlap correction, temperature PID, flash persistence, or dual-core
streaming is claimed. See validation.md for the required next measurements.

The code drives LD_SWITCH low during firmware shutdown/error handling and uses a
watchdog, driver-error IRQ, temperature checks, and a two-second USB command
lease. Reset/ROM boot and faults before firmware initialization need a hardware
pull-down and independent laser-disable mechanism: none is demonstrated by the
schematic. A stalled PIO can hold its previous pin level until the fault handler
stops it. Do not enable optical output before those hardware protections exist.
