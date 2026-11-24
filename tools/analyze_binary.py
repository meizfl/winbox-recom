#!/usr/bin/env python3
"""Extract PE metadata and Windows imports without executing the binary."""
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

binary = Path(sys.argv[1] if len(sys.argv) > 1 else "winbox64.v3.43p10.exe")
output = Path(sys.argv[2] if len(sys.argv) > 2 else "analysis")
output.mkdir(parents=True, exist_ok=True)
headers = subprocess.run(["objdump", "-p", str(binary)], check=True, capture_output=True, text=True).stdout
sections = subprocess.run(["objdump", "-h", str(binary)], check=True, capture_output=True, text=True).stdout
imports_text = headers.split("The Import Tables", 1)[1].split("The Export Tables", 1)[0]
imports = {}
current = None
for line in imports_text.splitlines():
    match = re.search(r"DLL Name: (\S+)", line)
    if match:
        current = match.group(1)
        imports[current] = []
    elif current:
        match = re.search(r"<none>\s+[0-9a-fA-F]+\s+(\S+)", line)
        if match:
            imports[current].append(match.group(1))
report = {"binary": binary.name, "sha256": hashlib.sha256(binary.read_bytes()).hexdigest(), "imports": imports}
(output / "imports.json").write_text(json.dumps(report, indent=2) + "\n")
(output / "sections.txt").write_text(sections)
print(f"Extracted {sum(map(len, imports.values()))} named imports from {len(imports)} DLLs")
