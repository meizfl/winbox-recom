"""Connect the translated GUI to a disposable local TCP peer, never a router."""
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import threading

binary=Path(sys.argv[1]).resolve()
try:
    listener=socket.socket(socket.AF_INET,socket.SOCK_STREAM)
    listener.bind(('127.0.0.1',0))
except PermissionError:
    print('Local sockets unavailable in this sandbox')
    sys.exit(77)
listener.listen(1)
listener.settimeout(15)
accepted=threading.Event()
errors=[]
def serve():
    try:
        peer,_=listener.accept()
        accepted.set()
        with peer:
            peer.settimeout(2)
            try: peer.recv(4096)
            except socket.timeout: pass
            # Closing an unresponsive local peer exercises connection failure.
    except Exception as exc:
        errors.append(str(exc))
worker=threading.Thread(target=serve,daemon=True)
worker.start()
with tempfile.TemporaryDirectory(prefix='winbox-connect-test-',dir=binary.parent) as folder:
    root=Path(folder)
    env=dict(os.environ,SDL_VIDEO_DRIVER='dummy',WINBOX_TEST_INPUT=f'127.0.0.1:{listener.getsockname()[1]}',
             WINBOX_TEST_CONNECT='1',WINBOX_TEST_CLOSE='1',WINBOX_PROFILE=str(root/'profile'),WINBOX_TRACE='1')
    for key in ('WINBOX_TEST_BACKSPACE','WINBOX_TEST_NEIGHBORS'):
        env.pop(key,None)
    result=subprocess.run([str(binary),'--smoke-ms','6000','--screenshot',str(root/'connection.png')],env=env,capture_output=True,text=True,timeout=30)
    (binary.parent/'connection-smoke.log').write_text(result.stdout+'\n'+result.stderr)
    assert result.returncode==0,result.stderr[-6000:]
    assert 'lift stopped' not in result.stderr,result.stderr[-6000:]
    assert accepted.is_set(),f'TCP peer was not reached: {errors}; {result.stderr[-6000:]}'
    assert 'Guest thread 2 finished (0)' in result.stderr,'Resolver thread did not finish'
    print('Connection test passed: resolver thread, local TCP connection and shutdown')
listener.close()
worker.join(3)
