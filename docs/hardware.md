# Schematic-derived hardware map

Reviewed 2026-10-25: fresh KiCad 10.0.6 XML netlist exported from
`Elektronik/Ceilometer-Mainboard/Ceilometer-Mainboard.kicad_sch`, all five child
sheets, and the existing review. The source schematics were not edited.
`hardware-netlist.xml` is the fresh export; `schematic-sha256.txt` identifies the
inputs. KiCad logged a registry access warning but returned success and produced
the netlist. This is a connectivity review, not a new PCB qualification.

| GPIO | Connected block / direction |
|---|---|
| 0 / 1 | I2C0 SDA/SCL: U2 laser DAC, U6 bias DAC |
| 2 / 3 | I2C1 SDA/SCL via J4/J5: U16 threshold DAC, U17 MCP9800 |
| 4 | LD_SWITCH output: all six U3 iC-HG30 enable inputs, ELVDS tied low |
| 5 | U10 SN65LVDT2 receiver output; PWM2 channel B edge input |
| 6 / 7 | PRN_SYNC / FRAME_SYNC diagnostics on J3 |
| 8 / 9 / 10 / 11 | SPI1 RX / software CS / SCK / TX to IC1 LT8722 |
| 12 | J3 expansion: optional bench test pulse output |
| 13 | J3 diagnostic PRN data output during dark `capture` |
| 14-15 | J3 expansion, unused |
| 16 | U3 active-low NER laser driver error input, pulled up by R7 |
| 17 | IC1 SWEN **input**, firmware output; misleading net name ~TEC_FAULT |
| 18 | LT3571 SHDN input; high enables bias, R11 pulls low |
| 19 | U18 externally powered DS18B20, 4.7k external pull-up R36 |
| 26 / ADC0 | U4 INA293B1 laser current amplifier |
| 27 / ADC1 | LT8722 AOUT programmable analog telemetry |
| 28 / ADC2 | LT3571 MON across R15=4.99k: bias load-current telemetry |

U11 REF3030 supplies ADC_VREF: use nominal 3.000 V / 4096 for ADC counts, not
3.3 V. ADC0 current gain needs calibration (R8/R10=10k alter INA293 gain).
ADC2 is neither a bias voltage divider nor threshold monitor. Firmware reports
raw counts to avoid claiming unverified physical-unit conversions.

| Device | 7-bit address | Connection |
|---|---|---|
| U2 AD5693R | 0x4c | I2C0, A0=GND, laser current command |
| U6 AD5693R | 0x4e | I2C0, A0=3.3V, LT3571 CTRL through R22/R23 |
| U16 AD5693R | 0x4e | I2C1, A0=3.3V, comparator threshold |
| U17 MCP9800A | 0x48 expected | I2C1; confirm fitted order-code address |

DAC framing is command 0x30 plus 16-bit big-endian data. Control 0x40/0x0000
selects the internal reference, unity gain and normal mode. Full-scale is nominal
2.5 V; determine safe output ranges and settling on the populated board.

LT8722 EN tied to GND is allowed: ENABLE_REQ activates the device through SPI.
GP17 must control SWEN rather than interpret it as a fault output. Firmware
reads faults from SPIS_STATUS. SPI1 uses mode 0, MSB first, 1 MHz, eight-byte
read/write packets, address shifted left one bit, CRC-8 polynomial 0x07 with seed
0 (Rev B), and ACK 0xa5. Response CRC is checked as well. Soft start ramps the
common-mode DAC for >=5 ms before switching. The commissioning driver uses
nominal +/-0.85 A current limits and a +/-500 mV differential command ceiling;
these must suit the actual TEC and be verified. The DS18B20 laser-diode sensor
now closes the PID loop; see temperature-control.md.

Important existing design concerns: U15's cached symbol has duplicate/nonphysical
pins; R8/R10 affect current calibration; C65 loads the threshold DAC beyond its
unloaded stability specification; U3's VDD bypass is upstream of R6. The current
review also records connector/ground-zone issues. GP4 lacks a demonstrated
external pull-down/independent disable. Resolve or qualify these before operation.
J4/J5 carry bias, power, I2C and LVDS over a proprietary orientation-sensitive
cable. Use only its verified pin map; connect PC USB to the Pico itself.

The originally supplied underscore mini_laser_controller tree contains STM32 laser-current ramp/fault
logic but no LT8722 driver was found in its C/H sources. Its surrounding sibling
project directory contains no accessible C/H implementation either. The firmware
uses an independently implemented Pico LT8722 transport based on the
manufacturer's protocol. The separate hyphenated `laphtec/Laser-Controller`
checkout contains the PID now ported into this project. The crossing reference supplies the ASCII $command,
$ACK/$NACK convention; no UART or RS485 transport is carried over.

Primary references:

- [LT8722 Rev B](https://www.analog.com/media/en/technical-documentation/data-sheets/lt8722.pdf)
- [AD5693R Rev E](https://www.analog.com/media/en/technical-documentation/data-sheets/AD5693R_5692R_5691R_5693.pdf)
- [RP2350 datasheet](https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf)
- Local Pico SDK 2.3.0 hardware_pwm, hardware_pio and hardware_dma headers.
