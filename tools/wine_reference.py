#!/usr/bin/env python3
"""Operate only the isolated reference X display; requires explicit DISPLAY/XAUTHORITY."""
import argparse,ctypes as C,os,subprocess,time
from pathlib import Path
parser=argparse.ArgumentParser();parser.add_argument('action',choices=['list','capture','click','type','key']);parser.add_argument('--window',type=lambda x:int(x,0));parser.add_argument('--output',type=Path);parser.add_argument('--x',type=int,default=0);parser.add_argument('--y',type=int,default=0);parser.add_argument('--text-file',type=Path);parser.add_argument('--text');parser.add_argument('--key',default='Return');parser.add_argument('--ctrl',action='store_true');args=parser.parse_args()
if os.environ.get('DISPLAY')!=':91':raise SystemExit('Reference automation is restricted to the hidden :91 display')
x=C.CDLL('libX11.so.6');t=C.CDLL('libXtst.so.6');D=C.c_void_p;W=C.c_ulong
x.XOpenDisplay.argtypes=[C.c_char_p];x.XOpenDisplay.restype=D
x.XDefaultRootWindow.argtypes=[D];x.XDefaultRootWindow.restype=W
x.XQueryTree.argtypes=[D,W,C.POINTER(W),C.POINTER(W),C.POINTER(C.POINTER(W)),C.POINTER(C.c_uint)];x.XQueryTree.restype=C.c_int
x.XFetchName.argtypes=[D,W,C.POINTER(C.c_void_p)];x.XFetchName.restype=C.c_int
x.XFree.argtypes=[C.c_void_p];x.XTranslateCoordinates.argtypes=[D,W,W,C.c_int,C.c_int,C.POINTER(C.c_int),C.POINTER(C.c_int),C.POINTER(W)]
x.XRaiseWindow.argtypes=[D,W];x.XSetInputFocus.argtypes=[D,W,C.c_int,W];x.XFlush.argtypes=[D];x.XSync.argtypes=[D,C.c_int];x.XStringToKeysym.argtypes=[C.c_char_p];x.XStringToKeysym.restype=W;x.XKeysymToKeycode.argtypes=[D,W];x.XKeysymToKeycode.restype=C.c_ubyte;x.XKeycodeToKeysym.argtypes=[D,C.c_ubyte,C.c_int];x.XKeycodeToKeysym.restype=W
for name,types in [('XTestFakeMotionEvent',[D,C.c_int,C.c_int,C.c_int,W]),('XTestFakeButtonEvent',[D,C.c_uint,C.c_int,W]),('XTestFakeKeyEvent',[D,C.c_uint,C.c_int,W])]:getattr(t,name).argtypes=types
connection=x.XOpenDisplay(None)
if not connection:raise SystemExit('Cannot open isolated X display')
root=x.XDefaultRootWindow(connection)
def tree(window,depth=0):
 name=C.c_void_p();x.XFetchName(connection,window,C.byref(name));label=C.string_at(name).decode(errors='replace') if name.value else '';x.XFree(name)
 if label:print(hex(window),repr(label),flush=True)
 r=W();parent=W();children=C.POINTER(W)();count=C.c_uint()
 if depth<12 and x.XQueryTree(connection,window,C.byref(r),C.byref(parent),C.byref(children),C.byref(count)):
  for child in list(children[:count.value]):tree(child,depth+1)
  x.XFree(children)
def key(name,down):
 symbol=x.XStringToKeysym(name.encode());code=x.XKeysymToKeycode(connection,symbol)
 if not code:raise ValueError('Unmapped key')
 t.XTestFakeKeyEvent(connection,code,int(down),0)
if args.action=='list':tree(root)
else:
 if args.action=='capture':
  if not args.output:raise SystemExit('--output is required')
  args.output.parent.mkdir(parents=True,exist_ok=True);subprocess.run(['import','-silent','-window',hex(args.window or root),str(args.output)],check=True)
 else:
  if args.window:x.XRaiseWindow(connection,args.window);x.XSetInputFocus(connection,args.window,2,0)
  if args.action=='click':
   px=C.c_int();py=C.c_int();child=W();x.XTranslateCoordinates(connection,args.window or root,root,args.x,args.y,C.byref(px),C.byref(py),C.byref(child));t.XTestFakeMotionEvent(connection,-1,px.value,py.value,0);t.XTestFakeButtonEvent(connection,1,1,0);t.XTestFakeButtonEvent(connection,1,0,0)
  elif args.action=='key':
   if args.ctrl:key('Control_L',True)
   key(args.key,True);key(args.key,False)
   if args.ctrl:key('Control_L',False)
  elif args.action=='type':
   value=args.text_file.read_text().strip() if args.text_file else args.text or ''
   for char in value:
    symbol=ord(char) if ord(char)<128 else 0x1000000|ord(char);code=x.XKeysymToKeycode(connection,symbol)
    if not code:raise ValueError('Only mapped reference-input characters are supported')
    shifted=x.XKeycodeToKeysym(connection,code,0)!=symbol
    if shifted:key('Shift_L',True)
    t.XTestFakeKeyEvent(connection,code,1,0);t.XTestFakeKeyEvent(connection,code,0,0)
    if shifted:key('Shift_L',False)
    x.XFlush(connection);time.sleep(.015)
  x.XSync(connection,False)
