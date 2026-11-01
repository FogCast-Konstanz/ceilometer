# Bias-voltage series at1809.4mV threshold,2026-10-26

11points: nominal overvoltage1..11V, temperature-compensated bias targets
39.736..49.740V. Receiver temperature56.937..57.000C. Physical voltage not measured
simultaneously; reported voltages are calibrated model targets.50Vcommand ceiling
and DAC ceiling60210 remained active. Threshold code47432 (~1809.399mV).
Eachpoint40x50ms locally counted windows=2s live observation, maxpollgap2us.
PRN, laser and GP12testpulse off; mainboard500MHz/1.5V.

OVV,mean targetV,cps:
1,39.73559,0
2,40.73779,0.5
3,41.73779,0
4,42.73559,4.5
5,43.73559,44
6,44.73559,475.5
7,45.73779,3669
8,46.73779,18153
9,47.74000,78516
10,48.73559,252883.5
11,49.73779,658432.5

Biasoff before/after:0counts in2s each. Rate-vs-voltage is strongly nonlinear at
this fixed threshold. This is not a single-photoelectron DCR calibration and
cannot be compared directly with25C datasheet values. Fixed voltage thresholds
can select a changing fraction of pulses as SiPM gain changes with overvoltage.
Zero-point Poisson95% upperbounds ~1.5cps per2s observation.

All outputs stopped; original OV10000mV and threshold47186 restored in RAM;
no settings saved. Rawdata:bias-series-1809p4.csv.
