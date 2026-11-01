# Raspberry Pi instrument

Install with `python -m pip install -e software/instrument` from the repository
root. Entry points: `ceilometer`, `ceilometer-dashboard`, `ceilometer-measure`,
and `ceilometer-benchmark`. Module equivalents use `python -m fogcast.<module>`.
The package owns USB transport, safety supervision, recording, PRN processing,
web server and dashboard assets. The simulator uses bundled generated metadata;
`software/bringup/update_metadata.py` regenerates both simulator inventories.
Tests: `python -m unittest discover -s software/instrument/tests`.

On the Pi the systemd unit runs the package from its release directory using
PYTHONPATH. Tokens and device settings stay under /etc/ceilometer; recordings
stay under /var/lib/ceilometer. Neither belongs in a deployment archive or Git.
