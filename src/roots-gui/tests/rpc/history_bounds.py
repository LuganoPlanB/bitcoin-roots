# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Bounded production RPC enrichment, duplicate IDs and pre-cancelled work."""

import sys

sys.dont_write_bytecode = True
import argparse
import json
import os
import subprocess
import tempfile
from datetime import datetime, timezone
from pathlib import Path
from urllib.parse import quote

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from rpc.fake_http import FakeHttp, Step
from support.runtime import (
    binary_identity,
    evidence_directory,
    validated_scratch,
    write_json,
)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--binary", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    a = p.parse_args()
    out = evidence_directory(a.output)

    def request(path, body, headers):
        assert path == "/wallet/" + quote("bounds/札幌", safe="") and body[
            "params"
        ] == dict(txid="1" * 64)

    steps = [
        Step(
            "gettransaction",
            dict(
                txid="1" * 64,
                amount=1,
                time=1700000000,
                confirmations=2,
                details=[dict(category="receive", vout=0, amount=1)],
            ),
            request_check=request,
        ),
        Step(
            "getbalances",
            dict(mine=dict(trusted=2, untrusted_pending=0, immature=0, nonmempool=0)),
        ),
        Step("getblockchaininfo", dict(blocks=2, bestblockhash="b" * 64)),
        Step("getblockhash", "a" * 64),
    ]
    steps += [
        Step("gettransaction", steps[0].result, request_check=request),
        Step("getbalances", steps[1].result),
        Step("getblockchaininfo", steps[2].result),
        Step("getblockhash", "c" * 64),
    ]
    with (
        tempfile.TemporaryDirectory(
            prefix="history-bounds-", dir=validated_scratch()
        ) as tmp,
        FakeHttp(steps) as server,
    ):
        cookie = Path(tmp) / "cookie"
        cookie.write_text("fixture:" + server.secret)
        cookie.chmod(0o600)
        argv = [str(a.binary), str(server.port), str(cookie)]
        start = datetime.now(timezone.utc).isoformat()
        run = subprocess.run(argv, capture_output=True, text=True, timeout=10)
        end = datetime.now(timezone.utc).isoformat()
        result = json.loads(run.stdout) if run.returncode == 0 else {}
        passed = (
            run.returncode == 0
            and not run.stderr
            and result.get("passed")
            and len(server.records) == 8
            and not server.check_failures
            and all(s.entered.is_set() for s in steps)
        )
    settled = (
        not server.thread.is_alive() and server.active == 0 and not Path(tmp).exists()
    )
    report = dict(
        selected=1,
        executed=1,
        passed=int(passed and settled),
        failed=int(not (passed and settled)),
        skipped=0,
        argv=argv,
        cwd=os.getcwd(),
        startUTC=start,
        endUTC=end,
        exit=run.returncode,
        stdout=run.stdout,
        stderr=run.stderr,
        result=result,
        requests=server.records,
        settled=settled,
        scratchRemoved=not Path(tmp).exists(),
        binary=binary_identity(a.binary),
        coverage="Loopback typed fixture only; no daemon or Core runtime.",
    )
    write_json(out / "result.json", report)
    print(
        json.dumps(
            {
                k: report[k]
                for k in ["selected", "executed", "passed", "failed", "skipped"]
            }
        )
    )
    return report["failed"]


if __name__ == "__main__":
    raise SystemExit(main())
