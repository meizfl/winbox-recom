import struct
import sys
from pathlib import Path
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from discover64 import discover

class Image:
    def __init__(self):
        self.image=bytearray(0x3000)
        # Table base in RBP, far ahead of the actual indirect jump.
        code=bytes.fromhex('488d2df90f0000')+b'\x90'*32+bytes.fromhex('31c048634485004801e8ffe0')
        self.image[0x1000:0x1000+len(code)]=code
        self.image[0x1100]=self.image[0x1110]=0xc3
        struct.pack_into('<ii',self.image,0x2000,0x1100-0x2000,0x1110-0x2000)
    def code(self,a):
        return 0x1000<=a<0x1080 or a in (0x1100,0x1110)
    def read(self,a,size=15):
        return bytes(self.image[a:a+size]) if 0<=a<len(self.image) else b''

class DiscoveryTests(unittest.TestCase):
    def test_delayed_relative_jump_table(self):
        instructions,_,failures,_=discover(Image(),[0x1000])
        self.assertIn(0x1100,instructions)
        self.assertIn(0x1110,instructions)
        self.assertFalse(failures)

if __name__=='__main__': unittest.main()
