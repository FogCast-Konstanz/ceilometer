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
| `set_rate,hz` | 10000..10000000, exact sys/(15*integer); invalidate prior profile |
| `test_pulse,hz` | GP12 test output; ACK returns quantized actual integer Hz; 0 disables |
| `capture,frames` | 1..1000000 complete PRN frames, GP4 remains low; PRN data on GP13 |
| `start,frames` | same integration, with laser modulation; requires commissioned build, arm, nonzero laser DAC, valid temperature |
| `raw,offset,count` | offset followed by 1..16 raw low-16-bit snapshots from **last burst only** |
| `histogram,offset,count` | offset followed by 1..16 unsigned 64-bit integrated photon-bin counts |
| `profile,offset,count` | offset followed by 1..16 signed 64-bit DC-rejected correlation values |
| `sensors` | adc0_raw, adc1_raw, adc2_raw, laser_temp_valid, laser_mC, receiver_temp_valid, receiver_mC |
| `set_threshold,code` | raw 16-bit U16 DAC code; does not enable detector bias |
| `arm,0_or_1` | 1 allows laser setup; 0 shuts down all power outputs |
| `laser_dac,code` | 0..calibrated compile-time ceiling; nonzero requires arm |
| `bias_dac,code` | 0..calibrated compile-time ceiling; nonzero requires commissioned bias build |
| `bias_enable,0_or_1` | GP18 high enables LT3571; requires commissioned bias build for 1 |
| `tec_read,register` | register (0..7), 32-bit unsigned value; no unrestricted write command |
| `tec_enable,0_or_1` | commissioned soft start / disable; failures latch FAULT |
| `tec_target,milliCelsius` | inside configured laser window, default 24000; reset PID state |
| `tec_polarity,1_or_-1` | set verified heating polarity while TEC and laser arming are off |
| `tec_pid,p,i,d` | integer gains 0..10000 in mV/C, mV/(C*s), mV*s/C; outputs off |
| `get_temperature_control` | TEC enabled, target_mC, valid, measured_mC, output_mV, polarity, thermal_fault |
| `tec_mv,...` | rejected: PID_OWNS_OUTPUT |

States: IDLE, ACQUIRING, READY, FAULT. During ACQUIRING only `stop`, `get_info`,
`get_state`, `heartbeat`, `get_count`, and `get_temperature_control` are accepted. BUSY protects shared
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
< $ACK,get_info,ceilometer,0.3.0,pico2,150000000,10000000,1023,0,0,0
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
| `get_limits` | build_laser_max_code, build_bias_max_code, hard_TEC_mV_limit, sys_clock_Hz, PRN_length, parameter_flash_offset |
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

`capture`, `start`, `laser_dac` and `bias_dac` accept omitted arguments to use
configured values. Actuation still requires the original build enables, limits,
arm and feedback interlocks. Parameter settings never bypass the PID: `tec_mv`
remains rejected. Telemetry timestamps/ages are uint32 microseconds (wrap every
~71.6 minutes); per-entry valid/stale flags should be used in addition to age.
