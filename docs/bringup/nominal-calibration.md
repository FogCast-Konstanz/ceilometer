# Nominal calibration starting values

Calculated from docs/hardware-netlist.xml and manufacturer datasheets, 2026-10-25.
These are nominal estimates, not measured calibration. No outputs were enabled.

## SiPM bias

AD5693R internal reference 2.5 V, unity gain, code D:
Vdac = 2.5 D/65536 V.
R14+R17 = 1208 kohm, R21=20 kohm, R22=15.4 kohm, R23=10 kohm.
Vbias = (1+1208/20)*(10/25.4)*Vdac - 5
      = 60.433070866 D/65536 - 5 V.
Use floor((Vtarget+5)*65536/60.433070866).

| Receiver temperature C | Target V | DAC code (rounded down) |
|---:|---:|---:|
| 0 | 44.75 | 53950 |
| 25 | 46.5 | 55848 |
| 50 | 48.25 | 57746 |
| 75 | 50 | 59644 |

Nominal maximum code: 59644. Existing defaults bias_gain_ppm=1000000,
bias_offset_uv=-5000000, bias_dac_ref_uv=2500000 already implement this model.
The 50 V limit is a commanded nominal limit; actual output must be measured.
MON mirror ratio is 0.2; R15=4990 ohm gives 998 uV/uA.
Starting bias_sense_uv_per_ua=998, bias_sense_offset_uv=0.

## Laser command

U2 drives all six CI inputs; all six LDK outputs and enable inputs are paralleled.
ELVDS is grounded (TTL). VCI=2.5 D/65536 referenced to board ground.
AGND is raised by total laser current through R9=0.04 ohm:
VCI_effective = 2.5 D/65536 - 0.04*I_total.
The proper nominal model is implicit:
I_total = 6*f(VCI_effective, VDD, VLDK, temperature),
where f is the iC-HG30 single-channel characteristic, not a constant gain.
VDD is nominal 3.3 V through R6=1 ohm.

Datasheet B4 electrical characteristics 109/110: threshold spans 0.5..1.2 V;
1 A/channel requires 2..2.9 V at CI with LDK >2.3 V (AGND=GND).
Thus no unique precision current-per-code constant follows from those limits.
For 6 A total the shunt drop alone is 0.24 V, and the required DAC voltage
would span 2.24..3.14 V. The 2.5 V DAC cannot reach the upper end.
The driver's 30 A pulsed headline is not an appropriate PRN-CW current limit.
A laser-diode rated operating current is not specified by LD1's generic schematic
value, so a diode-compatible command ceiling cannot be inferred from it.

## Laser current monitor

R9=0.04 ohm, INA293B1 nominal gain20. R8=R10=10000 ohm.
TI Eq.5: F=RB*R1/(RB*R1+RB*Rin+2*Rin*R1),
RB=6600 ohm nominal, R1=25000 ohm, Rin=10000 ohm.
F=0.2257181943; effective gain=4.514363885 V/V.
Vadc/I = 0.1805745554 V/A = 180.5745554 uV/mA.
Starting integer laser_sense_uv_per_ma=181, laser_sense_offset_uv=0.
Without the input resistors the scale would be 800 uV/mA, which is incorrect
for this schematic. Internal resistor variation and offset make bench calibration
necessary. TI recommends input series resistors <=10 ohm; this PCB uses 10 kohm.
During PRN modulation the filtered monitor represents averaged current, not
instantaneous on-state current; account for duty cycle when calibrating.

## Sources

- iC-Haus iC-HG30 datasheet B4 pp6,8-10 (manufacturer document hosted by distributor): https://sklep.semicon.com.pl/files/shop.php?id_plik=16562
- TI INA293 Rev B pp14,17-18: https://www.ti.com/lit/ds/symlink/ina293.pdf
- Analog Devices LT3571: https://www.analog.com/media/en/technical-documentation/data-sheets/3571fa.pdf

## Applied commissioning build

All three power drivers compiled in; CEIL_LASER_MAX_CODE=32505 and
CEIL_BIAS_MAX_CODE=59644. Laser code32505 gives 1.23997 V nominal DAC.
Figure1 (27 C, 3.3 V) suggests roughly 1.2 V for 0.167 A/channel;
adding the nominal 40 mV shared-shunt drop gives approximately 1 A total.
This is a coarse curve estimate, not a calibrated current limit or regulation.
It means on-state current; PRN average is approximately half at 50% duty.
Nominal monitor scales181 and998 are now firmware defaults. Zero still disables
engineering-unit current conversion. The board was configured in RAM with
laser_dac_code32505 and tec_polarity1; no outputs were activated. Flash Save
has a known reset issue, so these two RAM settings need reapplying after reset.

Flash Save was subsequently fixed and verified on hardware. The commissioning
laser target32505 and TEC polarity1 are now saved and restored after restart;
the earlier RAM-only limitation above no longer applies.

## First measured bias calibration

User measured50.985 V at25 C test setting (nominal code55848, old gain1000000,
offset-5V, reference2.5V). Assuming the offset remains-5V:
gain_ppm=ceil((50.985+5)/(old_estimated_output+5)*1000000)=1087100.
Saved this gain on the board. Corrected25 C target code51373 (estimated46.499042V).
Reduced compile DAC ceiling59644 ->54865, nominal calibrated50V ceiling.
This is a single-point fit, not a verified physical ceiling. A second voltage
measurement is needed to distinguish offset from gain and validate the correction.
All outputs left off; physical corrected voltage has not yet been measured.

## Two-point APD calibration (supersedes first VOUT-based fit)

The first readings were at VOUT, not APD, so the prior gain1087100 was incorrect
for calibrating SiPM bias. At that prior configuration, user subsequently measured
APD41.938V at25C and42.894V at40C (codes51373,52421).
Fit APD_uV=a*code+b: a=956000/1048 uV/code, b=-4925155.534uV.
Saved gain989241ppm (rounded up) and offset-4925156uV (nearest integer).
Compile ceiling60210 corresponds to the fitted nominal50V maximum.
New target codes:25C56373,40C57525. Values verified in firmware with outputs off.
Physical corrected output needs remeasurement; this unloaded local two-point fit
does not establish load/temperature/transient accuracy or an independent clamp.
