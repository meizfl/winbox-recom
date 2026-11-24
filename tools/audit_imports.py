#!/usr/bin/env python3
"""Compare PE imports with implemented native contracts; preserve unresolved names."""
import argparse, hashlib, json, re
from pathlib import Path
parser=argparse.ArgumentParser();parser.add_argument('--build',type=Path,default=Path('build/full'));parser.add_argument('--output',type=Path,default=Path('build/ui-audit/imports.json'));args=parser.parse_args()
coverage=json.loads((args.build/'gen/coverage.json').read_text())
imports=set(re.findall(r'return "([^"]+)";',(args.build/'gen/imports.c').read_text()))
implemented=set(coverage['dispatched_imports'])
data={'msvcrt.dll!_fmode','msvcrt.dll!_acmdln','msvcrt.dll!__initenv'}
missing=sorted(imports-implemented-data)
report={'binary_sha256':coverage['binary_sha256'],'imports':len(imports),'implemented':len(implemented&imports),'data_imports':sorted(data&imports),'unimplemented':missing,'note':'Dispatch coverage is not functional completeness. Some contracts remain partial; runtime UI cases are recorded separately.'}
args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n');print(f'{len(imports)} imports; {len(missing)} missing contracts; report: {args.output}')
