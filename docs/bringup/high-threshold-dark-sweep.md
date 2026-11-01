# Dark counts above1809mV,2026-10-26

User measured residual APD bias3.1V with supply disabled. Measured13thresholds,
biasoff then biason at10V nominal overvoltage, receiver about56.8C. Eachpoint
20x50ms on-board counter windows=1second, no PRN/acquisition/testpulse/laser.
Max observed polling gap2us avoids16bit-wrap ambiguity at admissible input rates.
No electronic background subtraction; threshold voltages are nominal DAC values.

|Threshold mV|Biasoff cps|Biason cps|
|---:|---:|---:|
|1809.1|255|986660|
|1809.2|126|544339|
|1809.4|0|222364|
|1809.6|0|71691|
|1809.8|0|27136|
|1810|0|10308|
|1810.2|0|3330|
|1810.5|0|606|
|1811|0|23|
|1812,1815,1820,1850|0|0|

1809.4mV is the lowest tested point in this sweep with no biasoff counts and a
strong bias-dependent signal. It is a candidate for further characterization,
not a validated single-photoelectron DCR threshold. There is strong threshold
dependence and no demonstrated single-photoelectron count plateau. Zero counts
in1s gives approximately3cps upper95% Poisson bound, not proof of identically0.
Physical APD voltage was not remeasured duringbiason;50Vcommand ceiling active.
Alloutputs off and originalthreshold47186(~1800mV) restored aftermeasurement.
Rawdata:high-threshold-dark-sweep.csv.
