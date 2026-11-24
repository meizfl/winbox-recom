#!/usr/bin/env python3
"""Reject non-system dylibs and an accidentally raised deployment target."""
import re, subprocess, sys
binary = sys.argv[1]
arch = sys.argv[2] if len(sys.argv)>2 else 'x86_64'
minimum = sys.argv[3] if len(sys.argv)>3 else '10.14'
assert subprocess.check_output(['lipo', '-archs', binary], text=True).strip() == arch
deps = subprocess.check_output(['otool', '-L', binary], text=True)
for line in deps.splitlines()[1:]:
    library = line.strip().split(' (', 1)[0]
    if not library.startswith(('/usr/lib/', '/System/Library/')):
        raise SystemExit('Non-system dependency: ' + library)
commands = subprocess.check_output(['otool', '-l', binary], text=True)
versions = re.findall(r'\b(?:minos|version) (\d+\.\d+(?:\.\d+)?)', commands)
# Restrict to the OS deployment load command, not dylib version fields.
match = re.search(r'cmd LC_(?:BUILD_VERSION|VERSION_MIN_MACOSX)\b.*?\b(?:minos|version) (\d+\.\d+(?:\.\d+)?)', commands, re.S)
if not match or tuple(map(int, match[1].split('.')[:2])) > tuple(map(int, minimum.split('.')[:2])):
    raise SystemExit('Deployment target exceeds macOS '+minimum+' or is missing')
print(deps, 'Deployment target:', match[1])
