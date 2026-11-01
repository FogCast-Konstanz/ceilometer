# Threshold versus overvoltage dark-count sweep, 2026-10-26

Receiver covered per user. Laser and GP12 test pulse disabled. Actual MCP9800
SiPM-board temperature55.625..55.750C, so this is NOT a25C datasheet comparison.
Nominal temperature-compensated OV5..10V;13thresholds1798..1812mV. Each point
uses160PRN frames,16368us effective sampled duration,10MHz DMA bins. Counts summed
over all1023integrated histogram bins. Digital target43.65..48.65V, below50V;
physical output voltage was not measured during this sweep. Normal50V interlock
and calibrated DAC ceiling remained active. Receiver calibration is feedforward.

Linear least-squares fits to6OV points:
| Threshold mV | Rate at5V Mcps | Rate at10V Mcps | Slope Mcps/V | R2 |
|---:|---:|---:|---:|---:|
|1804|0.000611|0.799120|0.149944|0.79645|
|1805|0.079729|5.859787|1.074590|0.83112|
|1806|2.362109|27.396444|5.220566|0.99271|
|1807|27.620235|45.779692|3.570950|0.99710|
|1808|35.243402|49.989492|2.662299|0.92421|
|1809|0.002749|1.111865|0.197762|0.74580|

Bias-disabled background, after2s decay wait, same16368us sampled duration:
1806mV:0counts;1807mV:53723counts=3.282197Mcps;
1808mV:493629counts=30.158175Mcps.
No measured bias voltage confirms complete discharge; this is commanded-bias-off
background, not a provenzero-voltage detector baseline. Count uncertainty and
baseline drift require longer repetitions and independent voltage/scope checks.

1807mV is the strongest mathematical linear fit, but its background is material.
1806mV is a more useful next candidate with near-linear bias-dependent counts
and no counts observed in the short bias-off acquisition. Neither result proves
single-photoelectron DCR; electronics noise/ringing/crosstalk/afterpulsing and
high detector temperature can affect counts. User's25C plot implies roughly
0.75..1.25Mcps at5..11V, slope0.083Mcps/V, unlike these measured curves.
Need actual25C receiver temperature and a calibrated single-photoelectron
threshold/plateau to compare with that curve; do not fake temperature to25C.

Restored OV10000mV and threshold47186 (~1800mV) in RAM, all outputs off.
CSV:dark-overvoltage-sweep.csv; bias-off replies:dark-overvoltage-baseline.txt.
