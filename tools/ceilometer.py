"""USB CDC client. Install pyserial; no UART transport or baud timing assumptions."""
import argparse
import csv
import json
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
        wire=("$"+command.lstrip("$")).encode("ascii")
        if len(wire)>127:
            raise ValueError("Request exceeds firmware's 127-character line limit")
        self.serial.write(wire+b"\n")
        line = self.serial.readline().decode("ascii").strip()
        fields = line.split(",")
        name = command.lstrip("$").split(",")[0]
        if len(fields) < 2 or fields[1] != name:
            raise RuntimeError(f"Incomplete/unexpected response: {line!r}")
        if fields[0] != "$ACK":
            raise RuntimeError(line)
        return fields[2:]

    def parameters(self):
        count=int(self.command("parameter_count")[0])
        result={}
        for index in range(count):
            info=self.command(f"parameter_info,{index}")
            value=self.command(f"get_param,{info[1]}")
            result[info[1]]={"value":int(value[1]),"unit":value[2],
                             "min":int(info[3]),"max":int(info[4]),"default":int(info[5])}
        return result

    def set_parameters(self, settings, save=False):
        fields=[]
        for name,value in settings.items():
            fields.extend([name,str(int(value))])
        self.command("set_params,"+",".join(fields))
        if save:
            self.command("save")

    def telemetry(self):
        count=int(self.command("telemetry_count")[0])
        result={}
        for index in range(count):
            value=self.command(f"get_telemetry,{index}")
            result[value[0]]={"unit":value[1],"value":int(value[2]),"valid":bool(int(value[3])),
                              "sampled_us":int(value[4]),"age_us":int(value[5]),"sequence":int(value[6])}
        return result

    def capture(self, frames=None, laser=False, timeout=180):
        name="start" if laser else "capture"
        self.command(name+(f",{frames}" if frames is not None else ""))
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
    sub.add_parser("parameters")
    sub.add_parser("telemetry")
    setting=sub.add_parser("set")
    setting.add_argument("settings",nargs="+",help="name=value; up to eight atomic changes within 127 wire characters")
    setting.add_argument("--save",action="store_true")
    capture = sub.add_parser("capture")
    capture.add_argument("--frames", type=int, default=None, help="uses persisted integration_frames if omitted")
    capture.add_argument("--output", default="profile.csv")
    capture.add_argument("--offset-bins", type=int, default=None)
    args = parser.parse_args()
    instrument = Ceilometer(args.port)
    try:
        if args.action == "command":
            print(instrument.command(args.text))
        elif args.action == "parameters":
            print(json.dumps(instrument.parameters(),indent=2))
        elif args.action == "telemetry":
            print(json.dumps(instrument.telemetry(),indent=2))
        elif args.action == "set":
            settings={}
            for setting in args.settings:
                name,separator,value=setting.partition("=")
                if not separator or not name:
                    raise ValueError("Settings must have the form name=value")
                settings[name]=int(value)
            instrument.set_parameters(settings,save=args.save)
            print(instrument.command("get_storage"))
        else:
            info = instrument.command("get_info")
            chip_hz = int(info[4])
            offset=args.offset_bins
            if offset is None:
                offset=int(instrument.command("get_param,range_offset_bins")[1])
            group=int(instrument.command("get_param,range_average_bins")[1])
            state = instrument.capture(args.frames)
            histogram = instrument.read_array("histogram")
            profile = instrument.read_array("profile")
            with open(args.output, "w", newline="", encoding="utf-8") as file:
                writer = csv.writer(file)
                writer.writerow(["lag", "uncalibrated_range_m", "counts", "correlation_scaled_1023", "frames"])
                for lag in range(0,len(histogram),group):
                    distance=(lag-offset)*299792458/(2*chip_hz)
                    counts=sum(histogram[lag:lag+group])
                    values=profile[lag:lag+group]
                    correlation=sum(values)/len(values)
                    writer.writerow([lag,distance,counts,correlation,state[1]])
            print(f"Wrote {args.output}; hardware timing qualification is required")
    finally:
        # Close every session with all power outputs disabled, including on exceptions.
        try:
            instrument.command("stop")
        finally:
            instrument.serial.close()


if __name__ == "__main__":
    main()
