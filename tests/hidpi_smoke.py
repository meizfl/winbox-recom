"""Check native 1:1 GDI pixels, input and menus on a scaled Wayland output."""
import os,re,subprocess,sys,tempfile
from pathlib import Path
from png_pixels import read_png
expected=float(os.environ.get('WINBOX_EXPECT_DENSITY','0'))
if expected<=1: print('Run with a real HiDPI display and WINBOX_EXPECT_DENSITY');sys.exit(77)
binary=Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix='winbox-hidpi-') as directory:
    root=Path(directory);env={k:v for k,v in os.environ.items() if not k.startswith('WINBOX_TEST_')}
    env.update(WINBOX_PROFILE=str(root/'profile'),WINBOX_THEME='dark',WINBOX_TEST_PIXEL_GEOMETRY='1')
    def launch(name,**extra):
        result=subprocess.run([str(binary),'--smoke-ms','2600','--screenshot',str(root/(name+'.png'))],env=dict(env,**extra),capture_output=True,text=True,timeout=40)
        log=result.stdout+result.stderr
        assert result.returncode==0 and 'lift stopped' not in log and 'presentation failed' not in log,log[-4000:]
        assert f'DPI {round(96*expected)}' in log,log[:1000]
        geometry=re.findall(r'points=(\d+)x(\d+) buffer=(\d+)x(\d+) frame=(\d+)x(\d+)',log)
        assert geometry,'No native pixel geometry'
        pw,ph,bw,bh,fw,fh=map(int,geometry[-1]);assert (bw,bh)==(fw,fh),'Final frame was scaled'
        assert abs(bw-pw*expected)<=1 and abs(bh-ph*expected)<=1,'No native HiDPI framebuffer'
        return read_png(root/(name+'.png')),log
    picture,log=launch('input',WINBOX_TEST_INPUT='192.0.2.1',WINBOX_TEST_BACKSPACE='1',WINBOX_TEST_CARET_SOLID='1',WINBOX_TEST_THEME_TOGGLE='1')
    assert 'WinBox smoke input verified' in log,'HiDPI input/backspace clicked the wrong field'
    assert 'WinBox theme: light' in log,'HiDPI theme button hit testing failed'
    width,height,pixel=picture
    if expected==2:
        # A 96-DPI bitmap enlarged 2x would contain only identical 2x2 blocks.
        distinct=sum(len({pixel(x,y),pixel(x+1,y),pixel(x,y+1),pixel(x+1,y+1)})>1 for y in range(50,min(height-1,240),2) for x in range(20,min(width-1,180),2))
        assert distinct>100,('Guest text still resembles a scaled low-resolution bitmap',distinct)
    menu=root/'menu.png';launch('menu',WINBOX_TEST_THEME_MENU='1',WINBOX_TEST_MENU_SELECT='__do_not_select__',WINBOX_TEST_MENU_SCREENSHOT=str(menu))
    mw,mh,_=read_png(menu);assert mw>=round(140*expected) and mh>=round(20*expected),'Popup menu stayed low resolution'
    print(f'Native HiDPI {expected:g}x: device-pixel frame, sharp glyphs, field input, backspace, theme control and popup passed')
