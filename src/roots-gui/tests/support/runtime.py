# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Private test artifacts and secret-free fixture records, never product services."""
import hashlib
import json
import os
from pathlib import Path
import tempfile

REPOSITORY = Path(__file__).resolve().parents[4]

def evidence_directory(value):
    path = Path(value).resolve()
    if path == REPOSITORY or REPOSITORY in path.parents:
        raise ValueError('test artifacts must remain outside the source repository')
    path.mkdir(parents=True, exist_ok=True, mode=0o700)
    return path

def validated_scratch():
    # Ordinary operator execution has no managed owner packet. Preserve Python's
    # standard TMPDIR/TEMP/TMP/default selection in that case only.
    if 'GESTALT_SESSION_TMP_CONTEXT' not in os.environ:
        return Path(tempfile.gettempdir()).resolve()
    context = json.loads(os.environ['GESTALT_SESSION_TMP_CONTEXT'])
    required = ('directory', 'markerPath', 'ownerId', 'workspacePath')
    if not isinstance(context, dict) or any(not isinstance(context.get(k), str) or not context[k].strip() for k in required):
        raise ValueError('invalid managed scratch context')
    selected = os.environ.get('TMPDIR')
    if not selected or not selected.strip():
        raise ValueError('managed scratch requires TMPDIR')
    folder = Path(selected).resolve()
    if not folder.is_dir():
        raise ValueError('managed scratch directory is unavailable')
    marker = json.loads(Path(context['markerPath']).read_text())
    if not isinstance(marker, dict) or folder != Path(context['directory']).resolve() or marker.get('ownerId') != context['ownerId'] or marker.get('workspacePath') != context['workspacePath']:
        raise ValueError('disposable scratch ownership mismatch')
    return folder

def binary_identity(value):
    path = Path(value).resolve(strict=True)
    if not path.is_file() or not os.access(path, os.X_OK): raise ValueError('explicit executable required')
    return {'path':str(path),'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'bytes':path.stat().st_size}

def write_json(path, value):
    Path(path).write_text(json.dumps(value, indent=2)+'\n')

class Checks:
    def __init__(self): self.items=[]
    def check(self, ok, identifier):
        self.items.append(dict(id=identifier,passed=bool(ok)))
        if not ok: raise AssertionError(identifier)
    def result(self):
        failures=sum(not x['passed'] for x in self.items)
        return dict(selected=len(self.items),executed=len(self.items),passed=len(self.items)-failures,failed=failures,skipped=0,checks=self.items)
