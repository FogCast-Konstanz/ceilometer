# Parameter and telemetry implementation plan

Implement a single descriptor inventory and validated RAM settings, a versioned
CRC32 journal in two reserved flash sectors, then USB metadata/get/set/save/load/
defaults commands. Keep explicit saves as in crossing's parameter_storage.c,
but preserve the previous valid sector during rollover and verify writes.
No enable/arm/running state is persistent. Output target codes remain configured
values and never energize the laser, bias or TEC on boot/load.

All entries below are adjustable and persistent. Safety-related configurations
must be changed with all outputs off. Temperature windows nest inside the wider
TEC protection envelope, and the target must be inside the laser window.
The 10..30 C laser window and 24 C target are defaults, not fixed limits.
Parameter ACKs accept configuration; telemetry distinguishes pending hardware
application from successfully read-back values. Absent devices do not block
restoring defaults or saving settings.
Fractional PID gains use milli-units. Static board pin assignments, addresses,
150 MHz system clock, ten-bit PRN polynomial/length, unity DAC gain, watchdog
and build-time commissioning ceilings remain design/qualification constants.
Runtime diagnostic pulse generation and output enable/arm actions are transient.

| Parameter | Unit | Default | Allowed bounds | Purpose |
|---|---|---:|---|---|
| `laser_min_mc` | mC | 10000 | -10000..50000 | Laser temperature interlock lower limit |
| `laser_max_mc` | mC | 30000 | -10000..50000 | Laser temperature interlock upper limit |
| `tec_target_mc` | mC | 24000 | -10000..50000 | TEC set temperature, inside laser window |
| `tec_min_mc` | mC | -10000 | -10000..50000 | TEC feedback protection lower limit |
| `tec_max_mc` | mC | 50000 | -10000..50000 | TEC feedback protection upper limit |
| `temperature_age_ms` | ms | 2000 | 1000..10000 | Maximum age from DS18B20 conversion request |
| `ds_period_ms` | ms | 1000 | 800..5000 | DS18B20 conversion request interval |
| `pid_p_milli` | milli_mV_per_C | 100000 | 0..10000000 | PID proportional gain times 1000 |
| `pid_i_milli` | milli_mV_per_C_s | 10000 | 0..10000000 | PID integral gain times 1000 |
| `pid_d_milli` | milli_mV_s_per_C | 0 | 0..10000000 | PID derivative gain times 1000 |
| `tec_polarity` | sign | 0 | -1..1 | 0 uncommissioned, -1 or +1 required before TEC enable |
| `tec_output_mv` | mV | 500 | 0..500 | Symmetric TEC differential output limit |
| `pid_integral_mv` | mV | 500 | 0..500 | Symmetric PID integral limit, <= output limit |
| `tec_ilimn_code` | code | 64 | 48..64 | Negative current limit code; <= existing ~0.85 A ceiling |
| `tec_ilimp_code` | code | 448 | 448..462 | Positive current limit code; <= existing ~0.85 A ceiling |
| `tec_frequency` | selector | 5 | 0..5 | 0..5 selects 0.5,1,1.5,2,2.5,3 MHz |
| `laser_dac_code` | code | 0 | 0..65535 | Configured laser DAC target; never applied automatically at boot |
| `bias_dac_code` | retired | 0 | 0..0 | Retired raw bias target; compensation owns the DAC |
| `threshold_code` | code | 0 | 0..65535 | Receiver discriminator DAC code |
| `chip_hz` | Hz | 10000000 | 10000..10000000 | Exact 150 MHz/(15*integer) chip/bin rate |
| `integration_frames` | frames | 16 | 1..1000000 | Default capture/start integration frames |
| `prn_seed` | state | 1023 | 1..1023 | Nonzero ten-bit Fibonacci seed |
| `range_offset_bins` | bins | 0 | -1022..1022 | Signed range timing offset |
| `range_average_bins` | bins | 1 | 1..1023 | Number of native bins per exported range-profile group |
| `usb_lease_ms` | ms | 2000 | 500..10000 | USB activity lease while outputs or acquisition are active |
| `telemetry_period_ms` | ms | 100 | 20..2000 | Full telemetry sweep interval |
| `active_adc_period_us` | us | 1000 | 100..10000 | Laser-current ADC interval during emitting bursts |
| `amux_settle_us` | us | 1000 | 100..2000 | LT8722 mux settling before ADC sampling; bench qualify |
| `adc_average` | samples | 8 | 1..64 | ADC averaging count for idle/full telemetry sweep |
| `adc_reference_uv` | uV | 3000000 | 2800000..3100000 | External REF3030 ADC reference calibration |
| `laser_sense_uv_per_ma` | uV_per_mA | 0 | 0..10000 | 0 means laser current calibration unavailable |
| `laser_sense_offset_uv` | uV | 0 | -100000..100000 | Laser current amplifier zero offset |
| `bias_sense_uv_per_ua` | uV_per_uA | 0 | 0..1000000 | 0 means bias current calibration unavailable |
| `bias_sense_offset_uv` | uV | 0 | -100000..100000 | Bias-current monitor zero offset |
| `laser_temp_offset_mc` | mC | 0 | -5000..5000 | Laser DS18B20 calibration offset |
| `receiver_temp_offset_mc` | mC | 0 | -5000..5000 | Receiver MCP9800 calibration offset |
| `receiver_resolution` | bits | 12 | 9..12 | MCP9800 conversion resolution |
| `laser_dac_ref_uv` | uV | 2500000 | 2400000..2600000 | U2 reference estimate/calibration, gain fixed unity |
| `bias_dac_ref_uv` | uV | 2500000 | 2400000..2600000 | U6 reference estimate/calibration, gain fixed unity |
| `threshold_dac_ref_uv` | uV | 2500000 | 2400000..2600000 | U16 reference estimate/calibration, gain fixed unity |

| `sipm_overvoltage_mv` | mV | 10000 | 100..12000 | See sipm-bias.md |
| `sipm_breakdown_mv` | mV_at_reference | 36500 | 35000..38000 | See sipm-bias.md |
| `sipm_reference_mc` | mC | 25000 | -40000..85000 | See sipm-bias.md |
| `sipm_tempco_uv_c` | uV_per_C | 70000 | 0..100000 | See sipm-bias.md |
| `bias_limit_mv` | mV | 50000 | 10000..50000 | See sipm-bias.md |
| `bias_temperature_age_ms` | ms | 1000 | 800..5000 | See sipm-bias.md |
| `bias_gain_ppm` | ppm_of_nominal | 1000000 | 800000..1200000 | See sipm-bias.md |
| `bias_offset_uv` | uV | -5000000 | -6000000..-4000000 | See sipm-bias.md |

SiPM compensation details and safety boundaries: [sipm-bias.md](../bringup/sipm-bias.md).

Telemetry coverage plan: ADC0 laser-current sense, ADC1 tagged with the selected
LT8722 AOUT source, ADC2 LT3571 bias-current sense, Pico 2's existing VSYS/3 ADC3 and VBUS GPIO24, MCU temperature ADC4 raw;
laser DS18B20 temperature/scratchpad/age/validity, receiver MCP9800 temperature/
configuration/hysteresis/limit registers; U2/U6/U16 actual digital DAC register
readbacks plus requested codes; all eight LT8722 SPI registers and all thirteen
distinct AOUT signals (channels 13..15 alias channel 12); laser NER, shared SWEN
pin, bias enable, counter and acquisition status. Use normal AMUX test mode 0.
Expose raw samples, nominal derived quantities, timestamps, age and validity.
Capture laser-current sense during emitting bursts as well as idle sweeps.

LT8722 normal AOUT signals: positive/negative current-limit DAC monitors, control
DAC monitor, differential output voltage, output current, 2.5/1.25/1.65 V
references, die temperature, VIN, VCC, VDDIO and SFB. LDR follows from SFB plus
differential output. SWEN is bidirectional and must be released, not driven high,
so the device can signal a fault on the connected pin without contention.

No voltage divider exists for direct SiPM bias voltage, no threshold-output ADC
is wired, and U3 exposes a combined error rather than an internal fault reason.
Report those measurement limits rather than inventing telemetry. Current
engineering-unit values require user calibration because the PCB's INA293 input
resistors affect gain and the LT3571 monitor transfer must be verified.

Tests: descriptor bounds/cross-constraints; metadata and legacy command mappings;
save/reboot/default/no-write-on-unchanged; corrupt/torn records, generation wrap,
sector rollover and power loss; ADC/mux selection and restore, DAC I2C readback,
cached telemetry during acquisition, validity/stale propagation; existing thermal
and laser interlock tests; default and commissioned Pico 2 cross-builds with a
linker boundary protecting the storage region. No connected hardware is assumed.

Current parameter inventory adds laser_max_code (49th field): code0..65535,
default32505. Adjustable only with all outputs off; Save persists. Replaces
CEIL_LASER_MAX_CODE. Bias ceiling remains compile-time. Laser temperature and
arming interlocks remain in force. Firmware schema3 migrates schemas1/2.
