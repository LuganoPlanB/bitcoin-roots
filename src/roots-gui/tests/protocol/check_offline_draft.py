# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Check public signature and finalization policies against pinned public vectors."""
import argparse
import base64
import errno
import json
from pathlib import Path
import socket
import subprocess
import sys
import tempfile

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from support.runtime import validated_scratch
from theme.offline_draft_runner import pack,read_maps


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--probe",required=True,type=Path)
    p.add_argument("--cases",required=True,type=Path)
    args=p.parse_args()
    for family in (socket.AF_INET,socket.AF_INET6):
        try:
            with socket.socket(family):
                raise AssertionError("network enabled in public policy test")
        except OSError as error:
            assert error.errno==errno.EPERM
    data=json.loads(args.cases.read_text())
    selected=[("finalizer",i,False) for i in range(10)]
    selected += [("signer",i,False) for i in (6,7,8,17,19)]
    selected += [("signer",9,True),("signer",12,True),("finalizer",10,True)]
    results=[]
    with tempfile.TemporaryDirectory(prefix="offline-policy-",dir=validated_scratch()) as scratch:
        for group,index,rejected in selected:
            item=data[group][index]
            source=item["psbt" if group=="finalizer" else "result"]
            document=base64.b64decode(source,validate=True)
            fields=read_maps(document)
            # Each selected independent vector defines one input. Derive a
            # signature-free source by deleting only that input's signature keys.
            base=[dict(row) for row in fields]
            signatures=[key for key in base[1] if key[0] in (2,19)]
            assert signatures
            for key in signatures:
                del base[1][key]
            folder=Path(scratch)/f"{group}-{index}"
            folder.mkdir()
            (folder/"base.psbt").write_bytes(pack(base))
            (folder/"signature.psbt").write_bytes(document)
            if len(signatures)==2 and not rejected:
                partial=[dict(row) for row in fields]
                del partial[1][signatures[-1]]
                (folder/"partial.psbt").write_bytes(pack(partial))
            run=subprocess.run([str(args.probe),"policy",str(folder),"reject" if rejected else "accept"],
                text=True,capture_output=True,timeout=6)
            passed=run.returncode==0 and not run.stderr
            results.append(dict(group=group,index=index,title=item["comment"],expectedRejection=rejected,passed=passed,
                result=json.loads(run.stdout) if passed else None,stderr=run.stderr,exit=run.returncode))
    print(json.dumps(dict(selected=len(selected),executed=len(results),passed=sum(r["passed"] for r in results),
        failed=sum(not r["passed"] for r in results),skipped=0,networkDenied=True,scratchRemoved=not Path(scratch).exists(),cases=results)))
    return int(any(not r["passed"] for r in results))


if __name__=="__main__":
    raise SystemExit(main())
