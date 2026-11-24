#!/usr/bin/env python3
"""Reject non-system dylibs and an accidentally raised deployment target."""
import re, subprocess, sys
binary = sys.argv[1]
assert subprocess.check_output(['lipo', '-archs', binary], text=True).strip() == 'x86_64'
deps = subprocess.check_output(['otool', '-L', binary], text=True)
for line in deps.splitlines()[1:]:
    library = line.strip().split(' (', 1)[0]
    if not library.startswith(('/usr/lib/', '/System/Library/')):
        raise SystemExit('Non-system dependency: ' + library)
commands = subprocess.check_output(['otool', '-l', binary], text=True)
versions = re.findall(r'\b(?:minos|version) (\d+\.\d+(?:\.\d+)?)', commands)
# Restrict to the OS deployment load command, not dylib version fields.
match = re.search(r'cmd LC_(?:BUILD_VERSION|VERSION_MIN_MACOSX)\b.*?\b(?:minos|version) (\d+\.\d+(?:\.\d+)?)', commands, re.S)
if not match or tuple(map(int, match[1].split('.')[:2])) > (10, 14):
    raise SystemExit('Deployment target exceeds macOS 10.14 or is missing')
print(deps, 'Deployment target:', match[1])
