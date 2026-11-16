# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Actual Git source scope plus existing CMake/include/link/symbol audits."""
import sys
sys.dont_write_bytecode=True
import argparse
import json
from pathlib import Path
import subprocess
import tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'build'))
from support.runtime import evidence_directory,write_json
from audit_build import permitted_libraries

GUI=Path(__file__).resolve().parents[2];REPO=GUI.parents[1]
def permitted_path(path):return path.startswith('src/roots-gui/') or path in ['CMakeLists.txt','src/CMakeLists.txt']
def changed_paths(repository):
    files=[]
    # Report both ends of renames: moving an outside file into the GUI must
    # still expose its deletion, and moving out must expose its destination.
    for argv in [['git','diff','--no-renames','--name-only','-z'],
                 ['git','diff','--cached','--no-renames','--name-only','-z'],
                 ['git','ls-files','--others','--exclude-standard','-z']]:
        files += subprocess.check_output(argv,cwd=repository).decode().split('\0')
    return sorted(set(x for x in files if x))
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build',type=Path);p.add_argument('--output',type=Path,required=True);p.add_argument('--evaluate-fixture',type=Path)
 for flag in ['parent-off','parent-on','parent-qt-off','parent-qt-on']:p.add_argument('--'+flag,type=Path)
 a=p.parse_args();out=Path(tempfile.mkdtemp(prefix='boundary-',dir=evidence_directory(a.output)));checks=[]
 if a.evaluate_fixture:
  fixture=json.loads(a.evaluate_fixture.read_text())
  if fixture.get('kind')!='synthetic-boundary-input':raise ValueError('explicit synthetic fixture required')
  checks=[dict(id='source-scope.'+path,passed=permitted_path(path)) for path in fixture['paths']]
  checks.append(dict(id='link-fragments',passed=permitted_libraries(fixture['libraries'])))
  failed=sum(not c['passed'] for c in checks);r=dict(selected=len(checks),executed=len(checks),passed=len(checks)-failed,failed=failed,skipped=0,kind='synthetic contract input, not actual build qualification',checks=checks)
  write_json(out/'result.json',r);print(json.dumps(r|{'output':str(out)}));return int(failed>0 or not checks)
 if not a.build:p.error('actual boundary audit requires --build')
 for path in changed_paths(REPO):checks.append(dict(id='source-scope.'+path,passed=permitted_path(path)))
 if not checks:checks.append(dict(id='source-scope.clean-repository',passed=True))
 argv=[sys.executable,'-B',str(GUI/'tests/build/audit_build.py'),'--standalone',str(a.build),'--output',str(out/'build-audit.json')]
 for flag in ['parent-off','parent-on','parent-qt-off','parent-qt-on']:
  value=getattr(a,flag.replace('-','_'))
  if value:argv+=['--'+flag,str(value)]
 with (out/'audit.log').open('w') as log:r=subprocess.run(argv,stdout=log,stderr=subprocess.STDOUT,timeout=60)
 audit=json.loads((out/'build-audit.json').read_text());checks+=audit['checks']
 checks.append(dict(id='auditor-exit',passed=r.returncode==0))
 failed=sum(not c['passed'] for c in checks);result=dict(selected=len(checks),executed=len(checks),passed=len(checks)-failed,failed=failed,skipped=0,checks=checks,command=argv,exit=r.returncode,unexecutedProfiles=audit['unexecutedProfiles'])
 write_json(out/'result.json',result);print(json.dumps({k:result[k] for k in ['selected','executed','passed','failed','skipped','unexecutedProfiles']}|{'output':str(out)}));return int(failed>0 or not checks)
if __name__=='__main__':raise SystemExit(main())
