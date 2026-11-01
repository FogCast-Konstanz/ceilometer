# Single-core acquisition at 500 MHz: proposed architecture

Status: the preferred finite-burst implementation is now firmware 0.6.0.
See [current acquisition behavior](continuous-acquisition.md). Hardware timing
qualification is still pending. The user has authorized removing the deployment
inhibit for explicit testing; outputs remain disabled until Enable is pressed.
The interrupt-driven continuous design below remains a future proposal.

## Preferred immediate baseline: retain finite bursts

Following review of the previously working hardware implementation, the immediate
recommendation is the original single-core finite-burst acquisition with only
the shared hardware TX/RX start added. The interrupt-driven continuous design
below is a future option, not a prerequisite for one-second integrations.

The recorded real-mainboard test completed twenty 16-period captures at 500 MHz
and 10 MHz chip rate without reported TX starvation. The old acquisition already
accumulates multiple bursts and correlates once at the end of an integration;
9775 periods represent 0.9999825 s of nominal sampled exposure at 10 MHz.

Keep the original PWM edge counter, finite receiver DMA, foreground histogram
accumulation and housekeeping between bursts. Isolate/remove continuous RX ring,
core-1 launch and continuous TX DMA reload from this baseline build. Retain the
existing interlocks, calibration, parameter storage and diagnostics.

The small additions should be:
- Shared PWM master start: transmitter PIO waits for its hardware edge and the
  same master supplies RX DMA pacing. Verify/calibrate fixed offset and spread.
- At each optical TX restart, discard complete startup PRN periods before
  accepting bins, with correct first/last snapshot boundaries.
- Report accepted exposure, wall duration and inter-burst dead time separately.
- Adapt Pi measurement scheduling/readout to finite integrations before removing
  explicit Enable and firmware interlocks. A one-second live exposure takes longer than one
  wall-clock second when burst gaps are present.

This deliberately restores the tested timing/workload structure first. It does
not assert that an exact historical binary is available, or that adding hardware
start is already qualified. Firmware stays at 500 MHz and measurements remain
disabled until the operator explicitly starts a bench test of the baseline.

## Evidence and limits

The current firmware launches core 1 only on stream_start/stream_capture, not
at boot. USB failures have also been observed before launching acquisition.
Dual-core load is therefore a candidate contributor, not an established sole
cause. The 400 MHz diagnostic returned three CRC-checked one-second profiles
with zero drops, but that does not qualify sustained 500 MHz operation.

Keep the system clock at the requested 500 MHz and requested 1.5 V. Do not enable
measurement or remove the Pi inhibit merely because a single-core image builds.

## Recommended design

1. Retain continuous PRN PIO/TX DMA, the common PWM3 hardware start/clock, PWM2
   rising-edge counting and the single continuously paced RX DMA ring. Those
   functions do not require a second CPU core. No interrupt per chip or photon.
2. Run receiver block reconstruction in a bounded core-0 DMA IRQ. At 10 MHz,
   4096 snapshots arrive every 409.6 us: about 2441 block IRQs/s and 50 system
   clock cycles per sample. GPIO driver-fault shutdown must preempt this IRQ;
   USB/background work has lower priority. Target <=200 us worst-case block
   processing initially, then qualify under full load rather than assuming it.
3. Use checked uint32 histogram bins in the hot loop, retaining uint64 totals,
   exposure and the existing wire format. At a hypothetical 250 Mcps input and
   the maximum configured 60 s exposure, a 1023-bin uniform phase-coverage
   histogram is bounded by approximately 14.7 million counts/bin, far below
   UINT32_MAX. Always detect overflow; do not rely on a nominal optical rate.
   This bound assumes continuous complete PRN periods and the stated counter
   acceptance bound; corrupted input must still fault rather than wrap.
4. Swap histogram ownership at profile boundaries instead of copying a whole
   histogram in the IRQ. Use active, completed and pre-cleared spare buffers;
   clear/recycle released buffers in foreground slices. If the host retains a
   result, preserve it and report dropped newer results. Do not stall sampling.
5. Make housekeeping cooperative: one I2C/SPI/ADC operation per scheduler step,
   and AMUX select -> settling deadline -> ADC read -> next channel. Avoid the
   current whole telemetry sweep with 13 consecutive settling waits.
6. Move DS18B20 slot/reset timing to PIO with asynchronous completion. The
   current driver masks interrupts for 60 us bit slots and a 70 us presence
   sample. A long acquisition IRQ could also delay an unmasked reset/release
   transition. Preserve CRC, conversion completion, freshness and TEC/laser
   shutdown semantics. Treat this as required timing work, not cosmetic cleanup.
7. Keep correlation, normalization, recording and plotting on the Raspberry Pi.
   Continue sending integrated histograms, not the 40 MB/s raw snapshot stream.
   Convert uint32 bins to the established uint64 packet format page-by-page
   outside the IRQ. Keep USB requests and binary transfers bounded/cancellable.

The DMA IRQ must check hardware write position, pending completion and elapsed
time before publishing. A software completion counter cannot detect a second
completion while that same IRQ is still executing and cannot re-enter itself.
On timing/ownership failure, force the laser gate low and invalidate the profile.
Foreground then performs the existing common shutdown.

## Alternatives

A foreground-only accumulator is possible only after every blocking sensor,
telemetry and USB operation yields well within the DMA overwrite deadline. It
is not a safe first step with the present foreground code.

Increasing buffer size alone does not solve average CPU throughput. The current
hardware address ring is limited to 32 KiB; larger buffers need a different DMA
control design and another sampling-latency qualification. Do not introduce an
inactive second pacing channel, which can accumulate stale DREQ credit.

Replacing the PWM counter with a PIO pulse-counting loop would change pulse-width
and maximum-rate acceptance. Preserve the existing counter for this revision.

## Implementation and qualification order

A. Add a single-core backend build option, retaining protocol/config compatibility.
   First build and host-test without changing the deployed inhibited instrument.
B. Add checked 32-bit bins, buffer-ownership tests and profile-boundary handling.
C. Implement asynchronous DS18B20 and sliced telemetry before combined tests.
D. With explicit authorization for laser-disabled bench captures, compare the
   same firmware workload on one versus two cores at 500 MHz. Measure block
   processing, worst IRQ-off time, USB integrity, resets and sensor failures.
E. Stress USB download, TEC telemetry and sensor activity; test 1023/4095 chips,
   repeated starts/stops, one-second profiles and prolonged runs. Use an external
   pulse/delay reference to check bin timing, not just total counts.
F. Only after this passes consider removing the operator inhibit. Single-core
   firmware cannot guarantee operation beyond the MCU rated clock.
