#!/usr/bin/env python3
"""Give tracked sources stable mtimes so cached C/Ninja objects remain useful."""
import os, subprocess
from pathlib import Path
root = Path(__file__).resolve().parents[1]
history = subprocess.check_output(['git', 'log', '--format=@%ct', '--name-only', '--no-renames'], cwd=root, text=True)
seen = set()
timestamp = None
for line in history.splitlines():
    if line.startswith('@') and line[1:].isdigit():
        timestamp = int(line[1:])
    elif line and line not in seen and timestamp is not None:
        seen.add(line)
        path = root / line
        if path.is_file():
            os.utime(path, (timestamp, timestamp))
