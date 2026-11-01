# Fogcast ceilometer

Fogcast is an experimental laser ceilometer intended to measure atmospheric
backscatter and, eventually, cloud-base height. This repository contains the
instrument electronics and mechanical designs, Pico 2 firmware, board bring-up
interface, and Raspberry Pi acquisition and dashboard software.

The instrument uses pseudo-randomly modulated continuous-wave lidar (PRN-CW).
A laser transmits a repeating binary sequence. A silicon photomultiplier (SiPM)
receiver detects returned photons, and correlation with the transmitted sequence
produces a backscatter profile versus range.

## Project status

The project currently supports board commissioning, receiver characterization,
PRN acquisition, raw-data recording and range-profile display. It is a research
prototype: calibrated atmospheric ranging and automatic cloud-base detection
have not been established. Displayed profiles require timing, optical and
calibration validation before they can be interpreted as qualified measurements.

The firmware currently operates at **500 MHz with a requested 1.5 V core supply**,
above the Pico 2's rated clock. This configuration has been used in bench tests;
stability must be checked on each instrument. See the
[clock and watchdog notes](docs/bringup/operating-clock.md) for evidence and limits.

## Instrument architecture

- **Pico 2 / RP2350:** generates PRN modulation, counts receiver pulses, captures
  counter snapshots through DMA, accumulates histograms, and controls peripherals.
- **Transmitter:** laser-current DAC and driver, plus a TEC regulated from a
  DS18B20 laser-temperature sensor.
- **Receiver:** SiPM bias control, receiver-temperature feedback and an adjustable
  comparator threshold. Bias targets track temperature and are limited to 50 V;
  this software limit is not a measured-voltage hardware clamp.
- **Raspberry Pi:** supervises measurements over USB CDC, records raw histograms,
  calculates correlation profiles and serves the instrument dashboard.

Acquisition runs on one Pico core in finite bursts. A common hardware event
starts transmission and receiver sampling. Startup periods are discarded after
each restart, and sensor servicing occurs between bursts. The nominal one-second
integration refers to accepted exposure; elapsed time is longer because of
warmup and processing gaps. Both are recorded separately.

See [acquisition architecture](docs/architecture/continuous-acquisition.md) and
[USB protocol](interfaces/usb-protocol.md) for the implementation details.

## Explore without hardware

Requirements: Python 3.10 or newer. Run these commands from the repository root:

```sh
python -m venv .venv
# Linux / Raspberry Pi:
. .venv/bin/activate
# Windows PowerShell alternative: .venv\Scripts\Activate.ps1
python -m pip install -e software/instrument
ceilometer-dashboard --demo --http-port 8081
```

Open **http://127.0.0.1:8081/**. Demo mode uses simulated data and does not open
USB or control physical outputs.

If you need the binary CAD, print projects or other large design assets, install
Git LFS and run `git lfs pull` after cloning.

## Connect an instrument

1. Assemble and inspect the hardware against the
   [electronics sources](hardware/electronics/README.md). Review the
   [bring-up documentation](docs/bringup) and [laser safety material](docs/safety)
   before applying power or commissioning optical output.
2. Build and load the Pico firmware as described below. USB powers the Pico;
   the instrument's other circuits require their designed external supplies.
3. Open [software/bringup/index.html](software/bringup/index.html) directly in
   desktop Chrome or Edge. Connect through Web Serial to inspect sensors,
   configure parameters and commission each functional block. Confirm wiring,
   TEC polarity, temperature feedback, DAC limits and board-specific calibration.
4. Disconnect the bring-up GUI before starting the Pi application. Only one
   program should own the instrument's USB connection.
5. Start the instrument dashboard in the Python environment:

```sh
ceilometer-dashboard auto --http-port 8080 --output measurements
```

Open **http://127.0.0.1:8080/**. Automatic discovery selects only USB manufacturer
`Fogcast`, product `Fogcast Ceilometer`, with the expected Pico VID/PID. Other
Picos are ignored; multiple matching ceilometers require an explicit device
path. Firmware 0.6.4 or newer supplies this USB identity.

The application starts **disabled**. Select a measurement mode and press
**Enable instrument** explicitly. **Disable for servicing** requests shutdown
and reports whether the board has acknowledged the laser, bias and TEC off.
External supplies remain powered. Temperature, driver-fault, watchdog and USB
lease interlocks remain active; reconnecting does not resume measurement.

For access from another computer on a trusted network or VPN:

```sh
export CEILOMETER_WEB_TOKEN="$(python -c 'import secrets; print(secrets.token_urlsafe(32))')"
ceilometer-dashboard auto --listen 0.0.0.0 --http-port 8080 --output measurements
```

Open `http://<pi-address>:8080/` and enter the token. Network binding requires a
token of at least 24 characters. The supplied server uses HTTP; use an HTTPS
reverse proxy where transport encryption is required. Keep tokens and local
configuration outside Git.

## Build firmware and run checks

Firmware requirements: Pico SDK **2.3.0**, a compatible Arm GNU toolchain, CMake
and a supported build tool. Set the SDK path for your machine:

```sh
cmake -S firmware/pico2 -B build/firmware -DPICO_BOARD=pico2 -DPICO_SDK_PATH=/path/to/pico-sdk
cmake --build build/firmware
```

The output is `build/firmware/ceilometer.uf2`. To load it manually, hold BOOTSEL
while connecting the Pico and copy the UF2 to its bootloader drive. Select the
intended board carefully when several Picos are connected.

Power-driver build flags default **OFF**. Commissioned hardware requires the
appropriate `CEIL_ENABLE_LASER`, `CEIL_ENABLE_BIAS` and `CEIL_ENABLE_TEC` options,
as well as a calibrated `CEIL_BIAS_MAX_CODE`. Build permission alone does not
turn outputs on. Do not reuse another board's DAC calibration or limits without
verification; see [SiPM bias control](docs/bringup/sipm-bias.md) and
[temperature control](docs/bringup/temperature-control.md).

Portable regression checks require a native C compiler; install the instrument
Python dependencies first. Node.js 22 or newer enables the GUI protocol checks.

```sh
cmake -S firmware/pico2 -B build/host -DCEIL_HOST_TESTS=ON
cmake --build build/host
ctest --test-dir build/host --output-on-failure
python scripts/check_design_sources.py
```

After editing the bring-up GUI, regenerate its direct-open bundle with
`python software/bringup/build_bundle.py`. After changing firmware parameter or
telemetry inventories, first run `python software/bringup/update_metadata.py`.

## Repository guide

| Directory | Contents |
|---|---|
| [firmware/pico2](firmware/pico2) | C/PIO firmware, peripheral drivers and firmware tests |
| [software/bringup](software/bringup) | Direct-open HTML/Web Serial commissioning GUI |
| [software/instrument](software/instrument) | Python package, recorder, processing and Pi dashboard |
| [hardware/electronics](hardware/electronics) | KiCad projects, custom libraries and LTspice simulations |
| [hardware/mechanical](hardware/mechanical) | CAD exchange files, meshes, drawings, print projects and studies |
| [interfaces](interfaces) | USB protocol and recorded-data format |
| [analysis](analysis) | Bench datasets, calibration evidence and plotting scripts |
| [docs](docs) | Architecture, bring-up, operation and safety documentation |
| [deploy](deploy) | Pi service templates, release packaging, updates and rollback |

Live measurements and generated build outputs are not versioned. Small recorded
bench datasets are retained as engineering evidence; see the
[measurement format](interfaces/measurement-format.md) for data interpretation.

## Deployment

After building the firmware, create a checksummed runtime release:

```sh
python deploy/releases/package_release.py --output build/ceilometer-release.tar.gz
```

The archive contains the Pi application, deployment tools, interface documents
and UF2. It excludes CAD, credentials and live measurements. Service templates
are under `deploy/raspberry-pi/`; install/update tools are under `deploy/releases/`.
Automated updates require an explicitly configured Git repository and target
Pico serial number. Updates are applied only while outputs are confirmed off,
and the service restarts disabled. Hardware-only commits do not trigger the
firmware deployment workflow.

### Pi-initiated continuous deployment

GitHub Actions builds and tests a release, then publishes it to the repository's
`deploy` branch. **GitHub never connects to the Raspberry Pi.** A systemd timer
on the Pi initiates an outbound Git fetch every five minutes (with up to 30 s
jitter), starting about two minutes after boot. No inbound port, webhook,
public Pi address or university firewall exception is needed. The Pi needs
outbound HTTPS access to the Git server; configure its proxy if required.

After provisioning the instrument release under `/opt/ceilometer-firmware`,
install and configure the poller on the Pi:

```sh
sudo install -d -m 0750 /etc/ceilometer
sudo install -m 0600 /opt/ceilometer-firmware/deploy/raspberry-pi/cd.env.example /etc/ceilometer/cd.env
sudoedit /etc/ceilometer/cd.env
# Set the HTTPS repository URL, refs/heads/deploy and the target Pico USB serial.
sudo install -m 0644 /opt/ceilometer-firmware/deploy/raspberry-pi/ceilometer-update.service /etc/systemd/system/
sudo install -m 0644 /opt/ceilometer-firmware/deploy/raspberry-pi/ceilometer-update.timer /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now ceilometer-update.timer
```

For a private repository, configure read-only Git credentials on the Pi for the
root-run updater. Do not embed credentials in the URL or commit them. GitHub
Actions needs permission to publish the deployment branch, but no Pi credentials.

```sh
systemctl list-timers ceilometer-update.timer
journalctl -u ceilometer-update.service -n 50
# Trigger an immediate check:
sudo systemctl start ceilometer-update.service
```

While measuring, or when shutdown cannot be confirmed, the updater defers the
update until a later poll. Network outages leave the installed release running;
the next scheduled poll retries. The installer verifies release hashes, targets
the configured Pico, checks the restarted service and attempts rollback on failure.
Neither polling nor an update automatically enables measurements. An initial
local installation is required before polling can update the Pi itself.
