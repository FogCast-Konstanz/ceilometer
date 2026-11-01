"""Regenerate offline demo descriptors from the firmware's public C inventories."""
import json
import re
from pathlib import Path

root = Path(__file__).resolve().parents[2]
parameters = [dict(name=n, unit=u, min=int(lo), max=int(hi), default=int(d))
              for n, u, lo, hi, d in re.findall(
                  r'X\((\w+),"([^"]+)",(-?\d+),(-?\d+),(-?\d+)\)',
                  (root / 'firmware/pico2/include/parameters.h').read_text())]
telemetry = [dict(name=n, unit=u) for n, u in re.findall(
    r'X\((\w+),"([^"]+)"\)', (root / 'firmware/pico2/include/telemetry.h').read_text())]
(root / 'software/bringup/demo-metadata.json').write_text(
    json.dumps(dict(parameters=parameters, telemetry=telemetry), indent=2) + '\n')

(root / "software/instrument/src/fogcast/demo-metadata.json").write_text(
    json.dumps(dict(parameters=parameters, telemetry=telemetry), indent=2) + "\n", encoding="utf-8")
