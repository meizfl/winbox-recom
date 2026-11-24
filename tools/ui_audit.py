#!/usr/bin/env python3
"""Open read-only WinBox views in temporary profiles; never apply router settings."""
import argparse,concurrent.futures,hashlib,json,os,re,shutil,subprocess,tempfile,time
from pathlib import Path
parser=argparse.ArgumentParser();parser.add_argument('--binary',type=Path,default=Path('build/full/winbox3'));parser.add_argument('--modules',type=Path,required=True);parser.add_argument('--address',required=True);parser.add_argument('--password-file',type=Path,required=True);parser.add_argument('--output',type=Path,default=Path('build/ui-audit'));parser.add_argument('--jobs',type=int,default=2);parser.add_argument('--only',default='');args=parser.parse_args()
args.binary=args.binary.resolve();args.password_file=args.password_file.resolve();args.output=args.output.resolve();args.output.mkdir(parents=True,exist_ok=True);secret=args.password_file.read_text().strip()
flat=['Quick Set','WiFi','User Manager','Interfaces','WireGuard','Bridge','PPP','Mesh','Queues','Files','Log','RADIUS','Terminal','Container','App','Dot1X']
groups={'IP':['ARP','Addresses','Cloud','DHCP Client','DHCP Relay','DHCP Server','DNS','Firewall','Hotspot','IPsec','Neighbors','Pool','Routes','SNMP','Services','Settings','Socks','Traffic Flow','UPnP','Web Proxy'], 'IPv6':['Addresses','DHCP Client','DHCP Server','Firewall','ND','Neighbors','Pool','Routes','Settings'], 'Routing':['BFD','BGP','Filters','OSPF','Rules','Tables'], 'System':['Clock','Health','Identity','Logging','Resources','Packages','Scheduler','Scripts','Users','License','Certificates'], 'Tools':['Ping','Traceroute','Torch','Profile','Netwatch','Graphing']}
cases=[(name,None) for name in flat]+[(section,item) for section,items in groups.items() for item in items]
if args.only:
 wanted=set(args.only.split('|'));cases=[case for case in cases if '/'.join(x for x in case if x) in wanted]
def check(case):
 section,item=case;label='/'.join(x for x in case if x);slug=re.sub(r'[^a-z0-9]+','-',label.lower());started=time.monotonic();revision=hashlib.sha256(args.binary.read_bytes()).hexdigest()
 with tempfile.TemporaryDirectory(prefix='winbox-audit-') as folder:
  profile=Path(folder)/'profile';cache=profile/'Mikrotik/Winbox';cache.mkdir(parents=True)
  for source in args.modules.iterdir():
   if source.is_dir() and source.name[0].isdigit():shutil.copytree(source,cache/source.name)
  env=dict(os.environ,SDL_VIDEO_DRIVER='dummy',WINBOX_TEST_INPUT=args.address,WINBOX_TEST_PASSWORD_FILE=str(args.password_file),WINBOX_TEST_CONNECT='1',WINBOX_TEST_MAIN_READONLY='1',WINBOX_TEST_MAIN_SECTION=section,WINBOX_TEST_MAIN_RESIZE='1280x1000')
  for key in ('WINBOX_TRACE','WINBOX_DUMP_ICONS','WINBOX_TEST_MENU_METHOD','WINBOX_TEST_MAIN_DRAG','WINBOX_TEST_CLOSE','WINBOX_TEST_MENU_SELECT','WINBOX_TEST_TERMINAL'):env.pop(key,None)
  if item:env['WINBOX_TEST_MENU_SELECT']=item
  if section=='Terminal':env['WINBOX_TEST_TERMINAL']='1'
  try:
   result=subprocess.run([str(args.binary),'--profile',str(profile),'--smoke-ms','14000','--screenshot',str(args.output/(slug+'.png'))],env=env,capture_output=True,text=True,timeout=35)
   log=(result.stdout+'\n'+result.stderr).replace(secret,'[redacted]');code=result.returncode
  except subprocess.TimeoutExpired as e:
   log=((e.stdout or b'')+(e.stderr or b'')).decode(errors='replace').replace(secret,'[redacted]');code=None
  (args.output/(slug+'.log')).write_text(log)
  traps=[line for line in log.splitlines() if 'lift stopped' in line]
  titles=re.findall(r'WinBox native window: (.*?) \(\d+x\d+\), class routeros_dbl_canvas',log)
  if code is None:status='timeout'
  elif code or traps:status='failed'
  elif 'logged in!!!' not in log:status='login_failed'
  elif 'WinBox smoke opening ' not in log:status='not_reached'
  elif item and 'WinBox smoke selecting custom popup: '+item not in log:status='not_available_or_not_reached'
  elif not any(titles):status='no_view_observed'
  else:status='opened'
  return {'view':label,'status':status,'exit_code':code,'windows':[title for title in titles if title],'errors':traps,'seconds':round(time.monotonic()-started,2),'build_sha256':revision}
results=[]
with concurrent.futures.ThreadPoolExecutor(max_workers=max(1,min(args.jobs,2))) as pool:
 for result in pool.map(check,cases):
  results.append(result);(args.output/'results.json').write_text(json.dumps(results,ensure_ascii=False,indent=2)+'\n');print(result['view'],result['status'],'; '.join(result['errors']),flush=True)
print('Completed',len(results),'cases',flush=True)
