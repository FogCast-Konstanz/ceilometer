import runpy
from pathlib import Path
runpy.run_path(str(Path(__file__).resolve().parents[1]/'deploy/releases/tests/test_release.py'),run_name="__main__")
