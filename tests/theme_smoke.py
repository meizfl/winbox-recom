"""Click the top-panel theme control; check persistence, text and caret contrast."""
import os, re, subprocess, sys, tempfile
from pathlib import Path
from png_pixels import read_png
binary=Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix='winbox-theme-') as directory:
    root=Path(directory)
    base={key:value for key,value in os.environ.items() if not key.startswith('WINBOX_TEST_') and key!='WINBOX_THEME'}
    base.update(SDL_VIDEO_DRIVER='dummy',WINBOX_PROFILE=str(root/'profile'))
    def launch(name,**extra):
        result=subprocess.run([str(binary),'--smoke-ms','2300','--screenshot',str(root/(name+'.png'))],env=dict(base,**extra),text=True,capture_output=True,timeout=35)
        assert result.returncode==0,result.stderr[-4000:]
        assert 'lift stopped' not in result.stderr and 'presentation failed' not in result.stderr,result.stderr[-4000:]
        return read_png(root/(name+'.png')),result.stderr
    light,_=launch('light')
    dark,log=launch('dark',WINBOX_TEST_THEME_TOGGLE='1',WINBOX_TEST_INPUT='192.0.2.1',WINBOX_TEST_CARET_SOLID='1')
    assert 'WinBox theme: dark' in log,log[-2000:]
    assert (root/'profile/theme.conf').read_text().strip()=='dark'
    assert light[:2]==dark[:2],'Theme toggle changed window geometry'
    width,height,pixel=dark
    assert sum(light[2](width//2,38))>690,'Baseline input is not light'
    assert sum(pixel(width//2,38))<180,'Dark input background is too bright'
    text=sum(min(pixel(x,y))>180 for y in range(25,95) for x in range(10,80))
    assert text>30,'Dark labels have insufficient contrast'
    positions=re.findall(r'WinBox smoke caret drawn (\d+) (\d+) (\d+) (\d+)',log)
    assert positions,'Theme button stole input focus or hid the caret'
    x,y,w,h=map(int,positions[-1]);ink=sum(min(pixel(cx,cy))>180 for cy in range(y,y+h) for cx in range(x,x+w))
    assert ink>=w*(h-2),'Input caret is not visible on a dark field'
    menu_file=root/'menu.png'
    reopened,_=launch('persisted',WINBOX_TEST_THEME_MENU='1',WINBOX_TEST_MENU_SELECT='__do_not_select__',WINBOX_TEST_MENU_SCREENSHOT=str(menu_file))
    assert sum(reopened[2](width//2,38))<180,'Dark theme did not persist'
    mw,mh,menu=read_png(menu_file)
    assert sum(menu(mw-5,mh-3))<180,'Native popup menu stayed light'
    assert sum(min(menu(x,y))>180 for y in range(mh) for x in range(mw))>20,'Dark popup text is unreadable'
    restored,_=launch('restored',WINBOX_TEST_THEME_TOGGLE='1')
    assert (root/'profile/theme.conf').read_text().strip()=='light'
    assert sum(restored[2](width//2,38))>690,'Switching back did not restore the light palette'
    print('Top-panel theme toggle: geometry, persistence, dark fields, caret and popup contrast passed')
