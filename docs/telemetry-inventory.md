# Captured telemetry inventory

Every item is available by name or index through `get_telemetry`; `telemetry_info`
enumerates names and units. Values include validity, last successful timestamp,
age, and sample sequence. Failed reads invalidate the entry without refreshing
old data. No hidden I2C/SPI transfers occur when reading cached telemetry during
acquisition. A full sweep runs between bursts. Direct ADCs include the Pico 2
module's existing VSYS/3 ADC3 and MCU temperature ADC4; GPIO24 reports VBUS.
MCU die temperature is a nominal approximation, not the laser feedback.

LT8722 analog entries require its analog circuitry to be active. Polling never
enables the TEC. The scanner selects normal-mode channels 0..12, waits the
configured settling time, reads ADC1 and restores the prior AMUX register.
Channels 13..15 duplicate SFB and add no distinct quantity. Derived readings use
the measured reference channels, normal-mode transfer functions, and propagate
invalid dependencies. Test modes change monitor scaling and are not normal
telemetry settings. All eight raw SPI registers remain visible, including status
and all DAC/current-limit/clamp settings.

Laser-current ADC0 is also sampled during emitting acquisition bursts. Those
last-capture raw/peak/count entries are distinct from the idle ADC reading and
carry their own age. Their sample timing adds bus traffic and must be included in
DMA jitter qualification. A short burst can end while a conversion is underway;
these are monitored filtered-current samples, not a per-chip current waveform.

Current scales default to nominal schematic estimates: laser181 uV/mA and bias998 uV/uA. These require bench calibration. Setting either scale to zero marks that current reading invalid; raw ADC values remain available. DAC voltage
fields are estimates from digital input-register readback and configured reference,
not independently measured output voltages. No direct bias voltage divider or
threshold-output ADC exists. U3's NER cannot reveal an internal fault category.
ADC sample values at rails cannot establish unsaturated LT8722 monitor voltages.

| Index | Name | Unit |
|---:|---|---|
| 0 | `laser_adc_raw` | code |
| 1 | `tec_adc_raw` | code |
| 2 | `bias_adc_raw` | code |
| 3 | `vsys_adc_raw` | code |
| 4 | `mcu_adc_raw` | code |
| 5 | `laser_sense_uv` | uV |
| 6 | `bias_sense_uv` | uV |
| 7 | `vsys_uv` | uV |
| 8 | `mcu_temperature_mc` | mC |
| 9 | `laser_current_ma` | mA |
| 10 | `bias_current_ua` | uA |
| 11 | `laser_active_raw` | code |
| 12 | `laser_active_peak_raw` | code |
| 13 | `laser_active_samples` | samples |
| 14 | `laser_active_uv` | uV |
| 15 | `laser_active_current_ma` | mA |
| 16 | `laser_temp_mc` | mC |
| 17 | `laser_temp_raw_mc` | mC |
| 18 | `laser_temp_sequence` | count |
| 19 | `receiver_temp_mc` | mC |
| 20 | `receiver_config` | bits |
| 21 | `receiver_hysteresis_raw` | bits |
| 22 | `receiver_limit_raw` | bits |
| 23 | `ld_error` | bool |
| 24 | `tec_swen` | bool |
| 25 | `bias_enabled` | bool |
| 26 | `vbus_present` | bool |
| 27 | `smps_pwm` | bool |
| 28 | `counter_raw` | code |
| 29 | `spi_errors` | count |
| 30 | `ds_presence_errors` | count |
| 31 | `ds_crc_errors` | count |
| 32 | `ds_conversion_errors` | count |
| 33 | `sweep_sequence` | count |
| 34 | `sweep_duration_us` | us |
| 35 | `ds_byte0` | byte |
| 36 | `ds_byte1` | byte |
| 37 | `ds_byte2` | byte |
| 38 | `ds_byte3` | byte |
| 39 | `ds_byte4` | byte |
| 40 | `ds_byte5` | byte |
| 41 | `ds_byte6` | byte |
| 42 | `ds_byte7` | byte |
| 43 | `ds_byte8` | byte |
| 44 | `dac0_requested` | code |
| 45 | `dac1_requested` | code |
| 46 | `dac2_requested` | code |
| 47 | `dac0_readback` | code |
| 48 | `dac1_readback` | code |
| 49 | `dac2_readback` | code |
| 50 | `dac0_uv` | uV |
| 51 | `dac1_uv` | uV |
| 52 | `dac2_uv` | uV |
| 53 | `lt_register0` | bits |
| 54 | `lt_register1` | bits |
| 55 | `lt_register2` | bits |
| 56 | `lt_register3` | bits |
| 57 | `lt_register4` | bits |
| 58 | `lt_register5` | bits |
| 59 | `lt_register6` | bits |
| 60 | `lt_register7` | bits |
| 61 | `lt_mux0_raw` | code |
| 62 | `lt_mux1_raw` | code |
| 63 | `lt_mux2_raw` | code |
| 64 | `lt_mux3_raw` | code |
| 65 | `lt_mux4_raw` | code |
| 66 | `lt_mux5_raw` | code |
| 67 | `lt_mux6_raw` | code |
| 68 | `lt_mux7_raw` | code |
| 69 | `lt_mux8_raw` | code |
| 70 | `lt_mux9_raw` | code |
| 71 | `lt_mux10_raw` | code |
| 72 | `lt_mux11_raw` | code |
| 73 | `lt_mux12_raw` | code |
| 74 | `lt_mux0_uv` | uV |
| 75 | `lt_mux1_uv` | uV |
| 76 | `lt_mux2_uv` | uV |
| 77 | `lt_mux3_uv` | uV |
| 78 | `lt_mux4_uv` | uV |
| 79 | `lt_mux5_uv` | uV |
| 80 | `lt_mux6_uv` | uV |
| 81 | `lt_mux7_uv` | uV |
| 82 | `lt_mux8_uv` | uV |
| 83 | `lt_mux9_uv` | uV |
| 84 | `lt_mux10_uv` | uV |
| 85 | `lt_mux11_uv` | uV |
| 86 | `lt_mux12_uv` | uV |
| 87 | `lt_output_uv` | uV |
| 88 | `lt_output_ua` | uA |
| 89 | `lt_ref2500_uv` | uV |
| 90 | `lt_ref1250_uv` | uV |
| 91 | `lt_ref1650_uv` | uV |
| 92 | `lt_die_temp_mc` | mC |
| 93 | `lt_vin_uv` | uV |
| 94 | `lt_vcc_uv` | uV |
| 95 | `lt_vddio_uv` | uV |
| 96 | `lt_sfb_uv` | uV |
| 97 | `lt_ldr_uv` | uV |
| 98 | `lt_control_uv` | uV |

Normal AOUT channel mapping: 0 positive-current-limit DAC monitor, 1 negative limit,
2 control DAC monitor, 3 VLDR-VSFB, 4 output current, 5 2.5 V reference, 6 1.25 V
reference, 7 1.65 V reference, 8 die temperature, 9 VIN, 10 VCC, 11 VDDIO, 12 SFB.
`lt_output_ua` follows the normal AMUX transfer function sign; establish the sign
relative to physical TEC heating/cooling on the bench. `lt_control_uv` is the
translated control-monitor value, not the raw signed SPI DAC code.

Sources: [LT8722 Rev B, analog monitoring](https://www.analog.com/media/en/technical-documentation/data-sheets/lt8722.pdf),
[AD5693R readback](https://www.analog.com/media/en/technical-documentation/data-sheets/AD5693R_5692R_5691R_5693.pdf),
[Pico 2 onboard monitors](https://datasheets.raspberrypi.com/pico/pico-2-datasheet.pdf).


SiPM bias controller adds nine cached fields (108 total), available during acquisition:

| Name | Unit | Meaning |
|---|---|---|
| `bias_feedback_mc` | mC | See [sipm-bias.md](sipm-bias.md) |
| `bias_breakdown_uv` | uV_estimate | See [sipm-bias.md](sipm-bias.md) |
| `bias_target_uv` | uV_target | See [sipm-bias.md](sipm-bias.md) |
| `bias_estimated_uv` | uV_estimate | See [sipm-bias.md](sipm-bias.md) |
| `bias_overvoltage_uv` | uV_estimate | See [sipm-bias.md](sipm-bias.md) |
| `bias_target_code` | code | See [sipm-bias.md](sipm-bias.md) |
| `bias_fault` | bool | See [sipm-bias.md](sipm-bias.md) |
| `bias_reason` | enum | See [sipm-bias.md](sipm-bias.md) |
| `bias_feedback_sequence` | count | See [sipm-bias.md](sipm-bias.md) |
