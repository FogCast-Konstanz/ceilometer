# Ceilometer mainboard schematic and PCB review

Rechecked 13 October 2026 using KiCad 10.0.6, with the user's specified **maximum laser current of 1 A** and component values treated as provisional. The latest root schematic, all five subsheets, and combined PCB were checked again through netlist export, ERC, refilled-zone DRC, and schematic/PCB parity. The A/B PCB files have since been removed; their earlier results below are historical. Design files were not edited; zone refills were performed in memory without saving boards.

**Current conclusion:** U17's swapped connections have been corrected. U15's symbol remains malformed; the combined board still reports one J5 shield connection and seven narrow ground-zone connections. The custom cable needs a defined orientation/pin map. R8/R10 and C65 are value-selection concerns, not fixed wiring faults. R9 is not identified as overloaded at 1 A.

## Circuit findings and value-selection notes

### 1. RESOLVED: U17 temperature sensor SDA and SCLK

Location: `receiver.kicad_sch`, U17 (reference property at line 13579); combined PCB U17 at (267, 95.5) mm.

The latest exported netlist and actual combined PCB both connect MCP9800 pin 5 (SDA) to `/Receiver/EXT_SDA2` and pin 4 (SCLK) to `/Receiver/EXT_SCL2`. This matches U16 and resolves the earlier finding.

No further wiring correction is needed for U17.

Evidence: [Microchip MCP9800 datasheet, pin configuration and sections 3.1–3.2](https://ww1.microchip.com/downloads/aemDocuments/documents/OTH/ProductDocuments/DataSheets/21909d.pdf).

### 2. [P2] U3 supply bypass is upstream of its series resistor

Location: `laser_driver.kicad_sch`, R6 at line 7802, C6 at line 8063, U3 at line 4464. Combined PCB U3 at (204.55, 109.35) mm; R6 at (208.75, 109.35) mm.

U3 pin 18 is supplied through R6 = 1 ohm. The downstream net `Net-(U3-VDD)` contains only R6 and U3 pin 18: there is no capacitor to ground. C6 = 1 uF is on the upstream +3.3VD net. If a nonzero series resistor is retained, place bypass capacitance on the IC side to supply switching transients. The 1 A laser-current limit does not establish U3's VDD transient current; the two current paths are distinct. This is a supply-integrity concern, not proof of an observed malfunction.

Fix: move C6 or add suitable local bypass capacitance from pin 18 to U3 GND on the downstream side of R6, with short connections. If R6 becomes a zero-ohm link, reevaluate the existing capacitor's placement and supply path. Follow the manufacturer's bypass guidance and confirm VDD at the intended switching rate.

Evidence: [iC-Haus iC-HG30 product and datasheet](https://www.ichaus.de/product/ic-hg30/), application circuits with RVDD and CVDD1–3.

### 3. VALUE SELECTION: R8/R10 alter U4's current-sense gain

Location: `laser_driver.kicad_sch`, R8 at line 9379 and R10 at line 5985. Combined PCB U4 at (203.05, 119.4) mm.

Both INA293B1 inputs have 10 kohm series resistors. TI recommends input filter resistors of 10 ohms or less. These resistors interact with the amplifier's internal input/bias network; matching them does not preserve the specified gain. Applying TI's typical DC model for the gain-20 version gives a gain factor of approximately 0.226, or about 77% attenuation relative to nominal, before other errors. This is a model estimate, not a measured result. Firmware assuming 20 V/V would substantially under-report laser current.

This can be addressed through component selection without changing the basic wiring. At 1 A and R9 = 40 milliohms, the nominal gain-20 output is 0.8 V; with the present 10 kohm resistors, the same typical model predicts about 0.181 V before offset and other errors. Select small input resistors and suitable capacitance, or place averaging after the amplifier. Verify the complete measurement transfer function and required bandwidth before freezing values.

Evidence: [TI INA293 datasheet, sections 7.3.1.4 and 8.1.3, equation 5](https://www.ti.com/lit/ds/symlink/ina293.pdf).

### 4. [P2] U15 symbol has duplicate and nonexistent pins

Location: `receiver.kicad_sch`, U15 at line 11581; cached symbol around lines 503 and 613. Also `symbols/Comparror_usr.kicad_sym`. Combined PCB U15 at (254, 93.5) mm.

The TLV3801 symbol defines pin 5 twice, as both IN+ and hidden NC, and includes pin 11 (NC) and pin 13 (VEE). Its physical footprint has pads 1–8 and thermal pad 9. ERC reports the duplicate pin; schematic parity reports missing pads 11 and 13.

The existing physical pads 1–8 and pad 9 have the expected net assignments, so this is a symbol integrity error rather than proof that the comparator currently has a wrong physical pin connection. It nevertheless makes updates and automated verification unreliable.

Fix: rebuild the symbol for the 8-pin DSG TLV3801, retain a single IN+ pin 5, remove fictitious pins, and represent the exposed pad consistently as pad 9 tied to VEE/GND. Update both the library and cached schematic symbol, then recheck parity.

Evidence: [TI TLV3801/3802 datasheet, section 5](https://www.ti.com/lit/gpn/TLV3802).

### 5. INTERFACE REQUIREMENT: J4/J5 plug orientation and cable type

Location: `receiver.kicad_sch`, J4 at line 9692 and J5 at line 9519. Combined PCB J4 at (230, 95.5) mm and J5 at (246, 95.5) mm.

The boards deliberately use USB-C for a proprietary link, as their “NO USB!” markings indicate. However, only one set of SuperSpeed contacts is connected on each receptacle, and SiPM bias is connected only to A6; B6 is unused. Flipping a plug can therefore disconnect the bias and/or the active signal lanes. A USB 2-only cable also lacks the SuperSpeed wires carrying I2C and LVDS here.

This is acceptable if the proprietary interface intentionally uses a verified cable and fixed plug orientations. Specify and verify the complete cable pin map and permitted orientations. Mark the required plug orientations and require a verified full-featured passive cable. If arbitrary plug orientation is required, the present wiring needs modification. Do not simply parallel the differential lane sets without considering signal integrity.

Evidence: [USB-IF Type-C cable and connector specification](https://www.usb.org/sites/default/files/USB%20Type-C%20Spec%20R2.0%20-%20August%202019.pdf), receptacle/plug pin assignments and cable wiring. This finding concerns the intended proprietary connection, not missing USB protocol circuitry.

### 6. VALUE SELECTION: U16 output capacitance

Location: `receiver.kicad_sch`, U16 at line 8002 and C65 at line 13442.

C65 = 8.2 nF connects directly from U16 VOUT (`/Receiver/V_TRSH`) to ground. Its other load is U15's high-impedance input. AD5693R specifies 2 nF capacitive-load stability for an unloaded output; the larger 10 nF capability is specified with a 2 kohm load. This circuit has neither that load nor an isolation resistor, so output stability and threshold settling are not assured.

Selecting a smaller C65 can address this without changing the wiring. Reduce direct capacitance to within the stated limit or use the datasheet's output isolation/snubber approach. Check settling and oscillation while stepping the threshold over its operating range. Review U2/C8 separately against the combined CI input loading.

Evidence: [Analog Devices AD5693R datasheet, output characteristics and Output Buffer discussion](https://www.analog.com/media/en/technical-documentation/data-sheets/AD5693R_5692R_5691R_5693.pdf).

## Layout and release findings

### 7. [P2] Combined board reports an unconnected J5 shield pad

The repeated, refilled-zone combined-board DRC still reports J5 SH at (243.16, 99.99) mm as missing its GND connection. The previous separated B board reported zero unconnected items, but that file is no longer present. This is the current combined-board check result; it does not prove an open on a future separated manufacturing board.

Fix: inspect the shield copper on both files and make the intended connection explicit in the source; regenerate and verify the separated boards. If only A/B are manufactured, prioritize their verified connectivity.

### 8. [P2] Seven ground-zone connections are narrower than the configured minimum

Combined and A-board DRC report seven GND zone necks on F.Cu, In1.Cu, and In2.Cu, measuring approximately 0.099–0.1054 mm against a 0.125 mm minimum. B has no such warnings. These can be manufacturing weaknesses and unreliable return-path bottlenecks.

Fix: inspect the highlighted zone necks, adjust nearby clearances/routing, and refill. The report gives zone-level coordinates rather than exact neck locations, so locate them using KiCad's DRC markers.

Silkscreen also overlaps exposed copper: six findings on A and six on B, plus one silk overlap on A. B's findings involve the U12/U13 footprint outlines over pads. Fix the library outlines or confirm the plotting process removes silk from mask openings.

## Additional checks before release

- **Laser shunt at the specified 1 A maximum:** R9 = 40 milliohms produces a 40 mV drop and 40 mW dissipation at continuous 1 A; pulsed average dissipation is lower. No overload finding is justified from these numbers alone. Confirm the selected resistor's rating and tolerance when choosing the final BOM. The previous 30 A example has been removed as irrelevant to this design. Keep the pulse loop compact and verify ringing with the real diode/cable. The laser-current nets use zones as well as 0.25/0.5 mm track segments; track width alone is not evidence that all current passes through a thin trace. The AGND shunt introduces only 40 mV resistive lift at 1 A, plus switching-induced voltage that requires measurement.
- **LVDS impedance and skew:** the project has no custom impedance/skew constraints in its `.kicad_dru`. Geometric DRC consequently does not certify the PULSE pairs' impedance or timing. Verify the intended 100-ohm differential path against the fabricated stackup and include the cable. This is a validation gap, not a confirmed impedance error.
- **SiPM bias voltage ratings:** verify the actual bias range and voltage ratings/DC-bias derating of C19/C20 and C47–C52, plus connector/cable insulation. Values and footprint sizes alone do not establish those ratings.
- **Library reproducibility:** `sym-lib-table` declares RF_Amplifier_usr with an invalid-looking type string (`Unknown SCH_FILE_T value: 12`); `fp-lib-table` contains Analog_Devices_usr twice. Package_SON_usr is referenced but absent from the local footprint table. Repair these and provision the required standard libraries before relying on library comparison warnings.
- **U3 footprint differs from its local library:** DRC identifies this mismatch. Compare the changes before updating from the library, so a deliberate thermal/pad customization is not lost.

## Check results and limitations

| File/check | Result |
|---|---|
| Root schematic ERC, all severities | 1,254 reported items |
| Latest combined PCB DRC, refilled | 158 general violations, 1 unconnected item, 2 parity findings |
| Previous A PCB DRC, historical | 112 general violations, 0 unconnected items |
| Previous B PCB DRC, historical | 46 general violations, 0 unconnected items |

Most ERC reports are off-grid endpoints (821), symbol/library issues (258), and footprint link issues (135). Remaining categories include 29 unconnected pins, five pin-type conflicts, five undriven power pins, and the U15 duplicate-pin error. Many unconnected pins are unused module/connector contacts; many power conflicts come from incorrect custom pin electrical types or intentionally paralleled power pins. Do not interpret the raw ERC count as 1,254 functional faults.

The combined DRC has 137 library lookup warnings; the historical A/B results have 97 and 40 respectively. The isolated review configuration lacks the user's global standard-library tables. Those lookup warnings do not prove that the embedded PCB footprints are missing or physically wrong. No copper short, ordinary clearance, or outline errors were reported under the current project rules. The two current parity findings concern U15's fictitious pins. The earlier R36/U18 metadata differences have been resolved.

No bench measurements, thermal/pulse simulation, complete BOM rating audit, or cable continuity test was performed. Original design files were preserved. Reports are `erc.json`, `drc.json`, `drc-A.json`, `drc-B.json`; `connectivity.txt` contains the extracted pin-to-net connections. The rendered copper views show the saved board's fills, whereas DRC used freshly computed fills in memory.
