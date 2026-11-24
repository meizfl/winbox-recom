#!/usr/bin/env python3
"""Copy the original PE icon group into an ICO container, without executing it."""
from pathlib import Path
import struct, sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from pe64 import PE64
pe = PE64(sys.argv[1])
base, _ = pe.directory(2)
def entries(offset):
    named, ids = struct.unpack_from('<HH', pe.image, base + offset + 12)
    return dict(struct.unpack_from('<II', pe.image, base + offset + 16 + i * 8) for i in range(named + ids))
def resource(kind, number):
    types = entries(0)
    ids = entries(types[kind] & 0x7fffffff)
    languages = entries(ids[number] & 0x7fffffff)
    offset = next(iter(languages.values()))
    rva, size = struct.unpack_from('<II', pe.image, base + offset)
    return bytes(pe.image[rva:rva + size])
group = resource(14, 100)
count = struct.unpack_from('<H', group, 4)[0]
header = bytearray(struct.pack('<HHH', 0, 1, count))
data = bytearray()
for i in range(count):
    record = group[6 + i * 14:20 + i * 14]
    image = resource(3, struct.unpack_from('<H', record, 12)[0])
    header.extend(record[:8] + struct.pack('<II', len(image), 6 + count * 16 + len(data)))
    data.extend(image)
Path(sys.argv[2]).write_bytes(header + data)
