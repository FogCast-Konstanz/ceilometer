import runpy
from pathlib import Path
runpy.run_path(str(Path(__file__).resolve().parents[1]/'software/instrument/tests/test_stream_client.py'),run_name="__main__")
