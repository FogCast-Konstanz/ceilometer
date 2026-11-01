# Adjustable settings and flash storage

The inventory and implementation plan is in parameters-and-telemetry-plan.md.
The 48 descriptors are the firmware's single source of names, units, bounds and
defaults. Every descriptor has USB metadata/get/set access and is included in
flash serialization. Integer fixed-point units keep parsing and saved data
unambiguous; for example PID P=100.25 mV/C is `pid_p_milli=100250`.

Use `$set_param,name,value` or `$set_params,name,value,name,value...` to build a
candidate configuration. The entire candidate is validated before application;
up to eight pairs fit if the whole request stays within 127 characters. Use an
atomic update when changing both a window and its target. The temperature target
must remain inside the laser window, which remains inside the TEC protection
window. Default laser bounds are 10000/30000 mC and default target is 24000 mC.
Setters, save, load and defaults require acquisition, laser arm, TEC and bias off.
These guards preserve the prior fault latch. No configuration update clears a
fault, starts a capture or enables an output. ACK means the configuration was
accepted; consult telemetry validity/readback for application to a missing device.
Threshold application is retried on receiver reconnect, and MCP9800 resolution is
applied during receiver/bias service. Flash defaults/load/save work with peripherals
absent. Old telemetry is invalidated when configuration/calibration changes.

`save` explicitly commits RAM values to flash; `load` reloads the latest valid
record; `defaults` restores RAM defaults without writing flash. Save after
defaults to persist a factory reset. A reboot loads saved settings or safe
defaults if none are valid. A configured nonzero laser target stays inert:
after boot/load you must explicitly arm/apply the laser DAC. SiPM bias uses
temperature compensation; `bias_enable,1` explicitly starts it with valid receiver
feedback. Raw nonzero bias DAC actuation is rejected. `laser_dac` without an
argument applies its configured target subject to commissioning/interlocks. `capture` / `start` without
an argument use `integration_frames`. `set_threshold`, `set_rate`, `tec_target`,
`tec_polarity`, `tec_pid` and successful DAC target commands update the same RAM
inventory; they do not auto-save. Emergency shutdown zeros actual DAC outputs
without overwriting the configured targets. `get_param` reports configured values;
telemetry DAC requested/readback fields report actual driver state/readback.

All settings changes invalidate the previous acquisition result. Range timing
offset and exported averaging apply to a new capture; `range_point,index` returns
the first native bin, calibrated-axis distance in mm and averaged correlation
for each group. Native `raw`, `histogram`, and `profile` retrieval stay available.
No optical-overlap correction or automatic cloud classification is implied.

Storage uses the physical Pico 2 flash's last 8 KiB (offset 4186112), explicitly
excluded from the firmware linker region using pico_override_flash_size. Each
256-byte record contains magic, schema version, field count, generation, payload
length, explicit little-endian int32 values, padding and IEEE CRC32. CRC covers
the header and payload/padding, so generation/size corruption cannot pick a bad
record. Invalid schema, CRC or parameter constraints cause that record to be
ignored. `get_storage` reports loaded, dirty, generation, page, invalid-record
indicator and schema version. Schema 2 migrates schema 1 (40 fields), discarding the retired raw bias target
and defaulting the eight new compensation fields. It reports dirty until an
explicit save writes schema 2. Unknown future schemas are ignored rather than
interpreted as this layout. No automatic destructive reformat happens on boot.

Append pages within the active 4 KiB sector. Once full, erase the *other* sector
and write there, retaining the previous valid sector throughout. Verify written
bytes and decoded parameters before changing journal state. Equal-value saves do
not program or erase flash. Torn writes are skipped on recovery; generation
comparison handles unsigned wrap. A failed save leaves the previous record
usable; after a power cut, either the complete new record or the prior record is
selected. All power outputs/acquisition must be stopped before a save, diagnostic
pulses are stopped, flash helpers disable interrupts safely and the watchdog is
serviced before/after. No flash save occurs during DMA or on every PID iteration.

The firmware currently runs on core 0 only; PICO_FLASH_ASSUME_CORE1_SAFE is set
because core 1 never starts. Any future multicore use must replace this assumption
with the SDK's coordinated flash-safe execution setup. BOOTSEL/UF2 updates that
only program firmware sectors preserve settings; a whole-chip erase removes them.
Bench-test actual flash power-cut recovery and USB behavior on the hardware.

Examples in one USB session:

```text
$stop
$set_params,laser_min_mc,8000,laser_max_mc,35000,tec_target_mc,25000
$set_param,pid_p_milli,100250
$get_param,laser_min_mc
$save
$get_storage
```

CLI equivalents:

```powershell
python -m fogcast.ceilometer COM7 parameters
python -m fogcast.ceilometer COM7 set laser_min_mc=8000 laser_max_mc=35000 tec_target_mc=25000 --save
python -m fogcast.ceilometer COM7 telemetry
```

The CLI stops outputs before closing its session. Persisted polarity remains a
commissioning choice and the TEC/laser build options and code ceilings remain
independent of user settings. There is no persistent enable or automatic rearm.

## Flash-save fix verified on Pico 2

The application linker region is limited to4186112 bytes, while the SDK retains
physical PICO_FLASH_SIZE_BYTES=4194304. Using pico_override_flash_size for the
reservation incorrectly reduced both: SDK flash_range_program/erase hard_assert
rejected the journal addresses and the watchdog reset the halted firmware.
CMake now overrides only pico_flash_region.ld; the backend statically checks
physical capacity, and its existing bounds restrict journal operations to8KiB.

On-board checks: Save ACK, clean generation1, repeated unchanged Save retains
same generation; restart restores laser_dac_code32505, tec_polarity1 and nominal
monitor scales181/998 with all outputs off. Alternating telemetry_period_ms100/101
exercised programming and sector rollover through generation17/page16; original
100ms restored. Seven host test suites pass. No power outputs enabled by testing.

Schema3 appends laser_max_code (0..65535, default32505, zero disables laser arming).
It is adjustable through set_param/set_params while all outputs/acquisition are off
and persisted by Save. Schema2's48fields are retained, including bias calibration;
the new ceiling defaults32505. Schema1's40fields still migrate with retired raw
bias discarded. Saved targets never enable outputs. A stored laser_dac_code above
the ceiling may remain as a target, but applying it is rejected. The GUI laser
DAC maximum follows the parameter after Apply/Load/Defaults/Refresh.

Schema4 appends prn_length, measurement_period_ms and startup_discard_frames
(52 parameters total). Schema3/49-field records migrate preserving every old
field; new defaults are 1023 chips, 1000 ms, two discarded periods.

Firmware 0.6.0 uses finite bursts. startup_discard_frames must leave one accepted
period in the DMA buffer: 1..15 at 1023 chips, 1..3 at 4095 chips. The schema and
saved calibrations are unchanged; larger stored values reject acquisition start.
measurement_period_ms requests accepted exposure, so elapsed time can be longer.
