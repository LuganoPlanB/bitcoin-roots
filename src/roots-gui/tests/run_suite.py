# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Plan entry points with exact identities, nonempty selection and immutable logs."""
import sys
sys.dont_write_bytecode=True
import argparse
from datetime import datetime,timezone
import json
import os
import signal
from pathlib import Path
import shutil
import subprocess
import tempfile
import xml.etree.ElementTree as ET
from support.runtime import evidence_directory,write_json

SUITES={
 'model':('roots-gui.model',{'roots_gui.models','roots_gui.preferences.models','roots_gui.fixture_contracts','roots_gui.screen_contracts'}),
 'rpc':('rpc',{'roots_gui.rpc.fake_http','roots_gui.rpc.scratch'}),
 'ui':('ui',{'roots_gui.engine.preferences','roots_gui.engine.bootstrap',*[f'roots_gui.imkit.{x}' for x in ['theme','design_system','workflow','charts','api_compile','design_api_compile','charts_api_compile']]}),
 'regtest':('regtest',{'roots_gui.regtest.owned'}),
 'visual':('visual',{'roots_gui.visual.native','roots_gui.visual.contracts'}),
 'boundary':('boundary',{'roots_gui.boundary.actual','roots_gui.boundary.rejections','roots_gui.build_contract',*[f'roots_gui.runner.{x}' for x in ['failure','unknown','count','undrained','timeout']]}),
}
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--suite',choices=SUITES,required=True);p.add_argument('--build',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 parent=evidence_directory(a.output);out=Path(tempfile.mkdtemp(prefix=a.suite+'-',dir=parent));label,expected=SUITES[a.suite];build=a.build.resolve()
 argv=['ctest','--test-dir',str(build),'-L','^'+label+'$','--show-only=json-v1']
 result=subprocess.run(argv,capture_output=True,text=True,timeout=30);(out/'selection.json').write_text(result.stdout)
 selected={x['name'] for x in json.loads(result.stdout).get('tests',[])} if result.returncode==0 else set()
 record=dict(suite=a.suite,label=label,selected=sorted(selected),selectedCount=len(selected),expected=sorted(expected),selectionCommand=argv,startUTC=datetime.now(timezone.utc).isoformat(),executed=0,passed=0,failed=0,skipped=0)
 if selected!=expected:
  record.update(runnerErrors=1,error='exact intended suite unavailable or selection mismatch',exit=2);write_json(out/'result.json',record);print(json.dumps(record));return 2
 # Preserve mutable engine exports before CTest can replace them. CTest JUnit is unique.
 engine_roots=[build/'tests/engine',build/'src/roots-gui/tests/engine']
 reports=[p for folder in engine_roots if folder.is_dir() for p in folder.rglob('*.xml')]
 record['archivedReportCount']=len(reports)
 record['snapshotScope']=[str(folder) for folder in engine_roots if folder.is_dir()]
 for path in reports:
  target=out/'prior-engine-reports'/path.relative_to(build);target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(path,target)
 argv=['ctest','--test-dir',str(build),'-L','^'+label+'$','--output-on-failure','--no-tests=error','--output-junit',str(out/'junit.xml'),'-j','2']
 with (out/'ctest.log').open('w') as log:
  child=subprocess.Popen(argv,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
  try:child.wait(timeout=1200)
  except subprocess.TimeoutExpired:
   record['error']='owned CTest timeout';record['timedOut']=True
   os.killpg(child.pid,signal.SIGTERM)
   try:child.wait(timeout=5)
   except subprocess.TimeoutExpired:os.killpg(child.pid,signal.SIGKILL);child.wait(timeout=3)
  record['childSettled']=child.poll() is not None
 record.update(command=argv,exit=child.returncode,endUTC=datetime.now(timezone.utc).isoformat())
 if (out/'junit.xml').is_file():
  cases=ET.parse(out/'junit.xml').getroot().findall('.//testcase');executed=[c for c in cases if c.find('skipped') is None]
  record.update(executed=len(executed),passed=sum(c.find('failure') is None and c.find('error') is None for c in executed),failed=sum(c.find('failure') is not None or c.find('error') is not None for c in executed),skipped=len(cases)-len(executed))
  if {c.get('name') for c in cases}!=expected:record['error']='JUnit identity mismatch'
 else:record['error']='missing current JUnit'
 for path in reports:
  if path.is_file():
   target=out/'current-engine-reports'/path.relative_to(build);target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(path,target)
 write_json(out/'result.json',record);print(json.dumps(record));return int(child.returncode!=0 or record['executed']!=len(expected) or record['failed'] or record['skipped'] or 'error' in record)
if __name__=='__main__':raise SystemExit(main())
