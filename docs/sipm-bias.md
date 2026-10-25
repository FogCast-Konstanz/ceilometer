# Temperature-compensated SiPM bias

AFBR-S4P11P012R bias is owned by the compensation controller. With defaults:

```
Vbreakdown(T) = 36.5 V + 0.070 V/C * (T - 25 C)
Vbias(T)      = Vbreakdown(T) + 10.0 V
```

Targets are 44.75 V at 0 C, 46.5 V at 25 C, 48.25 V at 50 C, and 50 V at
75 C. Firmware rejects a requested target above `bias_limit_mv` or the immutable
50000 mV ceiling. It disables and latches a fault if an active target exceeds
the limit; it does not silently reduce the specified overvoltage. With default
parameters this prevents bias operation above 75 C. Receiver temperature must
also remain within the SiPM's -40..85 C operating range.

The receiver MCP9800 U17 supplies temperature feedback, with
`receiver_temp_offset_mc` applied. The laser's DS18B20 controls the TEC/laser
interlock separately. Receiver polling runs at 100 ms in acquisition dead time.
Configuration must show continuous conversions at the selected resolution; a
configuration mismatch or I2C failure invalidates feedback. After startup,
reconfiguration or reconnect, require 600 ms of normal sensor operation (the
12-bit maximum conversion time). Timestamp each reading conservatively 600 ms
before the read, rather than claiming it was converted at read time.
`bias_temperature_age_ms` defaults to 1000 ms; it must be at least 800 ms.
Freshness checks run even during DMA, without I2C traffic. Bias-active acquisition
bursts are limited to about 50 ms, with one frame minimum (102.3 ms at 10 kchip/s).

The board feedforward transfer follows the exported schematic:
R14+R17=1208 kohm and R21=20 kohm yield MONIN/CTRL=61.4;
R22=15.4 kohm and R23=10 kohm yield CTRL/DAC=10/25.4.
The LT3571 APD pin nominally sits 5 V below MONIN:

```
Vbias_est = code/65536 * bias_dac_ref_uv * (6140/254)
            * bias_gain_ppm/1000000 + bias_offset_uv
```

All voltages in that expression are microvolts. `bias_gain_ppm=1000000` and
`bias_offset_uv=-5000000` are nominal, adjustable commissioning calibration.
DAC codes round down; targets above either voltage ceiling or the independent
bench-calibrated `CEIL_BIAS_MAX_CODE` are rejected. Code readback must match
before SHDN is asserted and is checked at each receiver feedback update.
An I2C/write/readback error deasserts SHDN first and latches a fault.
Missing/stale/out-of-range feedback does the same. Faults stop the other power
outputs and acquisition through the common emergency path. Recovery requires
fresh valid feedback, a realizable target, `clear_fault`, then explicit enable.

Build defaults still disable bias. No saved parameter enables it after boot.
`bias_enable,1` enables compensation after valid feedback and commissioning;
`bias_enable,0`, `bias_dac,0` and `stop` disable bias. Nonzero `bias_dac` is rejected
with `TEMPERATURE_COMPENSATION_OWNS_OUTPUT`. The retired `bias_dac_code` field
is fixed at zero. Schema 2 loads schema 1's other settings, discards its raw bias
target and defaults the eight new fields. An explicit save upgrades the record.

Adjustable persistent parameters:

| Parameter | Default | Purpose |
|---|---:|---|
| sipm_overvoltage_mv | 10000 | Desired overvoltage, 100..12000 mV |
| sipm_breakdown_mv | 36500 | Calibrated breakdown at reference temperature |
| sipm_reference_mc | 25000 | Reference temperature for breakdown calibration |
| sipm_tempco_uv_c | 70000 | Breakdown temperature coefficient |
| bias_limit_mv | 50000 | Lowerable target ceiling, never above 50 V |
| bias_temperature_age_ms | 1000 | Maximum conservative receiver feedback age |
| bias_gain_ppm | 1000000 | Relative correction of board transfer gain |
| bias_offset_uv | -5000000 | APD output offset calibration |

`get_bias` returns enabled, latched fault, reason, target-valid, receiver mC,
receiver age us, estimated breakdown uV, target uV, estimated output uV and
target DAC code. Reasons: 0 OK, 1 temperature, 2 voltage limit, 3 build/code limit,
4 DAC failure. Validity describes a realizable target, not a measured voltage.
The generic telemetry inventory also exposes these estimates, estimated
overvoltage, fault/reason and receiver feedback sequence. Target code and voltage
can be valid while disabled; they are planned outputs. Actual DAC requested and
readback fields and `bias_enabled` indicate actuation. Disabled/decaying physical
high voltage cannot be inferred from a zero DAC code.

The PCB has **no bias-voltage ADC or independent 50 V clamp**. GP28 measures
LT3571 MON current. Firmware limits commanded/modelled voltage, but cannot
guarantee physical voltage under component tolerances, converter transients,
load drops, temperature gradients or a hardware failure. The converter model
and sensor offsets require bench calibration, including temperature/load effects.
`CEIL_BIAS_MAX_CODE` must be qualified against the actual APD voltage with margin.
An independent hardware clamp/interlock, qualified to remain <=50 V including
tolerances and transients, is required to meet an unconditional physical ceiling.
Do not qualify a ceiling by testing one nominal operating point only.

Broadcom specifies device-to-device breakdown variation; 36.5 V is typical.
Calibrate the installed device to obtain the intended overvoltage. The 12 V
parameter maximum is the device absolute maximum, not a recommended operating
point or an allowance for calibration error. Temperature compensation is
feedforward; it does not measure breakdown, actual high voltage or junction
temperature independently.

References: [Broadcom AFBR-S4P11P012R data sheet](https://docs.broadcom.com/doc/AFBR-S4P11P012R-NIR-Dual-Channel-SiPM-Data-Sheet),
[LT3571 data sheet](https://www.analog.com/media/en/technical-documentation/data-sheets/3571fa.pdf),
[MCP9800 data sheet](https://ww1.microchip.com/downloads/en/DeviceDoc/21909c.pdf),
and the committed hardware-netlist.xml.
