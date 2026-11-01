# Single-core finite-burst PRN acquisition (firmware 0.6.0)

The acquisition backend now follows the previously hardware-tested finite-DMA,
single-core design. The hardware start and repeated warmup exclusion are new;
the resulting build still requires timing qualification on the real mainboard.
500 MHz and requested 1.5 V remain unchanged. The deployment-only measurement
inhibit has been cleared at the user's request for explicit hardware testing;
outputs still start disabled and firmware interlocks apply. See operating-clock.md
for the 0.6.3 idle-wait correction and reboot diagnostics.

## Timing and counter reconstruction

PWM2B counts GP5 rising edges continuously. PWM3A supplies GP6 chip clock and
its wrap DREQ paces full-width DMA reads of PWM2.CTR. PIO TX and frame-sync
machines are armed together, waiting for the first GP6 hardware rising edge.
PWM starts only after receiver DMA and PIO are armed. The PIO runs at clk_sys;
its OUT/MOV/countdown loop gives exactly sys/chip_hz cycles per symbol.
The shared event does not eliminate input synchronization or DMA bus latency;
measure fixed offset and jitter before interpreting calibrated range.

RX uses the baseline finite buffer of 16,369 32-bit snapshots. DMA IRQ0 stops
the master clock and transmitter. Core 0 then reconstructs modulo-65536 counter
differences and folds accepted periods. Core 1 is never launched; there is no RX
ring and no TX reload DMA. The finite TX source contains 32 exact PRN periods
plus a zero word, longer than any permitted RX burst.

Every restart captures and discards startup_discard_frames complete periods.
The initial snapshot is only a counter baseline. Baselines reset at each burst;
counter increments through gaps are never included. Whole periods following
warmup are accepted, including a shorter final burst to meet profile exposure.
At 1023 chips/10 MHz, one period is 102.3 us, covering about 15.33 km unambiguous
range. Default two-period warmup provides 204.6 us of transmitted history before
accepted samples. Signals beyond the PRN unambiguous range can still alias.

Sensors, TEC/bias feedback, slow telemetry and USB run between bursts. Full
profiles are held immutable with modulation low during download. Releasing a
result permits the next profile to start. Stop and faults abort DMA, force the
laser gate low and invalidate pending results. All existing temperature,
lease, driver-fault and parameter-storage interlocks remain in use.

## Parameters and exposure

prn_length selects the existing 1023- or 4095-chip maximal sequence; the default
is 1023. measurement_period_ms requests accepted live exposure, not a guaranteed
wall-clock interval. Fractional whole-frame scheduling preserves long-term
requested exposure: nominal 1 s/1023 chips gives 9775 accepted periods
(999,982,500 ns), with occasional 9776-period profiles.

The unchanged finite buffer permits 16 raw periods at 1023 chips or four at
4095 chips. Warmup must leave at least one accepted period: startup_discard_frames
must be 1..15 or 1..3 respectively. Stored schema-4 values remain intact;
larger values return STARTUP_DISCARD_EXCEEDS_BURST_CAPACITY before acquisition.
Default 2 works for both lengths. At default length each full burst accepts
14 periods after two discarded periods. Stream chip rate remains >=1 MHz.
Legacy capture/start commands retain 1023-chip operation, with requested frames
now explicitly counting accepted periods and warmup at every restart.

Result exposure_ns is accepted_frames * length / chip_hz. stream_timing reports
elapsed wall_ns from first burst setup through the last DMA completion, and
dead_time_ns = wall_ns - exposure_ns. Dead time includes warmup, setup and
inter-burst processing/housekeeping; final folding, correlation and download are
outside that wall window. It is nominal digital exposure, not an optical duty
cycle measurement. first_chip/end_chip are logical sampled chip positions,
including captured warmups, and must not be used as wall time.

## USB and Pi compatibility

Existing stream_start/stream_capture/status/result/binary/release names remain
for compatibility; they now operate finite bursts, not continuous transmission.
stream_result retains its 11 fields; binary pages retain identity and CRC checks.
New stream_timing returns result_id,finite_burst,wall_ns,dead_time_ns for the held
profile. The updated Pi client validates and stores this metadata before release.
Legacy capture_timing returns exposure_ns,wall_ns,dead_time_ns after READY.
stream_debug now describes the finite backend, not DMA ring ownership.
The Pi dashboard shows valid exposure, elapsed acquisition and warmup/gap time.

Host regression tests exercise both lengths, warmup on every restart, arbitrary
counter jumps in gaps, rollover, final partial bursts, held result ownership,
exposure versus wall time, Stop and invalid-DMA rejection. These checks do not
qualify new hardware timing at 500 MHz.
