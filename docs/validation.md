# Validation and bench qualification

## Software checks

The native C tests verify full PRN period, 512/511 balance, all 1023 bipolar
autocorrelation lags, every packed bit across 32 code repeats, rollover modulo
differences, delayed synthetic return peak/polarity, uniform-background rejection,
atomic overflow rejection, numeric parsing, fragmented/CRLF/oversize/control-byte
line handling and SPI CRC/ACK failure detection. The Pico cross-build assembles
the actual PIO sources and links USB CDC and all peripheral drivers with compiler
warnings treated as errors. There is no connected hardware test in this result.

## Acquisition architecture and expected numbers

150 MHz clk_sys / 15 gives 10 MHz chip/bin rate: 100 ns bins, 14.9896229 m
spacing, 102.3 us PRN period and about 15.334 km unambiguous range. The polynomial
is x^10+x^3+1, seed 0x3ff, output LSB, right shift, XOR old bits 0 and 3 into bit 9.
Code 1 means gate high. `sequence.c` is the single code definition used by TX and RX.

PIO0 SM0 uses two instructions per chip totaling 15 PIO clocks (7 high-sync,
8 low-sync). Autopull is LSB-first. A 1023-word buffer holds 32 exact repetitions
of the 1023-bit code, followed by a zero tail. PIO0 SM1 marks frames, high for one
PIO clock every 15345 PIO clocks. A shared SM/divider restart aligns the two PIO
streams. No 1024th padding chip is transmitted between frames.

PWM2 free-runs at rising GP5 edges, TOP=65535, integer divider=1. PWM0 generates
wrap DREQs without owning any GPIO. DMA uses 32-bit reads of PWM2 CTR into SRAM,
with fixed source and incrementing destination. At 10 MHz that is 40 MB/s of
snapshot payload; TX requires another 1.25 MB/s payload. Neither figure includes
bus read/write overhead. The 16369-sample maximum burst uses 65476 bytes; TX
buffer uses 4096 bytes; histogram and profile each use 8184 bytes, plus code,
stack and SDK buffers. Single-core foreground processing avoids an unqualified
continuous real-time throughput claim. Correlation runtime is reported in us.

The first sample is a baseline. For sample index i>=1, bin `(i-1)%1023` gets
`uint16_t(C[i]-C[i-1])`. Upper register bits are ignored. Fewer than 65536 edges
must occur between successful reads; multiple counter wraps are irrecoverable.
Accumulators enforce a conservative int64-safe correlation bound and reject the
whole update on overflow. Aborted/failed captures do not become valid profiles.

The PIO-to-PWM start uses sequential writes under an interrupt mask. This does
not synchronize the peripherals in hardware. Sample zero arrives after startup;
even the first full count interval therefore has an uncalibrated lag relative to
code chip zero. Record a signed offset and its run-to-run spread. DMA request
credit queues and service arbitration also need
measurement: final transfer count alone cannot prove one snapshot per bin. The
channel pacing-credit counter is explicitly cleared before each burst.

## Electrical test procedure

1. Use the default build, with the laser disconnected/disabled and bias off.
   Verify GP4 stays low during idle, stop, fault, USB disconnect and reboot.
   Scope ROM/BOOTSEL/reset behavior separately; add/verify an external gate
   pull-down and independent disable before a commissioned output build.
2. Verify ADC reference is ~3.000 V. Confirm all I2C addresses with the fitted
   devices/cable. Scope DAC settling and comparator threshold. Calibrate laser
   current transfer at safe current, and LT3571 CTRL/bias voltage/current using
   external meters; ADC2 does not measure bias voltage. Choose DAC ceilings.
3. Check LT8722 SPI mode0, register0 readback, CRC and ACK, GP17 control direction,
   and faults. With a dummy load verify +/-0.85 A nominal limits, zero output,
   >=5 ms startup ramp, sign, disable and telemetry. Qualify TEC thermal polarity
   before considering temperature feedback. Check DS18B20 location and disconnect
   behavior; firmware uses it as a laser/TEC temperature limit, not a PID input.
4. Isolate GP5 from the receiver output before wiring a pulse generator or
   GP12-to-GP5 jumper. Two actively driven outputs must not be connected together.
   Use 3.3 V logic and common ground. Start at 100 kHz/1 MHz, measure high/low pulse
   widths and scope count accuracy, then sweep frequency. GPIO input synchronizers
   are retained; sub-cycle pulses are not supported or guaranteed to be counted.
5. Run `tools/benchmark.py PORT --repeats 10`. It sweeps 1/2/5/10 MHz bin rates
   and up to 37.5 MHz actual loopback pulses, saving total error, bin statistics,
   elapsed burst time, TX stalls and correlation runtime. With 16 frames, the
   ideal snapshot interval duration is 16368/bin_hz. Compare raw differences and
   long-run integrated totals against generator frequency. Missing pacing can
   still preserve totals; bin timing must also be scoped.
6. Scope GP6/GP7 plus a known delayed copy of the PRN modulation and receiver
   input. Digital `capture` runs the internal PRN and sync signals while GP4 stays
   low and exposes PRN data on GP13. Scope GP13 directly or use an isolated GP13
   loopback fixture. GP6 is chip sync, not the PRN data stream. Edge counting a
   logic-level PRN loopback measures transitions rather than code high duration;
   model that distinction when comparing correlation peaks.
   Verify exact 1023-chip recurrence and no boundary padding. Measure startup
   offset and repeatability over many bursts, then characterize receiver pulse
   placement near bin boundaries and PWM-wrap-to-read latency distribution.
7. Repeat under USB request traffic and intended CPU/SRAM/DMA contention. Current
   firmware does not synthesize arbitrary concurrent DMA stress; use an external
   fixture or explicit test build. Quantify missed requests, latency jitter,
   worst-case stalls and duty/dead time. Log scope/generator settings, firmware
   commit, clock, temperature, counts, phase histogram and acceptance limits.
8. Only after rated-clock qualification investigate alternate clocks, voltage,
   temperature and eventually 20 MHz bins. 500 MHz is not supported or qualified
   here. If phase/latency cannot meet the range uncertainty budget, use external
   latching/counting logic rather than interpreting precise-looking sample counts
   as precise bin boundaries.

## Remaining phases

Deterministic hardware TX/RX start, latched receiver timing, gapless ping-pong
DMA, 20 MHz oversampling, clock/voltage sweeps and dual-core accumulation remain
future engineering work after the finite-burst measurements. Optical current
calibration, independent interlocks, temperature control tuning, overlap and
atmospheric response calibration likewise require the physical system.
