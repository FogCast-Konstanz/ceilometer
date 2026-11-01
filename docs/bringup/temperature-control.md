# DS18B20 TEC control and laser interlock

U18/GP19 measures the laser diode mounted on the TEC. It is the sole temperature
feedback for this loop. The receiver MCP9800 and LT8722 die telemetry are not
substitutes. The PID is ported from the actual sibling checkout
`C:/Users/MarianKeller/Repos/laphtec/Laser-Controller/mini_laser_controller/Core`:
`Inc/pid_controller.h` and `Src/pid_controller.c` (original attribution retained).
The earlier underscore directory is a different project and lacks this PID.

The laphtec algorithm retains limited integral accumulation, filtered
derivative-on-measurement (0.1 s filter), output limiting and reset. Invalid or
nonfinite arithmetic now fails closed instead of substituting a tiny dt. Unlike
the reference's 1 ms ADC loop, this DS18B20 loop advances once per fresh completed
conversion using actual sample intervals, normally about one second. Cached
readings never integrate again. Feedforward is zero without calibrated current.

The default target is 24 C; `tec_target,value` sets milli-Celsius inside the
configured laser window. Both window endpoints and target are persistent parameters. The
commissioning gains are P=100 mV/C, I=10 mV/(C*s), D=0 mV*s/C; they are not claimed
to be tuned for this TEC. Use `tec_pid,p,i,d` (unsigned integer gains 0..10000)
only while TEC and laser arming are off. Output and integral limits remain
adjustable downward from a hard +/-500 mV ceiling through persistent parameters. This deliberately does not copy the laphtec plant's 4/-4.5 V
limits or its gains into the different thermal plant.

Polarity must be chosen explicitly before TEC enable: `tec_polarity,1` means
positive LT8722 VLDR-VSFB for positive (target-measured) error; `-1` inverts it as
in the reference temperature controller. Confirm which direction heats the diode
using a safe bench test. The default polarity is uncommissioned; a saved, explicitly selected polarity is restored on reset. The commissioned
TEC build option remains necessary. `tec_enable,1` starts regulation only with
valid feedback, configured polarity and no latched fault. It retains the driver's
soft start and nominal +/-0.85 A limits. Repeated enable requests are idempotent.
Manual `tec_mv` requests return `PID_OWNS_OUTPUT` and cannot bypass supervision.

Laser arming, nonzero laser DAC commands and every emitting burst require a valid
temperature **inside the configured window, inclusive (default 10 to 30 C)**. Leaving this window immediately stops
the laser/acquisition, zeros the laser DAC and disarms. If temperature is still
valid and within the configured TEC protection envelope (default -10..50 C), TEC regulation
continues to bring the diode back into range. Returning into range never rearms
the laser automatically. The TEC is not required to be on for a naturally
in-range laser; the actual temperature interlock always applies.

No valid reading, bad scratchpad CRC/configuration, failed presence/reset,
incomplete conversion, the 85 C power-on sentinel, or stale feedback disables the
TEC, resets PID state, stops/disarms the laser and latches a fault if either was
active. A new conversion request does not refresh the timestamp of an old sample.
The freshness budget is the configured age limit (default 2 seconds) measured conservatively from the successful
conversion's request time, not the read time. The wider TEC temperature envelope
also fails closed. Recovery requires valid feedback, `clear_fault`, and an
explicit new `tec_enable,1` / `arm,1`; it does not restart on reconnect alone.

One-wire transactions and SPI output updates run between DMA bursts; cached
freshness and laser range are checked every foreground iteration, including
during acquisition. With thermal outputs active, bursts are capped near 50 ms
(one complete 1023-chip frame minimum, at most 102.3 ms at the minimum 10 kHz
chip rate). Detected invalid feedback is handled before another emitting burst.
Physical sensor loss is observable on the next presence/conversion transaction,
normally within one conversion cycle; this is not instantaneous wire-break
detection. The watchdog and two-second USB activity lease remain in effect.

`get_temperature_control` returns: TEC enabled, target_mC, temperature_valid,
temperature_mC, output_mV, polarity, thermal_fault. It is available while acquiring.
Keep polling/heartbeat at least once per second in one persistent USB session.

Bench validation must include sensor disconnect, shorted bus, corrupted CRC,
sensor power-cycle/85 C, read-service starvation, USB loss, polarity, output sign,
temperature ramps across 10/30 C, recovery requiring rearm, and thermal step
response before tuning gains. Native tests exercise the portable PID, decoder,
freshness, boundary and fail-closed state logic; no hardware is simulated by those
tests. Separate host tests run the actual one-wire driver against scripted GPIO
read slots and the actual command handler/shutdown wiring against mocked hardware.
Configured temperature thresholds are measured sensor values; compensate sensor
accuracy and thermal gradients separately if physical diode limits require it.

DS18B20 timing/format reference:
[manufacturer datasheet](https://www.analog.com/media/en/technical-documentation/data-sheets/DS18B20.pdf).
