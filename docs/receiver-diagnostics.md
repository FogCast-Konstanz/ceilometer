# Receiver temperature diagnosis, 2026-10-25

Hardware: Pico 2 RP2350 A2, serial 2ED26056C38A563E, USB CDC COM19.
Firmware 0.4.1 was built, programmed and flash-verified. All compile-time power
enables remain OFF, laser/bias ceilings zero. No laser, TEC or bias was enabled.

Original firmware 0.4.0 readings: laser DS18B20 valid at about 23.2 C; mainboard
laser/bias DAC readbacks valid; LT8722 register read valid; VSYS approximately
4.691 V. Receiver MCP9800 configuration/temperature and U16 threshold DAC
readbacks all invalid. This is a shared receiver-bus problem, not a Celsius
conversion or temperature-window rejection. VSYS does not measure receiver VDD.

The schematic specifies MCP9800A0T-M/OT U17 at 7-bit address 0x48, SOT23-5:
pin 1=+3.3VA2, pin 2=GND, pin 4=SCLK, pin 5=SDA. U16 DAC shares the bus at 0x4e.
The firmware uses I2C1 GP2=SDA and GP3=SCL, 100 kHz, matching the schematic.

Final measured read-only scan results after explicit bus recovery:

| Bus | SDA/SCL before and after | Timeouts | NACK/error | Responding addresses |
|---|---|---:|---:|---|
| I2C0 mainboard | 1/1 | 0 | 110 | 0x4c, 0x4e |
| I2C1 receiver | 1/1 | 0 | 112 | none |

Both mainboard DAC readbacks were valid again after recovery. Receiver U17 and
U16 remained absent. MCU-side pullups can hold disconnected receiver lines high;
high idle levels alone do not prove cable continuity or receiver power.

The user identified the interconnect as UGREEN US535. UGREEN's part-number
15311/90440 product specification identifies USB 2.0 data capability. This is
incompatible with the current schematic's use of SuperSpeed contacts for I2C:

| Signal | Mainboard J4 | Receiver J5 | MCU / sensor |
|---|---|---|---|
| SDA | B2 (TX2+) | A11 (RX2+) | GP2 / U17 pin 5 |
| SCL | B3 (TX2-) | A10 (RX2-) | GP3 / U17 pin 4 |

A USB 2.0 C-to-C cable can carry receiver power while omitting these conductors.
This explains the simultaneous loss of receiver temperature and threshold DAC.
Replace it with a full-featured passive SuperSpeed cable, verifying the actual
contact mapping and connector orientation against this proprietary board link.
This is not standard USB signalling; avoid active/redriver cables. Power down
before altering this receiver interconnect, which also carries detector bias.
Correct operation with a replacement cable is not yet verified.

With a verified cable, expect I2C1 scan addresses 72/78 (0x48/0x4e), valid DAC2
readback and receiver temperature after conversion warmup. If still absent,
measure +5V2, U17 pin 1 to pin 2 (nominal 3.3 V), and continuity of the two routes
above; R34/R35 are 4.7 kohm pullups to +3.3VA2. Do not change the sensor address
merely to hide this bus failure.

Diagnostic implementation: `$i2c_scan,0_or_1` uses bounded two-byte read probes;
the GUI exposes both scans. `$i2c_recover,0_or_1` is an explicit bounded
open-drain nine-clock/STOP bus-clear command. Both reject active outputs or
acquisition. Early one-byte probes left a mainboard DAC mid-read, holding SDA low
across MCU resets; two-byte probes with STOP settling plus bus recovery restored
communication. No persistent DAC target or parameter was changed.

References:
- Committed hardware-netlist.xml and schematic pin mapping.
- [UGREEN 15311/90440 specification](https://ugreen.jp/products/ugreen-usb-c%E3%82%B1%E3%83%BC%E3%83%96%E3%83%AB%E8%B6%85%E6%80%A5%E9%80%9F%E5%85%85%E9%9B%BB-pd3-1-240w).
- [UGREEN declaration identifying US535 / SKU 15311](https://www.hellasdigital.gr/index.php?attachment_id=3325&dispatch=attachments.getfile).
- [MCP9800 data sheet](https://ww1.microchip.com/downloads/aemDocuments/documents/OTH/ProductDocuments/DataSheets/21909d.pdf).
- [AD5693R full read sequence](https://www.analog.com/media/en/technical-documentation/data-sheets/ad5693r_5692r_5691r_5693.pdf).
- [NXP I2C bus-clear procedure](https://www.nxp.jp/docs/en/user-guide/UM10204.pdf).
