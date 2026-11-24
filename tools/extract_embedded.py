"""Extract the supplied build's three bootstrap PNG resources without running PE."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib
from pe64 import PE64
from lift64 import PROBE_SHA256


def validate_png(data):
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError('Invalid PNG signature')
    pos=8
    dimensions=None
    while pos+12<=len(data):
        size,=struct.unpack_from('>I',data,pos)
        kind=data[pos+4:pos+8]
        end=pos+8+size
        if end+4>len(data):
            raise ValueError('Truncated PNG chunk')
        crc,=struct.unpack_from('>I',data,end)
        if zlib.crc32(data[pos+4:end])!=crc:
            raise ValueError('PNG CRC mismatch')
        if kind==b'IHDR':
            dimensions=struct.unpack_from('>II',data,pos+8)
        if kind==b'IEND':
            if any(data[end+4:]) or dimensions is None:
                raise ValueError('Unexpected PNG tail/header')
            return dimensions, end+4
        pos=end+4
    raise ValueError('Missing PNG end')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary')
    parser.add_argument('--output',default='build/assets')
    args=parser.parse_args()
    pe=PE64(args.binary)
    if hashlib.sha256(pe.data).hexdigest()!=PROBE_SHA256:
        parser.error('Resource table addresses belong to the supplied build only')
    out=Path(args.output); out.mkdir(parents=True,exist_ok=True)
    report=[]
    for index in range(3):
        name=pe.read(0x535204+12*index,12).split(b'\0',1)[0].decode('ascii')
        address,size=struct.unpack('<II',pe.read(0x6381bc+12*index,8))
        data=pe.read(address,size)
        dimensions,png_size=validate_png(data)
        data=data[:png_size]
        (out/name).write_bytes(data)
        report.append(dict(name=name,address=hex(address),embedded_size=size,png_size=png_size,dimensions=dimensions,
                           sha256=hashlib.sha256(data).hexdigest()))
    (out/'manifest.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))


if __name__=='__main__': main()
