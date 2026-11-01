# USB CDC protocol v0.1

ASCII requests: `$command[,argument]\n`. CR and CRLF also work; CRLF dispatches
once. Replies: `$ACK,command[,fields]\r\n` or `$NACK,command,reason\r\n`.
No unsolicited messages, checksum, address, or sequence number. Send one request,
wait for its complete LF-terminated reply, then send the next. USB line coding
baud does not control wire speed. UART transport is disabled.

The receive limit is 127 characters, excluding terminator. Oversize, non-ASCII,
and control-character lines are discarded through the next terminator. Lines
without `$` are ignored. Decimal unsigned parameters reject signs, whitespace,
trailing garbage and overflow. Only `tec_polarity` permits the literal `-1`.

Every syntactically dispatched request renews a two-second activity lease. While
acquiring or any power output is enabled, a closed CDC connection or expired lease
stops all outputs and latches FAULT. Poll `get_state` or `heartbeat` every <=1 s.
Replies should be consumed promptly; timeout/disconnect requires a new session,
fault inspection, explicit clearing and rearming. The one-second watchdog is an
additional recovery mechanism, not an independent physical laser interlock.

| Request | ACK fields / effect |
|---|---|
| `get_info` | name, version, board, clk_sys_hz, chip_hz, 1023, laser_build, bias_build, tec_build |
| `get_state` | state, accumulated_frames, remaining_frames, last_burst_elapsed_us, tx_starved, armed, bias_on, correlation_us |
| `heartbeat` | no fields; renew activity lease |
| `stop` | stop acquisition, clear result validity, disarm, DAC zero, bias and TEC off |
| `clear_fault` | GP16 must be high; thermal faults also require valid feedback inside the configured TEC window; no automatic restart |
| `get_count` | current free-running unsigned 16-bit PWM2 count; modulo 65536 |
| `set_rate,hz` | 10000..10000000, exact sys/(10*integer); invalidate prior profile |
| `test_pulse,hz` | GP12 test output; ACK returns quantized actual integer Hz; 0 disables |
| `capture,frames` | 1..1000000 complete PRN frames, GP4 remains low; PRN data on GP13 |
| `start,frames` | same integration, with laser modulation; requires commissioned build, arm, nonzero laser DAC, valid temperature |
| `raw,offset,count` | offset followed by 1..16 raw low-16-bit snapshots from **last burst only** |
| `histogram,offset,count` | offset followed by 1..16 unsigned 64-bit integrated photon-bin counts |
| `profile,offset,count` | offset followed by 1..16 signed 64-bit DC-rejected correlation values |
| `sensors` | adc0_raw, adc1_raw, adc2_raw, laser_temp_valid, laser_mC, receiver_temp_valid, receiver_mC |
| `set_threshold,code` | raw 16-bit U16 DAC code; does not enable detector bias |
| `arm,0_or_1` | 1 allows laser setup; 0 shuts down all power outputs |
| `laser_dac,code` | 0..laser_max_code parameter; nonzero requires arm |
| `bias_dac,code` | only 0 accepted (disable); nonzero rejected, compensation owns output |
| `bias_enable,0_or_1` | start/stop temperature-compensated bias; 1 requires commissioned build, valid receiver temperature and target <=50 V |
| `get_bias` | enabled, fault, reason, valid, receiver_mC, age_us, breakdown_est_uV, target_uV, output_est_uV, target_code; see sipm-bias.md |
| `tec_read,register` | register (0..7), 32-bit unsigned value; no unrestricted write command |
| `tec_enable,0_or_1` | commissioned soft start / disable; failures latch FAULT |
| `tec_target,milliCelsius` | inside configured laser window, default 24000; reset PID state |
| `tec_polarity,1_or_-1` | set verified heating polarity while TEC and laser arming are off |
| `tec_pid,p,i,d` | integer gains 0..10000 in mV/C, mV/(C*s), mV*s/C; outputs off |
| `get_temperature_control` | TEC enabled, target_mC, valid, measured_mC, output_mV, polarity, thermal_fault |
| `tec_mv,...` | rejected: PID_OWNS_OUTPUT |

States: IDLE, ACQUIRING, READY, FAULT. During ACQUIRING only `stop`, `get_info`,
`get_state`, `heartbeat`, `get_count`, `get_bias`, and `get_temperature_control` are accepted.
The cached parameter and telemetry getters listed below also remain available. BUSY protects shared
buffers and avoids I2C/SPI/one-wire traffic during a burst. Sensor service and TEC
health checks occur between bursts; freshness/range checks also run during bursts.
Laser arming and emission require valid feedback inside the configured laser
window (default 10..30 C inclusive). TEC PID can recover temperature inside its
configured wider protection envelope (default -10..50 C).
Missing/stale feedback disables TEC and laser, resets PID and latches a fault.
See [temperature-control.md](temperature-control.md) for configuration and recovery.
Wait several seconds after boot before arming. Only one externally powered
DS18B20 is supported; Skip ROM is used and scratchpad CRC is checked.

Examples:

```text
> $get_info
< $ACK,get_info,ceilometer,0.4.0,pico2,150000000,10000000,1023,0,0,0
> $capture,16
< $ACK,capture
> $get_state
< $ACK,get_state,READY,16,0,<elapsed_us>,0,0,0,<correlation_us>
> $histogram,0,4
< $ACK,histogram,0,<bin0>,<bin1>,<bin2>,<bin3>
```

Angle brackets describe variable fields and are not sent literally. Read 1023
histogram/profile bins in pages; `raw` has `1023*last_burst_frames+1` samples.
`READY` means the programmed DMA transfer completed without a detected DMA bus
error or PIO TX stall. It does **not** certify that every pacing request was
serviced at the intended physical bin boundary.

Correlation is `N * sum_k(h[k] * (2*code[(k-d) mod N]-1)) - sum_k(h[k])`, N=1023.
Dividing by 1023 gives count-unit DC-rejected correlation. Positive delayed
copies peak at positive lag d. Range is `(lag-offset_bins)*299792458/(2*chip_hz)`.
Calibrate offset/polarity using an electrical or optical reference before assigning
physical range. No range correction, normalization by integration, or overlap
correction is applied in firmware.


## v0.3 parameter and telemetry commands

| Request | ACK fields / action |
|---|---|
| `parameter_count` | count, schema_version |
| `parameter_info,index` | index, name, unit, minimum, maximum, default |
| `get_param,name` | name, value, unit, minimum, maximum, default |
| `set_param,name,value` / `set_params,name,value,...` | atomic validated changes; all outputs/acquisition off |
| `save` | persist current settings; identical saves avoid flash wear |
| `load` | load latest valid record; outputs stay off |
| `defaults` | restore RAM defaults; use save explicitly to persist |
| `get_storage` | loaded, dirty, generation, page, invalid_record_seen, schema_version |
| `telemetry_count` | number of named telemetry fields |
| `telemetry_info,index` | index, name, unit |
| `get_telemetry,index_or_name` | name, unit, value, valid, sampled_us, age_us, sequence |
| `get_limits` | laser_max_code, build_bias_max_code, hard_TEC_mV_limit, sys_clock_Hz, PRN_length, parameter_flash_offset |
| `get_test_pulse` | actual diagnostic pulse frequency Hz |
| `range_point,index` | group index, first native bin, range_mm, integer averaged correlation |

Metadata/getter/cached telemetry commands remain available during acquisition.
Setters/save/load/defaults reject active outputs or acquisition. `set_param` and
`set_params` accept up to eight pairs subject to the 127-character line limit.
Negative values are accepted for signed parameter descriptors, including window
limits, offsets and polarity. Temperature limits and target are adjustable;
10..30 C is the default laser window. Generic writes and legacy configuration
commands update the same inventory. Legacy live target/gain changes now require
outputs off. See parameter-storage.md and telemetry-inventory.md for semantics.

`capture`, `start` and `laser_dac` accept omitted arguments to use configured
values. `bias_dac` without an argument disables bias (retired target is zero). Actuation still requires the original build enables, limits,
arm and feedback interlocks. Parameter settings never bypass the PID: `tec_mv`
remains rejected. Telemetry timestamps/ages are uint32 microseconds (wrap every
~71.6 minutes); per-entry valid/stale flags should be used in addition to age.


Firmware 0.4.1 adds `$i2c_scan,0` (GP0/1 mainboard) and `$i2c_scan,1`
(GP2/3 receiver). Acquisition and all power outputs must be off. Scan uses only
two-byte reads at nonreserved 7-bit addresses 0x08..0x77, with 1 ms deadlines;
it does not write device configuration or attempt bus recovery. ACK fields:
bus, SDA_before, SCL_before, SDA_after, SCL_after, timeout_count, nack_or_error_count,
address_count, addresses... (decimal addresses). Expected 0x4c/0x4e on bus 0 and
0x48/0x4e on bus 1. Presence alone does not identify or validate a device.


`$i2c_recover,0_or_1` also requires acquisition and all power outputs off.
It releases the bus, sends up to nine open-drain SCL pulses if SDA is held low,
then a STOP and restores 100 kHz I2C. Clock-high waits are bounded at 500 us;
neither pin is ever actively driven high. ACK: bus, cleared, final_SDA, final_SCL.
`cleared=1` reports idle lines only, not peripheral presence. A hard SCL-low fault
is reported as failure; no automatic power cycle is attempted. Recovery is
explicit, not run during bias/TEC/laser operation. A scan allows 100 us for the
final STOP to settle before changing address. It reads two data bytes to complete
the AD5693R readback sequence.

Receiver-disconnected bias testing: while all outputs are off, send
`bias_test,25000` (manual milli-Celsius, -40000..85000), then `bias_enable,1`.
`get_bias_test` returns one field: 1 for manual-temperature test mode, 0 for
normal receiver feedback. `get_bias` then reports the assumed temperature.
The normal overvoltage model, configured and hard50V target limits, compile DAC
ceiling, DAC readback, USB lease and common fault shutdown remain enforced.
`stop`, `bias_enable,0`, `bias_dac,0`, reset and reconfiguration clear test mode.
Test selection is never saved. `bias_test,off` clears it while outputs are off.
Use only for supply diagnostics: no measured receiver temperature is used.

Operating clock:500MHz/1.5V; see operating-clock.md. get_clock returns sys_hz, measured_khz, regulator_selector. Nominal10MHz symbol rate uses an integer PIO divider5.

`count_window,us` (1000..100000, outside acquisition) returns locally accumulated
uint64 edge count, elapsed_us and maximum counter sampling gap_us. No generator
is started. Check sampling gap against counter wrap interval before interpreting.

Laser ceiling: `set_param,laser_max_code,N` (0..65535, default32505) with all outputs and acquisition off. `save` persists it. Zero disables arming. `get_limits` returns the current parameter as its first field. Raising the limit never applies the laser DAC or arms emission.

## v0.5 continuous measurements

See [continuous acquisition](continuous-acquisition.md) for the hardware master
clock, stream commands, binary packet layout, exposure and result ownership.
Finite diagnostic commands above retain their 1023-chip format.


### Firmware 0.6.0 finite-burst timing

The `stream_*` interface uses single-core finite bursts. Each restart discards
complete warmup periods; `measurement_period_ms` requests accepted exposure.
`stream_timing` returns `result_id,finite_burst,wall_ns,dead_time_ns` for the held
result. `dead_time_ns = wall_ns - exposure_ns`; wall ends at final DMA completion,
excluding final foreground folding/download. `stream_result` remains 11 fields.
`capture_timing` returns `exposure_ns,wall_ns,dead_time_ns` for a READY legacy
1023-chip capture. Requested legacy frame counts now exclude repeated warmup.
Warmup must fit the finite buffer: 1..15 frames for 1023 chips, 1..3 for 4095;
otherwise start returns `STARTUP_DISCARD_EXCEEDS_BURST_CAPACITY`.
`stream_debug` fields are backend=1,inflight,length,discard,raw_burst_frames,0,0,blocks.


### Firmware 0.6.1 runtime diagnostics

`get_runtime` returns `boot_id,uptime_us,watchdog_reboot` without changing outputs.
The Pi samples this once per second and records USB disconnect/reboot counts in
status and its journal. Watchdog classification is captured before initialization.
A changed boot ID distinguishes a Pico reboot from a serial-session reconnect.

Firmware >=0.6.2 appends `previous_stage` to `get_runtime`: the retained last
foreground stage before reboot (0 if unavailable). See operating-clock.md.


Firmware 0.6.4 USB CDC descriptors advertise manufacturer `Fogcast` and product
`Fogcast Ceilometer`. The SDK VID/PID remain 2e8a:0009 and the hardware serial
number remains unique. Pi automatic selection requires both descriptor strings
and VID/PID, ignores unrelated Picos without opening them, and rejects multiple
matching ceilometers. Firmware without this identity needs an explicit port for
its initial upgrade. Flashing remains restricted to the configured hardware serial.
