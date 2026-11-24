"""Small PE32+ reader for static recompilation; never executes the input."""
from pathlib import Path
import struct


class PE64:
    def __init__(self, path):
        self.data = Path(path).read_bytes()
        self._require(0, 64)
        if self.data[:2] != b'MZ':
            raise ValueError('Missing DOS signature')
        pe = self.u32(0x3c)
        self._require(pe, 24)
        if self.data[pe:pe+4] != b'PE\0\0' or self.u16(pe+4) != 0x8664:
            raise ValueError('Expected AMD64 PE')
        n = self.u16(pe+6)
        optional_size = self.u16(pe+20)
        opt = pe+24
        self._require(opt, optional_size)
        if optional_size < 112 or self.u16(opt) != 0x20b:
            raise ValueError('Expected PE32+ optional header')
        self.base = self.u64(opt+24)
        self.entry = self.base+self.u32(opt+16)
        self.size = self.u32(opt+56)
        if not 0 < self.size <= 256*1024*1024:
            raise ValueError('Image too large or empty')
        self.image = bytearray(self.size)
        headers = self.u32(opt+60)
        self._require(0, headers)
        if headers > self.size:
            raise ValueError('Headers exceed image')
        self.image[:headers] = self.data[:headers]
        self.sections = []
        for i in range(n):
            pos = opt+optional_size+40*i
            self._require(pos, 40)
            name = self.data[pos:pos+8].rstrip(b'\0').decode('ascii', 'replace')
            virtual_size, rva, raw_size, raw = struct.unpack_from('<IIII', self.data, pos+8)
            if rva+max(virtual_size, raw_size) > self.size:
                raise ValueError('Section exceeds image')
            self._require(raw, raw_size)
            self.image[rva:rva+raw_size] = self.data[raw:raw+raw_size]
            self.sections.append(dict(name=name, start=self.base+rva,
                                      end=self.base+rva+max(virtual_size, raw_size),
                                      executable=bool(self.u32(pos+36)&0x20000000)))
        count = min(self.u32(opt+108), (optional_size-112)//8)
        self.directories = [struct.unpack_from('<II', self.data, opt+112+8*i) for i in range(count)]
        self.imports = self._imports()
        self.functions = self._unwind_functions()

    def _require(self, pos, size):
        if pos < 0 or size < 0 or pos+size > len(self.data):
            raise ValueError('Truncated PE')

    def u16(self, pos):
        self._require(pos, 2)
        return struct.unpack_from('<H', self.data, pos)[0]

    def u32(self, pos):
        self._require(pos, 4)
        return struct.unpack_from('<I', self.data, pos)[0]

    def u64(self, pos):
        self._require(pos, 8)
        return struct.unpack_from('<Q', self.data, pos)[0]

    def unpack(self, fmt, rva):
        size = struct.calcsize(fmt)
        if rva < 0 or rva+size > len(self.image):
            raise ValueError('RVA outside image')
        return struct.unpack_from(fmt, self.image, rva)

    def string(self, rva):
        if not 0 <= rva < len(self.image):
            raise ValueError('String RVA outside image')
        end = self.image.find(0, rva, min(rva+4096, len(self.image)))
        if end < 0:
            raise ValueError('Unterminated PE string')
        return self.image[rva:end].decode('ascii', 'replace')

    def directory(self, index):
        return self.directories[index] if index < len(self.directories) else (0, 0)

    def _imports(self):
        rva, size = self.directory(1)
        result = {}
        if not rva:
            return result
        for pos in range(rva, rva+size, 20):
            original, stamp, chain, name, iat = self.unpack('<IIIII', pos)
            if not any((original, stamp, chain, name, iat)):
                break
            dll = self.string(name)
            for i in range(self.size//8):
                thunk, = self.unpack('<Q', (original or iat)+8*i)
                if not thunk:
                    break
                symbol = '#'+str(thunk&0xffff) if thunk >> 63 else self.string(thunk+2)
                result[self.base+iat+8*i] = dll+'!'+symbol
            else:
                raise ValueError('Unterminated import table')
        return result

    def _unwind_functions(self):
        rva, size = self.directory(3)
        result = {}
        if rva:
            for pos in range(rva, rva+size-size%12, 12):
                start, end, unwind = self.unpack('<III', pos)
                if start and start < end <= self.size:
                    result[self.base+start] = self.base+end
        return result

    def code(self, address):
        return any(s['executable'] and s['start'] <= address < s['end'] for s in self.sections)

    def read(self, address, size=15):
        off = address-self.base
        if not 0 <= off < self.size:
            return b''
        return bytes(self.image[off:off+size])
