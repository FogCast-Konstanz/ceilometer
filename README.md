# Fogcast ceilometer

Monorepo for the PRN-CW laser ceilometer: RP2350 Pico 2 firmware, direct-open
bring-up GUI, Raspberry Pi instrument software, electronics and mechanical design.

| Directory | Contents |
|---|---|
| [firmware/pico2](firmware/pico2) | C/PIO firmware and portable firmware tests |
| [software/bringup](software/bringup) | HTML/Web Serial diagnostics GUI, works from file:// |
| [software/instrument](software/instrument) | Python `fogcast` package, recorder, processing and Pi dashboard |
| [hardware/electronics](hardware/electronics) | KiCad mainboard, project-local libraries, LTspice simulations |
| [hardware/mechanical](hardware/mechanical) | CAD exchange/mesh files, drawings, print projects and studies |
| [interfaces](interfaces) | USB protocol and recorded measurement format |
| [analysis](analysis) | Bench evidence, calibration and plotting scripts |
| [docs](docs) | Architecture, bring-up, operation and laser safety |
| [deploy](deploy) | Pi services and checksummed release/update tooling |

## Get started

Use Git LFS for the binary CAD, office and video sources (`git lfs pull` after
cloning). The monorepo preserves firmware history and the `pre-monorepo-0.6.4` tag.
The original firmware checkout and OneDrive design folder were left intact.

Open `software/bringup/index.html` in desktop Chrome/Edge for USB bring-up.
After editing the GUI, run `python software/bringup/build_bundle.py`.
Regenerate parameter/telemetry metadata with `python software/bringup/update_metadata.py`.

Install instrument software in a Python environment:

```sh
python -m pip install -e software/instrument
ceilometer-dashboard --demo --http-port 8081
# USB instrument (starts disabled):
ceilometer-dashboard auto --listen 127.0.0.1 --http-port 8080
```

`auto` selects only USB manufacturer Fogcast/product Fogcast Ceilometer; other
Picos are ignored and multiple matching ceilometers are rejected. Use a network
access token for LAN binding. See [Pi operation](docs/operation/pi-dashboard.md).

## Build and test

```sh
cmake -S firmware/pico2 -B build/host -DCEIL_HOST_TESTS=ON
cmake --build build/host
ctest --test-dir build/host --output-on-failure

cmake -S firmware/pico2 -B build/firmware -DPICO_BOARD=pico2 -DPICO_SDK_PATH=/path/to/pico-sdk
cmake --build build/firmware
```

Pico SDK 2.3.0 and an Arm toolchain are required for the Pico build. Power-driver
build flags default OFF. The commissioned production configuration uses
`-DCEIL_ENABLE_LASER=ON -DCEIL_ENABLE_BIAS=ON -DCEIL_ENABLE_TEC=ON -DCEIL_BIAS_MAX_CODE=60210`.
Firmware remains 0.6.4, 500 MHz/requested 1.5 V, with the finite-burst architecture,
per-burst warmup exclusion, watchdog and temperature/USB interlocks. Changing the
repository layout does not qualify acquisition timing or optical performance.

## Releases

```sh
python deploy/releases/package_release.py --output build/ceilometer-release.tar.gz
```

The package includes the instrument application, deployment tools, interface
documentation and built UF2. CAD, live measurements, local configuration and
firmware source/build directories are excluded. Firmware/instrument/deployment
changes can publish a deployment; hardware-only edits cannot trigger flashing.
The Pi applies updates only when disabled and confirmed safe. The running Pi
installation was not modified by this repository migration.

See [migration record](docs/migration/README.md), [USB protocol](interfaces/usb-protocol.md),
[acquisition architecture](docs/architecture/continuous-acquisition.md), and
[clock diagnostics](docs/bringup/operating-clock.md).
