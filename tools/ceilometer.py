"""USB CDC client. Install pyserial; no UART transport or baud timing assumptions."""
import argparse
import csv
import time
import serial


class Ceilometer:
    def __init__(self, port):
        self.serial = serial.Serial(port, 115200, timeout=1, write_timeout=1)
        time.sleep(0.2)
        self.serial.reset_input_buffer()

    def command(self, command):
        if "\n" in command or "\r" in command:
            raise ValueError("one command per request")
        self.serial.write(("$" + command.lstrip("$") + "\n").encode("ascii"))
        line = self.serial.readline().decode("ascii").strip()
        fields = line.split(",")
        name = command.lstrip("$").split(",")[0]
        if len(fields) < 2 or fields[1] != name:
            raise RuntimeError(f"Incomplete/unexpected response: {line!r}")
        if fields[0] != "$ACK":
            raise RuntimeError(line)
        return fields[2:]

    def capture(self, frames, laser=False, timeout=180):
        self.command(f"{'start' if laser else 'capture'},{frames}")
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            state = self.command("get_state")  # also renews two-second lease
            if state[0] == "READY":
                return state
            if state[0] == "FAULT":
                raise RuntimeError("Device fault; inspect hardware and clear explicitly")
            time.sleep(0.05)
        self.command("stop")
        raise TimeoutError("Acquisition timed out")

    def read_array(self, command, length=1023):
        values = []
        for offset in range(0, length, 16):
            response = self.command(f"{command},{offset},{min(16, length-offset)}")
            if int(response[0]) != offset:
                raise RuntimeError("Page offset mismatch")
            page = [int(v) for v in response[1:]]
            if len(page) != min(16, length-offset):
                raise RuntimeError("Incomplete data page")
            values.extend(page)
        return values


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", help="USB CDC port, e.g. COM7 or /dev/ttyACM0")
    sub = parser.add_subparsers(dest="action", required=True)
    command = sub.add_parser("command")
    command.add_argument("text", help="command without $, e.g. get_info")
    capture = sub.add_parser("capture")
    capture.add_argument("--frames", type=int, default=16)
    capture.add_argument("--output", default="profile.csv")
    capture.add_argument("--offset-bins", type=int, default=0)
    args = parser.parse_args()
    instrument = Ceilometer(args.port)
    try:
        if args.action == "command":
            print(instrument.command(args.text))
        else:
            info = instrument.command("get_info")
            chip_hz = int(info[4])
            state = instrument.capture(args.frames)
            histogram = instrument.read_array("histogram")
            profile = instrument.read_array("profile")
            with open(args.output, "w", newline="", encoding="utf-8") as file:
                writer = csv.writer(file)
                writer.writerow(["lag", "uncalibrated_range_m", "counts", "correlation_scaled_1023", "frames"])
                for lag, (counts, correlation) in enumerate(zip(histogram, profile)):
                    distance = (lag - args.offset_bins) * 299792458 / (2 * chip_hz)
                    writer.writerow([lag, distance, counts, correlation, state[1]])
            print(f"Wrote {args.output}; hardware timing qualification is required")
    finally:
        # Close every session with all power outputs disabled, including on exceptions.
        try:
            instrument.command("stop")
        finally:
            instrument.serial.close()


if __name__ == "__main__":
    main()
