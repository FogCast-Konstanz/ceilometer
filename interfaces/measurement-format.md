# Recorded measurement format

The instrument writes raw JSON before FFT processing, then a `.profile.json`
record. Records identify boot/measurement IDs, firmware configuration, chip rate,
PRN length, accepted frames/exposure, wall time, dead time, raw uint64 histogram,
UTC receipt and available telemetry. Processing normalizes with accepted exposure,
not wall time. See `software/instrument/src/fogcast/instrument.py` (Recorder) and
`prn_processing.py` for the implementation, and the USB protocol for wire fields.
Live acquisitions belong outside Git; `analysis/bench-tests/legacy` contains the
small preserved commissioning datasets. Future bench runs should record board
revision, firmware commit, configuration, setup and instrument calibration.
