"""Exercise translated WinBox startup, input, tabs, saving and close without a router."""
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import re
from png_pixels import read_png

binary = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix='winbox-gui-test-', dir=binary.parent) as folder:
    root = Path(folder)
    env = dict(os.environ, SDL_VIDEO_DRIVER='dummy', WINBOX_THEME='light', WINBOX_TEST_INPUT='192.0.2.1',
               WINBOX_TEST_BACKSPACE='1', WINBOX_TEST_PASTE_INPUT='1', WINBOX_TEST_RESIZE='1', WINBOX_TEST_NEIGHBORS='1', WINBOX_TEST_CLOSE='1', WINBOX_PROFILE=str(root/'profile'))
    frames=root/'frames';frames.mkdir();env['WINBOX_TEST_FRAME_DIR']=str(frames)
    env.pop('WINBOX_TRACE', None)
    result = subprocess.run([str(binary), '--smoke-ms', '6000', '--screenshot', str(root/'login.png')],
                            env=env, capture_output=True, text=True, timeout=90)
    (binary.parent/'gui-smoke.log').write_text(result.stdout+'\n'+result.stderr)
    assert result.returncode == 0, result.stderr[-4000:]
    assert 'lift stopped' not in result.stderr, result.stderr[-4000:]
    assert 'WinBox presentation failed' not in result.stderr, result.stderr[-4000:]
    for marker in ['WinBox event loop ready', 'WinBox smoke input verified', 'WinBox smoke close requested']:
        assert marker in result.stderr, f'Missing {marker}: {result.stderr[-4000:]}'
    assert 'Could not save configuration' not in result.stdout
    assert 'bad bmp format' not in result.stdout,result.stdout[-2000:]
    config=root/'profile'/'Mikrotik'/'Winbox'/'Addresses.cdb'
    assert config.exists() and config.stat().st_size>=4, 'Configuration did not save'
    png = (root/'login.png').read_bytes()
    assert png[:8] == b'\x89PNG\r\n\x1a\n'
    width, height = struct.unpack_from('>II', png, 16)
    assert (width,height)==(960,720) and len(png)>8000, f'Native resize did not reach the rendered canvas: {width}x{height}'
    presented=sorted(frames.glob('frame-*.png'));assert len(presented)>=2,'No frame sequence captured'
    for frame in presented:
        fw,fh,pixel=read_png(frame)
        ink=sum(max(pixel(x,y))<80 for y in range(26,min(145,fh)) for x in range(10,min(520,fw)))
        assert ink>80,f'Resize presented a cleared or unfinished header: {frame.name}, ink={ink}'
    reopened_env=dict(env);reopened_env.pop('WINBOX_TEST_FRAME_DIR',None)
    for key in ('WINBOX_TEST_INPUT','WINBOX_TEST_RESIZE','WINBOX_TEST_PASTE_INPUT','WINBOX_TEST_BACKSPACE','WINBOX_TEST_NEIGHBORS','WINBOX_TEST_CLOSE'):
        reopened_env.pop(key,None)
    reopened=subprocess.run([str(binary),'--smoke-ms','1000','--screenshot',str(root/'reopened.png')],env=reopened_env,capture_output=True,text=True,timeout=30)
    assert reopened.returncode==0,reopened.stderr[-4000:]
    caret_env=dict(reopened_env,WINBOX_TEST_INPUT='192.0.2.1',WINBOX_TEST_CARET_SOLID='1')
    caret=subprocess.run([str(binary),'--smoke-ms','2600','--screenshot',str(root/'caret.png')],env=caret_env,capture_output=True,text=True,timeout=30)
    assert caret.returncode==0,caret.stderr[-4000:]
    positions=re.findall(r'WinBox smoke caret drawn (\d+) (\d+) (\d+) (\d+)',caret.stderr)
    assert positions,'Input caret was never rendered'
    cx,cy,cw,ch=map(int,positions[-1]);fw,fh,pixel=read_png(root/'caret.png')
    assert cw>0 and ch>=8 and cx+cw<=fw and cy+ch<=fh
    caret_ink=sum(max(pixel(x,y))<80 for y in range(cy,cy+ch) for x in range(cx,cx+cw))
    assert caret_ink>=cw*(ch-2),f'Missing visible input caret: {cx},{cy},{cw},{ch}, ink={caret_ink}'
    reopened_png=(root/'reopened.png').read_bytes()
    assert len(reopened_png)>8000,'Restoring the saved window lost its controls'
    # Invalid persisted text previously disabled drawing of every later widget.
    layout=root/'profile'/'Mikrotik'/'Winbox'/'settings.cfg.viw'
    data=layout.read_bytes();offset=0;address_replaced=False
    while offset+2<=len(data):
        size=struct.unpack_from('<H',data,offset)[0]
        if not size: break
        body=data[offset+2:offset+2+size];key_size=body[0]
        if body[1:1+key_size]==b'addr':
            replacement=body[:1+key_size]+b'bad-\xd1.value'
            layout.write_bytes(data[:offset]+struct.pack('<H',len(replacement))+replacement+data[offset+2+size:])
            address_replaced=True
            break
        offset+=size+2
    assert address_replaced,'Saved address record missing'
    malformed=subprocess.run([str(binary),'--smoke-ms','800','--screenshot',str(root/'malformed-text.png')],env=reopened_env,capture_output=True,text=True,timeout=30)
    assert malformed.returncode==0,malformed.stderr[-4000:]
    assert len((root/'malformed-text.png').read_bytes())>8000,'Malformed text poisoned the drawing context'
    print(f'Translated WinBox GUI passed: {width}x{height}; input with real SDL Backspace, Neighbors tab and close')
