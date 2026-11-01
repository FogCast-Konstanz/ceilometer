"""Record electrical loopback measurements with GP12 physically wired to GP5.
Disconnect the receiver output before connecting two potential drivers.
No laser/bias/TEC is enabled by this script.
"""
import argparse
import csv
import statistics
from .ceilometer import Ceilometer


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port")
    parser.add_argument("--output", default="benchmark.csv")
    parser.add_argument("--repeats", type=int, default=10)
    args = parser.parse_args()
    device = Ceilometer(args.port)
    try:
        device.command("stop")
        with open(args.output, "w", newline="", encoding="utf-8") as file:
            writer = csv.writer(file)
            writer.writerow(["bin_hz", "pulse_hz", "repeat", "intervals", "observed_edges",
                             "ideal_edges", "edge_error", "min_bin", "max_bin", "mean_bin",
                             "irq_elapsed_us", "tx_starved", "correlation_us"])
            for bin_hz in [1000000, 2000000, 5000000, 10000000]:
                device.command(f"set_rate,{bin_hz}")
                for pulse_hz in [100000, 1000000, 5000000, 10000000, 20000000, 37500000]:
                    actual_hz = int(device.command(f"test_pulse,{pulse_hz}")[0])
                    for repeat in range(args.repeats):
                        state = device.capture(16)
                        samples = device.read_array("raw", 16 * 1023 + 1)
                        bins = [(b-a) & 65535 for a, b in zip(samples, samples[1:])]
                        expected = actual_hz * len(bins) / bin_hz
                        writer.writerow([bin_hz, actual_hz, repeat, len(bins), sum(bins), expected,
                                         sum(bins)-expected, min(bins), max(bins), statistics.mean(bins),
                                         state[3], state[4], state[7]])
                        file.flush()
        print(f"Wrote {args.output}. Counts and elapsed time cannot certify per-bin DMA latency.")
    finally:
        try:
            device.command("test_pulse,0")
            device.command("stop")
        finally:
            device.serial.close()


if __name__ == "__main__":
    main()
