"""Small RGB/RGBA PNG reader for Cairo-rendering regressions (standard library)."""
import struct,zlib
from pathlib import Path

def read_png(path):
 data=Path(path).read_bytes();assert data[:8]==b'\x89PNG\r\n\x1a\n';offset=8;encoded=bytearray()
 while offset<len(data):
  size=struct.unpack_from('>I',data,offset)[0];kind=data[offset+4:offset+8];chunk=data[offset+8:offset+8+size];offset+=size+12
  if kind==b'IHDR':width,height,depth,color,_,_,interlace=struct.unpack('>IIBBBBB',chunk)
  elif kind==b'IDAT':encoded.extend(chunk)
  elif kind==b'IEND':break
 assert depth==8 and color in (2,6) and interlace==0
 channels=3 if color==2 else 4;stride=width*channels;raw=zlib.decompress(encoded);previous=bytearray(stride);rows=[];offset=0
 for _ in range(height):
  method=raw[offset];row=bytearray(raw[offset+1:offset+1+stride]);offset+=stride+1
  for i in range(stride):
   left=row[i-channels] if i>=channels else 0;up=previous[i];corner=previous[i-channels] if i>=channels else 0
   if method==1:value=left
   elif method==2:value=up
   elif method==3:value=(left+up)//2
   elif method==4:
    p=left+up-corner;dist=(abs(p-left),abs(p-up),abs(p-corner));value=left if dist[0]<=dist[1] and dist[0]<=dist[2] else up if dist[1]<=dist[2] else corner
   else:assert method==0;value=0
   row[i]=(row[i]+value)&255
  rows.append(row);previous=row
 def pixel(x,y):return tuple(rows[y][x*channels:x*channels+3])
 return width,height,pixel
