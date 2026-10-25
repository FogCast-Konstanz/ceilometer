# Ceilometer board console

Run from the firmware repository:

```powershell
python gui/serve.py
```

Open **http://127.0.0.1:8765** in desktop Chrome or Edge. Choose **Connect USB**
and select the Pico 2's USB CDC port. Close other serial clients first. USB line
coding is 115200 for the browser API; this is not a UART and requires no baud
negotiation. Connect the PC to the Pico's USB connector; J4/J5 are proprietary
receiver links. No installation, build step, account or network connection is
needed. Local serving is required for ES modules; do not open index.html as a file.

**Try demo** uses the same serial client/parser with an in-browser simulated
port. Its banner, firmware version and exported data identify simulation. Demo
provides missing-temperature fault injection and synthetic captures; its values
are not measurements. Metadata comes from the firmware's descriptor lists.
The console targets firmware 0.4.0 or later with the compensated-bias commands.

The Bring-up page provides:

- Firmware identity, compile-time output enables/code limits, fault state,
  flash generation/dirty status and timestamped sensor summaries.
- Laser DS18B20 and receiver MCP9800 feedback; separate TEC and compensated
  SiPM-bias controls; explicit laser arming and DAC application.
- GP12 test-pulse generation and modulo-65536 GP5 counter observation.
- Mainboard and receiver I2C scans (firmware 0.4.1+), including idle line levels,
  timeout/NACK counts and responding addresses. Scanning requires outputs off.
- Dark captures with GP4 low / diagnostic PRN on GP13, optional commissioned
  emitting captures, 1023-bin histogram and correlation readback, CSV export.
- Peripheral readback health for both temperature sensors, three DACs and TEC SPI.

All 108 telemetry fields are discovered from the connected firmware, including
validity, device sample age and sequence. Invalid/missing readings remain visibly
invalid, not zeroed into plausible measurements. Plot up to four compatible-unit
signals; changing to a different unit replaces the selection. Temperature displays
in Celsius and microvolt signals display in volts. Raw exact integers remain in
the table and exports. Pause freezes the plot only; polling/USB activity continue.
Choose 10/30/60/120-second windows, remove traces in the legend or clear history.
Selected-channel history retains about 125 seconds / 2400 points per channel;
CSV retains the latest 20000 received telemetry records, including invalid samples.

The renderer reuses the RDS GUI's locally bundled WebGL-Plot **1.1.2**, fixed
Float32Array buffers, WebGL2 lines, 20 FPS ceiling and expanding/gradually
contracting autoscale. The MIT license is preserved under vendor/webgl-plot.
Charts use approximate JavaScript numbers; 64-bit telemetry/capture integers are
preserved as strings in tables and CSV. Invalid points leave gaps. Capture range
in CSV uses the configured chip rate and offset; physical timing remains unqualified.

Parameters discovers all 48 descriptors (bounds, units, defaults, current values).
Stage up to eight changes within the firmware's 127-character command limit and
apply atomically. Larger edits must be divided deliberately so each candidate
passes firmware cross-validation. Save explicitly writes flash; Load and RAM
defaults do not enable outputs. Editing is disabled while outputs/acquisition
are active. The retired raw bias-DAC parameter cannot be changed. Export JSON
records the current accepted RAM configuration, not pending edits.

The console records up to 500 exact TX/RX/error lines. JSON diagnostic reports
contain identity, build limits, configuration, last status/telemetry and the log.
Browser/USB freshness is separate from the firmware's device-valid flag; the
table reports both age and validity at its last read. Polling rotates through all
fields, prioritizing plotted signals and bring-up feedback. This is request/reply
monitoring, not synchronous high-rate waveform acquisition. Full board sweeps
continue only in firmware acquisition dead time.

Connection and failure behavior:

- Identify the board before sending power commands. Successful connection sends
  Stop and disables the diagnostic pulse before reading the inventory.
- Serialize all requests; ACK/NACK names must match the pending command. USB
  timeout, stream loss or a mismatched response closes the session. A late ACK
  cannot satisfy a later command. Ordinary firmware NACKs remain visible and
  keep the synchronized connection open.
- Stop all cancels queued requests, waits for the current response, sends Stop,
  then disables GP12. It cancels any longer inventory/capture workflow. It cannot
  preempt an already transmitted request. Firmware interlocks remain authoritative.
- Disconnect sends Stop before closing. Hiding the tab requests Stop; page unload
  makes a best-effort stop. Browser timers/unload handlers are not reliable safety
  mechanisms: the firmware USB activity lease/watchdog and independent hardware
  protection are still required. No automatic reconnect, rearm or output restart.
- Power controls require the corresponding commissioned firmware build. The GUI
  does not bypass compile-time ceilings, DS18B20 feedback, temperature windows,
  PID ownership or compensated-bias limits. SiPM voltage is a calculated estimate;
  there is no physical bias-voltage ADC or independent 50 V clamp on this PCB.

Native JavaScript tests (Node 22+):

```powershell
node --test gui/tests/*.test.mjs
```

The CMake host-test configuration also registers these tests when Node is found.
After descriptor changes, run `python gui/update_metadata.py` to refresh demo
metadata; the test suite checks it against the C headers.

No real board, hardware interlock or physical voltage was exercised by these tests.
Web Serial follows the [Chrome documentation](https://developer.chrome.com/docs/capabilities/serial).
