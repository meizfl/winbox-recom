#!/usr/bin/env python3
"""Fetch pinned upstream archives and extract them into private build directories."""
import argparse,hashlib,json,os,subprocess,tempfile,urllib.request
from pathlib import Path
parser=argparse.ArgumentParser();parser.add_argument('names',nargs='+');parser.add_argument('--cache',type=Path,required=True);parser.add_argument('--extract',type=Path);args=parser.parse_args();lock=json.loads(Path(__file__).with_name('sources.json').read_text());args.cache.mkdir(parents=True,exist_ok=True)
for name in args.names:
 source=lock[name];path=args.cache/source['filename']
 if not path.exists():
  if not source['url'].startswith('https://'):raise SystemExit('Only HTTPS sources are allowed')
  temporary=path.with_suffix(path.suffix+'.partial')
  with urllib.request.urlopen(source['url'],timeout=90) as response,temporary.open('wb') as out:
   while chunk:=response.read(1024*1024):out.write(chunk)
  temporary.replace(path)
 actual=hashlib.sha256(path.read_bytes()).hexdigest()
 if actual!=source['sha256']:raise SystemExit(f'Checksum mismatch: {name}')
 if args.extract:
  destination=args.extract/name;destination.mkdir(parents=True,exist_ok=True)
  subprocess.run(['tar','-xf',str(path.resolve()),'-C',str(destination.resolve()),'--strip-components=1'],check=True)
 print(name,actual,flush=True)
