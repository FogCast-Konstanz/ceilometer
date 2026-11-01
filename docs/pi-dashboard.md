# Raspberry Pi instrument dashboard

The dashboard runs acquisition on the Pi, rather than in a browser. It shares
`tools/instrument.py` with `tools/measure.py`; exactly one controller owns USB.
The browser uses HTTP and does not need Web Serial. Closing a browser does not
stop Pi acquisition. **Disable for servicing** is the explicit shutdown control.
All CSS, JavaScript and the existing RDS/WebGL-Plot renderer are served locally.
No internet connection or cloud service is required for operation.

## Operation

The service always starts disabled. It connects to the Pico, sends Stop and
checks acquisition/arming, TEC enable and receiver-bias enable status. Enable
is unavailable until the board acknowledges the disabled state. An explicit
Enable starts one of these modes:

- Laser measurement: check commissioned TEC polarity and laser code/ceiling;
  wait for valid temperature, regulate to within 0.5 C of target for ten seconds,
  enable compensated receiver bias, arm/apply the saved laser DAC and start PRN.
- Receiver reference: receiver bias on, laser off, continuous receiver acquisition.
- Electronics reference: bias and laser off, continuous acquisition.

Disable cancels warm-up as well as measurement. A priority Stop thread competes
only with the current bounded USB operation; every subsequent startup command
checks a cancellation epoch under the USB lock. Stop ACK is followed by explicit
output-status readback. Pending startup work cannot re-arm after Stop.

The dashboard distinguishes requested shutdown, confirmed disabled outputs and
unknown state after lost communication. Browser/Pi disconnection never produces
a green shutdown confirmation. External board supplies remain powered: this
software shutdown is not mains isolation and cannot discharge arbitrary hardware.

Faults disable measurement and require explicit Reset fault while shutdown is
confirmed, followed by a new Enable. Reconnection and service restart do not
resume emission. Pico temperature, voltage-target and driver interlocks remain
in charge; no DAC ceiling is raised and manual bias-test mode is not used.
SIGTERM/Ctrl-C attempt verified shutdown and close USB. The Pico lease remains
the fallback on service crash or loss of USB, not an independently measured
shutdown confirmation.

## Dashboard

- Instrument state and persistent Enable / Disable for servicing controls.
- Laser and receiver temperatures, bias target, profile count rate and exposure.
- Latest exposure-normalized range profile with symlog/linear scaling and JSON download.
- Profile/drop counters, PRN configuration, DMA processing time and error flags.
- All firmware telemetry, filterable with validity/age handling.
- Storage availability and last-profile receipt time.

Range and correlation are still uncalibrated instrument outputs; no cloud-base
or physical-backscatter retrieval is implied. Profile count rate is integrated
counts divided by accepted exposure, not a separate instantaneous counter.
UTC labels refer to Pi receipt time, not a hardware-latched acquisition time.
The browser marks stale status and does not assume an unreachable Pi is safe.

## Raspberry Pi 3B installation

Use Raspberry Pi OS with Python 3.9 or newer. Install distro-built NumPy and
pyserial to avoid compiling NumPy on the Pi's ARM processor:

```sh
sudo apt update
sudo apt install python3 python3-numpy python3-serial
```

From the firmware repository root, copy the runtime files:

```sh
sudo install -d /opt/ceilometer-firmware
sudo cp -a tools pi-dashboard gui /opt/ceilometer-firmware/
ls -l /dev/serial/by-id/
```

For a foreground run on the Pi itself:

```sh
python3 /opt/ceilometer-firmware/tools/dashboard.py /dev/serial/by-id/<pico> --output ~/ceilometer-data
```

Open `http://127.0.0.1:8080` on the Pi. To access from another computer, set a
random access token and explicitly bind the LAN interface:

```sh
export CEILOMETER_WEB_TOKEN="$(openssl rand -hex 32)"
python3 /opt/ceilometer-firmware/tools/dashboard.py /dev/serial/by-id/<pico> --listen 0.0.0.0 --output ~/ceilometer-data
```

Open `http://<pi-address>:8080` and enter that token. The UI keeps it in browser
session storage; the token is never placed in a URL or logs. LAN binding requires
a token of at least 24 characters. Use a trusted LAN/VPN or an HTTPS reverse
proxy for remote access; the supplied HTTP listener is not internet-facing TLS.
Do not run the standalone recorder or the USB bring-up GUI at the same time.
Linux USB is opened exclusively in addition to the service's serialization.

## Start at boot with systemd

The service still boots **disabled** and waits for an operator to Enable.
From the repository root:

```sh
sudo useradd --system --user-group --home-dir /var/lib/ceilometer --groups dialout ceilometer
sudo install -d -m 0750 -o root -g ceilometer /etc/ceilometer
sudo install -m 0640 -o root -g ceilometer deploy/dashboard.env.example /etc/ceilometer/dashboard.env
sudoedit /etc/ceilometer/dashboard.env
sudo install -m 0644 deploy/ceilometer-dashboard.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now ceilometer-dashboard
```

Set the actual serial-by-id path and a random token in dashboard.env before
starting. Replace both example values; do not use the template token. The service
runs without root privileges, with dialout access and writable state only under
`/var/lib/ceilometer`. Profiles are stored in its `measurements/YYYY-MM-DD/`
subdirectories. Review with:

```sh
systemctl status ceilometer-dashboard
journalctl -u ceilometer-dashboard
sudo systemctl stop ceilometer-dashboard
```

The Pi clock should be synchronized for useful UTC receipt timestamps. Recording
continues without internet. Daily folders contain atomic raw/processed JSON
files; bounded retention and remote upload are not implemented here. Disk-write
failure faults and disables the instrument instead of silently discarding data.
The service needs Pico firmware 0.5.0 or later.

## Simulation and tests

```sh
python3 tools/dashboard.py --demo --http-port 8081 --output preview-data
```

Open `http://127.0.0.1:8081`. The header explicitly identifies simulation, no USB
is opened, and all records/downloads are marked simulation. Demo files go under
`preview-data/simulation`. Enable and Disable operate on a simulated Pico only.

Controller/API tests cover startup-disabled behavior, normal measurement,
shutdown confirmation, warm-up cancellation, in-flight enable/disable races,
latched faults, disconnected hardware, missing ACKs, disk failure, access tokens,
cross-origin control rejection and static asset path restrictions. Hardware
response and deployment on a real Pi 3B remain separate qualification steps.

An optional CEILOMETER_INHIBIT_ENABLE reason in dashboard.env locks every
measurement mode off, including reference captures, while keeping status and
Disable available. This maintenance lock survives deployment updates.


### Device swaps and test enable

`CEILOMETER_SERIAL=auto` selects only a device advertising VID 2e8a/PID 0009,
manufacturer `Fogcast` and product `Fogcast Ceilometer` (firmware >=0.6.4).
Other Pico boards are ignored without opening or probing their serial ports.
Exactly one matching ceilometer is required; zero or multiple matches leave USB unavailable;
selection never starts outputs. This supports swapping the bare Pico for the
ceilometer mainboard once both run firmware >=0.6.4. Older generic `Pico` firmware
must first be upgraded using its explicitly identified USB serial number. Deployment
flashing still targets its explicitly configured serial; CD is not auto-retargeted.
An empty `CEILOMETER_INHIBIT_ENABLE` permits explicit dashboard Enable; startup,
reconnects and service restarts remain disabled. Temperature and fault checks
still apply. Firmware 0.6.1 exposes uptime and watchdog boot classification;
the dashboard displays these alongside USB disconnect and detected reboot counts.
