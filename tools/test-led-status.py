#!/usr/bin/env python3
"""Run the shared native daemon suite; no board access is performed."""
from pathlib import Path
import runpy

runpy.run_path(str(Path(__file__).resolve().parents[1] / "sdk/gpio/tests/run.py"), run_name="__main__")
