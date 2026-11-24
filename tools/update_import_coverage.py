#!/usr/bin/env python3
"""Refresh runtime dispatch metadata without decoding unchanged PE instructions."""
import json, re, sys
from pathlib import Path
generated=Path(sys.argv[1]);root=Path(__file__).resolve().parents[1]
path=generated/'coverage.json';report=json.loads(path.read_text())
imports=set(re.findall(r'return "([^"]+)";',(generated/'imports.c').read_text()))
names=set()
for source in (root/'runtime').glob('*.c'):
    text=source.read_text()
    names.update(re.findall(r'IS\("([^"]+)"\)',text))
    names.update(re.findall(r'!([A-Za-z_][A-Za-z_0-9]+)"',text))
report['dispatched_imports']=sorted(name for name in imports if name.split('!')[1] in names)
report['dispatched_import_count']=len(report['dispatched_imports'])
result=json.dumps(report,indent=2)+'\n'
if path.read_text()!=result:path.write_text(result)
