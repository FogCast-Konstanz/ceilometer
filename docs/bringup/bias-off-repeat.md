# Extended bias-off measurements, 2026-10-26

Bias commanded OFF, bias DAC readback0, laser off, test pulse0; PRN and sync
state machines stopped. Receiver temperature56.75C. Thirteen thresholds,
20locally accumulated50ms windows each:1s total observation per threshold.
get_count USB polling wrap ambiguity avoided by firmware count_window command;
maximum on-device sampling gap2us, well below16-bit wrap interval even250MHz.
No PRN generation or DMA capture occurred during these windows.

Threshold mV -> observed counts/s:
1795,1800,1804,1805,1806:0
1806.5:1289
1807:2684900
1807.5:56973227
1808:29732271
1808.5:1004793
1809:2010
1810,1815:0

This supports a narrow electronic-baseline crossing region and does not establish
avalanche DCR. SiPM avalanches should disappear below breakdown, but comparator
transitions can still be produced by front-end noise/oscillation at its baseline.
Actual APD voltage while bias is disabled is not measured by the PCB; verify with
meter, and scope comparator IN+ and its LVDS output to separate noise/oscillation.
The previous1807mV linear overvoltage fit is contaminated by substantial bias-off
background, so it is not an established single-photoelectron DCR threshold.
Zero in1s is an observation, not proof of identically zero rate (Poisson95% upper
bound approximately3cps if stationarity/Poisson assumptions hold).

All outputs off, threshold restored47186(~1800mV), no Save requested/performed.
Seven host regression suites pass. Data:bias-off-idle-repeat.csv.
Protocol count_window,us accepts1000..100000us, only outside acquisition;
returns total uint64edges, actual elapsed_us, maximum sampling_gap_us. It counts
whatever signal is present without starting any pulse source. Sampling gaps must
be checked against65536/input_rate to exclude ambiguous full counter wraps.
