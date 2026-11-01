# Operating clock and PRN timing

Firmware 0.5.0 changes the timing architecture: PWM3A/GP6 is the master clock,
its wrap DREQ paces receiver DMA, and PIO waits for its rising edge before PRN
output. PIO runs at clk_sys with a counted chip delay, removing divided-SM start phase. See continuous-acquisition.md.
The measurements below qualify only the previous finite-burst implementation.

## Historical validation, 2026-10-26

The operating build now defaults to500MHz clk_sys and requests1.5V core voltage.
The mainboard was left running this configuration, with laser/TEC/bias OFF.
Normal startup asserts safe GPIO levels, sets the regulator, waits20ms, then
selects500MHz. RAM execution matches the prior diagnostic; high-voltage operation
remains outside manufacturer-rated conditions. No long-term stability guarantee.

PRN PIO: OUT[4]+NOP[4]=10PIO cycles per symbol. Integer SM divider5 at500MHz
produces exactly10MHz symbols with50system cycles/symbol and equal5cycle sync
high/low phases. Frame sync uses1023*10cycles at the same divider. PWM0 DMA pacing
uses wrap49 at divider1. Parameter validation and demo use500MHz/10cycle rules.

Boot-stage flash divider10 plus explicit runtime QMI divider10 keep flash reads
at50MHz. The runtime setting is necessary for RAM boot, where changing only the
boot-stage definition did not correctly constrain reads. Journal reads use the
uncached alias. Flash program/erase temporarily lower clk_sys to150MHz with all
power outputs idle, retain1.5V, then explicitly restore divider10 and500MHz.
Real modified-settings Save verified generation22 and23 and clean readback.
Original telemetry period100ms restored; bias calibration989241/-4925156 retained.

Hardware validation: get_clock500000000,500000,18. Twenty captures of16frames at
10MHz all READY with zero transmitter starvation; burst elapsed1637..1638us,
matching1636.8us nominal plus setup/IRQ latency. Correlation completed around44ms.
Both laser and receiver temperature readings remained valid. All7host suites pass.
Data: production-overclock-results.txt. These were dark digital acquisition tests,
not an optical ranging or independent oscilloscope timing calibration.

This supersedes the earlier diagnostic-only clock policy in mainboard-overclock.md
and the old15cycles/chip,150MHz timing assumptions in earlier project notes.


## Pi USB/watchdog investigation, 2026-10-31

The bare Pico D5E72DD9B609A5A9 repeatedly disappeared and re-enumerated while
firmware 0.6.0/0.6.1 was disabled. Pi undervoltage flags were zero, autosuspend
was off, and the dashboard was the sole serial owner. Runtime diagnostics
confirmed changed Pico boot IDs and watchdog resets, rather than a browser-only
status issue. A retained stage marker in 0.6.2 identified a reset in stage 8,
the idle `sleep_us(100)` alarm/WFE path. `get_clock` returned 500000000 Hz,
frequency counter 500000 kHz and VREG selection 18 (requested 1.50 V; not an
analog voltage measurement).

Firmware 0.6.3 replaces that idle sleep with `busy_wait_us_32(100)` while leaving
interrupts enabled. It retains boot/uptime/watchdog/stage diagnostics and Pi
journal messages for disconnects and changed boot IDs. Stages are 1 loop/watchdog,
2 thermal, 3 bias, 4 active ADC, 5 telemetry, 6 acquisition, 7 commands, 8 idle,
10 boot initialization. These identify the last foreground stage; interrupts
can also execute while a stage is marked. This evidence locates the observed
stall but does not establish the physical cause or qualify acquisition timing.
The system remains 500 MHz/requested 1.5 V. The deployment-only enable inhibit
has been cleared at the user's request; explicit Enable and all firmware
interlocks remain required. Unique-Pico discovery supports swapping the bare
board for the production mainboard; no automatic measurement restart occurs.

Post-change observation: 180 dashboard status samples over 184.24 s, all
connected and disabled, zero USB disconnects, zero detected Pico reboots, one
boot ID and monotonically increasing uptime. See usb-idle-0.6.3-results.json.
This is a short idle regression observation, not a guarantee of long-term or
acquisition stability. Repeated stage-8 watchdog resets were observed before
the idle-wait replacement; none occurred during this observation afterward.
