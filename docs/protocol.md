# USB CDC protocol v0.1

ASCII requests: `$command[,argument]\n`. CR and CRLF also work; CRLF dispatches
once. Replies: `$ACK,command[,fields]\r\n` or `$NACK,command,reason\r\n`.
No unsolicited messages, checksum, address, or sequence number. Send one request,
wait for its complete LF-terminated reply, then send the next. USB line coding
baud does not control wire speed. UART transport is disabled.

The receive limit is 127 characters, excluding terminator. Oversize, non-ASCII,
and control-character lines are discarded through the next terminator. Lines
without `$` are ignored. Decimal unsigned parameters reject signs, whitespace,
trailing garbage and overflow. Only `tec_mv` permits a minus sign.

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
| `clear_fault` | clear software fault only if GP16 is high; no automatic restart |
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
| `tec_mv,millivolts` | -500..500 nominal VLDR-VSFB; requires enabled TEC |

States: IDLE, ACQUIRING, READY, FAULT. During ACQUIRING only `stop`, `get_info`,
`get_state`, `heartbeat`, and `get_count` are accepted. BUSY protects shared
buffers and avoids I2C/SPI/one-wire traffic during a burst. Sensor service and TEC
health checks occur between bursts. Laser/TEC operation requires a valid DS18B20
reading between -10 and +50 C; no PID or automatic target temperature is active.
Wait several seconds after boot before arming. Only one externally powered
DS18B20 is supported; Skip ROM is used and scratchpad CRC is checked.

Examples:

```text
> $get_info
< $ACK,get_info,ceilometer,0.1.0,pico2,150000000,10000000,1023,0,0,0
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
