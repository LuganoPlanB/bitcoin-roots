# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Kill owned recovery actors at real durable boundaries and reopen their stores."""

import argparse
import base64
from contextlib import closing
from datetime import datetime, timezone
from decimal import Decimal
import hashlib
import json
import os
from pathlib import Path
import secrets
import sqlite3
import subprocess
import sys
import tempfile
import threading
import time

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from protocol.qr_exchange import png
from rpc.fake_http import FakeHttp, Step
from rpc.funding_runner import encode_maps, maps
from support.runtime import (
    binary_identity,
    evidence_directory,
    validated_scratch,
    write_json,
)

GENESIS = "0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206"
WALLET = "crash-fixture"
TIP = "e" * 64
BOUNDARIES = {
    "draft-before-save": "edited",
    "draft-after-save": "saved",
    "qr-before-save": "qr-decoded",
    "qr-after-save": "qr-saved",
    "sign-pending": "sign-dispatched",
    "finalized-before-save": "finalized-local",
    "submission-lost-reply": "durable-submission",
}
CASES = ("archive-contracts", "sqlite-migration", "protected-migration", *BOUNDARIES)


def now():
    return datetime.now(timezone.utc).isoformat()


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def private(path, content):
    path.write_text(content)
    path.chmod(0o600)


def wait_marker(path, process, expected):
    deadline = time.monotonic() + 12
    while time.monotonic() < deadline:
        if path.is_file() and path.read_text() == expected:
            return
        if process.poll() is not None:
            raise RuntimeError("owned process exited before its interruption boundary")
        time.sleep(0.003)
    raise RuntimeError("interruption boundary deadline")


def call(binary, root, record, mode, *args, image=False):
    argv = [str(binary), mode, str(root), *map(str, args)]
    start = now()
    done = subprocess.run(argv, text=not image, capture_output=True, timeout=25)
    entry = dict(
        argv=argv,
        cwd=str(Path.cwd()),
        startUTC=start,
        endUTC=now(),
        exit=done.returncode,
        stdout="" if image else done.stdout,
        stderr=done.stderr.decode(errors="replace") if image else done.stderr,
    )
    if image:
        entry["stdoutSha256"] = hashlib.sha256(done.stdout).hexdigest()
        entry["stdoutBytes"] = len(done.stdout)
    record["processes"].append(entry)
    if done.returncode or entry["stderr"]:
        raise RuntimeError(f"fixture command {mode} did not complete")
    return done.stdout if image else json.loads(done.stdout)


def interrupted(
    binary, root, record, mode, boundary, *, env=None, port=None, enter=None
):
    argv = [str(binary), mode, str(root)]
    if port is not None:
        argv.append(str(port))
    start = now()
    reached = False
    with subprocess.Popen(
        argv, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env
    ) as child:
        try:
            wait_marker(root / "boundary", child, boundary)
            if enter and not enter.wait(7):
                raise RuntimeError(
                    "owned RPC fixture did not reach the expected boundary"
                )
            reached = True
        finally:
            if child.poll() is None:
                child.kill()
            stdout, stderr = child.communicate(timeout=5)
            record["processes"].append(
                dict(
                    argv=argv,
                    cwd=str(Path.cwd()),
                    startUTC=start,
                    endUTC=now(),
                    pid=child.pid,
                    exactBoundary=boundary,
                    boundaryReached=reached,
                    exit=child.returncode,
                    stdout=stdout,
                    stderr=stderr,
                    deliberateKillSignal=9 if reached else None,
                )
            )
    assert child.returncode == -9 and not stdout and not stderr


def pinned_vector(file, root):
    vector = json.loads(file.read_text())["finalizer"][2]
    rows = maps(base64.b64decode(vector["psbt"]))
    rows[0][b"\x06"] = b"\x00"
    rows[0][b"\xfc\x05roots\x01"] = b"opaque recovery fixture"
    signed = encode_maps(rows)
    original = [dict(row) for row in rows]
    (sig,) = (key for key in original[1] if key[0] == 2)
    original[1].pop(sig)
    unsigned = encode_maps(original)
    (root / "original.psbt").write_bytes(base64.b64decode(unsigned))
    (root / "collected.psbt").write_bytes(base64.b64decode(signed))
    (root / "descriptor").write_text("wpkh(" + sig[1:].hex() + ")")
    (root / "qr-source.txt").write_text(unsigned)
    return unsigned, signed


def short_qr_vector(file, root):
    source = json.loads(file.read_text())["signer"][12]["psbt"]
    rows = maps(base64.b64decode(source))
    rows[0][b"\xfc\x05roots\x01"] = b"opaque QR fixture"
    encoded = encode_maps(rows)
    (root / "original.psbt").write_bytes(base64.b64decode(encoded))
    (root / "qr-source.txt").write_text(encoded)


def schema(path):
    with closing(sqlite3.connect(path)) as db:
        (version,) = db.execute("PRAGMA user_version").fetchone()
        names = [
            n
            for (n,) in db.execute(
                "SELECT name FROM sqlite_schema WHERE type='table' ORDER BY name"
            )
        ]
        row = db.execute("SELECT revision,display_name FROM scopes").fetchone()
        labels = db.execute("SELECT kind,target,label FROM labels").fetchall()
    return dict(version=version, tables=names, scope=row, labels=labels)


def migration(args, root, record, protected):
    path = root / ("metadata.sqlite.protected" if protected else "metadata.sqlite")
    target = path if protected else Path(str(path) + "-journal")
    operation = "rename" if protected else "unlink"
    seed_mode = "protected-legacy" if protected else "legacy"
    run_mode = "migrate-protected" if protected else "migrate"
    call(args.binary, root, record, seed_mode)
    before = (
        call(args.binary, root, record, "protected-schema")
        if protected
        else schema(path)
    )
    assert (
        before["schema"] if protected else before["version"]
    ) == 1 and path.is_file()
    initial_hash = sha(path)
    env = dict(
        os.environ,
        LD_PRELOAD=str(args.stop_library),
        ROOTS_GUI_TEST_CRASH_TARGET=str(target),
        ROOTS_GUI_TEST_CRASH_MARKER=str(root / "boundary"),
        ROOTS_GUI_TEST_CRASH_SYSCALL=operation,
    )
    interrupted(args.binary, root, record, run_mode, "boundary\n", env=env)
    if protected:
        assert sha(path) == initial_hash
    else:
        assert target.is_file(), (
            "rollback journal must still be present after process death"
        )
    recovered = (
        call(args.binary, root, record, "protected-schema")
        if protected
        else schema(path)
    )
    assert recovered == before
    assert sha(path) == initial_hash, "restart recovered the exact legacy bytes"
    call(args.binary, root, record, run_mode)
    after = (
        call(args.binary, root, record, "protected-schema")
        if protected
        else schema(path)
    )
    assert (after["schema"] if protected else after["version"]) == 11
    if not protected:
        assert (
            after["scope"] == (7, "Legacy display")
            and after["labels"] == before["labels"]
        )
        assert (
            "wallet_icons" in after["tables"] and "wallet_icons" not in before["tables"]
        )
    assert not Path(str(path) + "-journal").exists()
    record.update(
        exactInterposedPath=str(target),
        interposedSyscall=operation,
        original=before,
        recoveredLegacy=recovered,
        migrated=after,
        originalHash=initial_hash,
        migratedHash=sha(path),
    )


def check_record(root, facts):
    path = root / "metadata.sqlite"
    with closing(sqlite3.connect(path.as_uri() + "?mode=ro", uri=True)) as db:
        (row,) = db.execute(
            "SELECT genesis,txid,wtxid,wire_digest FROM broadcast_attempts"
        ).fetchall()
        (detail,) = db.execute(
            "SELECT serial,outcome FROM broadcast_details"
        ).fetchall()
    assert row == (
        GENESIS,
        facts["txid"],
        facts["wtxid"],
        facts["wireDigest"],
    ) and detail == (1, 0)
    return dict(serial=detail[0], outcome="Pending", txid=row[1], wireDigest=row[3])


def transactions(args, root, record, name):
    unsigned, signed = pinned_vector(args.vectors, root)
    facts = call(args.binary, root, record, "facts")
    (root / "expected-txid").write_text(facts["txid"])
    (root / "expected-digest").write_text(facts["wireDigest"])
    observations = []
    journal = []
    gate = threading.Event()

    def verify(method, condition, wallet=False):
        def check(path, body, _headers):
            assert path == ("/wallet/" + WALLET if wallet else "/") and condition(
                body["params"]
            )
            observations.append(
                dict(
                    method=method,
                    phase="before-kill",
                    scope="wallet" if wallet else "node",
                )
            )

        return check

    def same_wire(hex_text):
        return (
            hashlib.sha256(bytes.fromhex(hex_text)).hexdigest() == facts["wireDigest"]
        )

    accepted = None
    if name == "sign-pending":
        steps = [
            Step(
                "getblockhash",
                GENESIS,
                request_check=verify("getblockhash", lambda p: p == [0]),
            ),
            Step(
                "getwalletinfo",
                dict(
                    walletname=WALLET,
                    descriptors=True,
                    private_keys_enabled=True,
                    external_signer=False,
                ),
                request_check=verify("getwalletinfo", lambda p: p == [], True),
            ),
            Step(
                "walletprocesspsbt",
                dict(psbt=signed, complete=True),
                gate=gate,
                request_check=verify(
                    "walletprocesspsbt",
                    lambda p: (
                        p
                        == dict(
                            psbt=unsigned, sign=True, finalize=False, bip32derivs=False
                        )
                    ),
                    True,
                ),
            ),
        ]
        accepted = steps[-1].entered
        reopen = "reopen-signing"
    elif name == "submission-lost-reply":
        policy = [
            dict(
                txid=facts["txid"],
                wtxid=facts["wtxid"],
                allowed=True,
                fees=dict(base=facts["fee"]),
            )
        ]

        def preflight():
            return [
                Step(
                    "getblockhash",
                    GENESIS,
                    request_check=verify("getblockhash", lambda p: p == [0]),
                ),
                Step(
                    "getblockchaininfo",
                    dict(
                        chain="regtest", bestblockhash=TIP, initialblockdownload=False
                    ),
                    request_check=verify("getblockchaininfo", lambda p: p == []),
                ),
                Step(
                    "testmempoolaccept",
                    policy,
                    request_check=verify(
                        "testmempoolaccept",
                        lambda p: (
                            len(p) == 2
                            and len(p[0]) == 1
                            and same_wire(p[0][0])
                            and p[1] == Decimal("0.001")
                        ),
                    ),
                ),
                Step(
                    "getbestblockhash",
                    TIP,
                    request_check=verify("getbestblockhash", lambda p: p == []),
                ),
            ]

        observed_send = threading.Event()

        def sent(path, body, headers):
            verify(
                "sendrawtransaction",
                lambda p: (
                    len(p) == 3
                    and same_wire(p[0])
                    and p[1] == Decimal("0.001")
                    and p[2] == 0
                ),
            )(path, body, headers)
            journal.append(check_record(root, facts))
            observed_send.set()

        steps = (
            preflight()
            + preflight()
            + [Step("sendrawtransaction", facts["txid"], request_check=sent, gate=gate)]
        )
        accepted = observed_send
        reopen = "reopen-submission"
    else:
        steps = []
        reopen = "reopen-finalization"
    with FakeHttp(steps) as node:
        private(root / "fixture-cookie", "fixture:" + node.secret)
        try:
            interrupted(
                args.binary,
                root,
                record,
                name,
                BOUNDARIES[name],
                port=node.port,
                enter=accepted,
            )
        finally:
            gate.set()
        if steps:
            limit = time.monotonic() + 5
            while len(node.records) < len(steps) and time.monotonic() < limit:
                time.sleep(0.002)
        assert not node.check_failures and len(node.records) == len(steps)
        # Restart is performed with the same live owned listener. Production
        # recovery has no capability to repeat signing or submission.
        call(args.binary, root, record, reopen, node.port)
        assert [r["method"] for r in node.records] == [step.method for step in steps]
        assert all(
            node.secret not in p["stdout"] + p["stderr"] for p in record["processes"]
        )
    assert not node.active and not node.thread.is_alive()
    if name == "submission-lost-reply":
        assert journal == [check_record(root, facts)]
    record.update(
        exactPublicProof=facts,
        requestsBeforeKill=observations,
        requestsAfterRestart=0,
        journalAtDispatch=journal,
        fixtureSettled=True,
    )


def case(args, output, name):
    record = dict(id=name, startUTC=now(), processes=[], passed=False)
    with tempfile.TemporaryDirectory(
        prefix="l77-crash-", dir=validated_scratch()
    ) as folder:
        root = Path(folder)
        private(root / "archive-password", secrets.token_urlsafe(28))
        try:
            if name == "archive-contracts":
                record["contractChecks"] = call(args.binary, root, record, name)
            elif name in ("sqlite-migration", "protected-migration"):
                migration(args, root, record, name == "protected-migration")
            elif name.startswith(("draft-", "qr-")):
                if name.startswith("qr-"):
                    short_qr_vector(args.vectors, root)
                    raster = png(call(args.binary, root, record, "paint", image=True))
                    (root / "source.png").write_bytes(raster)
                    record["ownedQrSha256"] = sha(root / "source.png")
                interrupted(args.binary, root, record, name, BOUNDARIES[name])
                record["restart"] = call(
                    args.binary, root, record, "recover-public", name
                )
            else:
                transactions(args, root, record, name)
            record["passed"] = True
        except Exception as error:
            record["failure"] = str(error)
    record.update(scratchRemoved=not root.exists(), endUTC=now())
    write_json(output / (name + ".json"), record)
    return record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for item in ("binary", "stop-library", "vectors", "output"):
        parser.add_argument("--" + item, type=Path, required=True)
    args = parser.parse_args()
    output = Path(
        tempfile.mkdtemp(
            prefix="recovery-crash-attempt-", dir=evidence_directory(args.output)
        )
    )
    records = [case(args, output, name) for name in CASES]
    passed = sum(item["passed"] and item["scratchRemoved"] for item in records)
    result = dict(
        selected=len(CASES),
        executed=len(records),
        passed=passed,
        failed=len(records) - passed,
        skipped=0,
        binary=binary_identity(args.binary),
        stopLibrary=binary_identity(args.stop_library),
        vectors=dict(
            path=str(args.vectors.resolve()),
            bytes=args.vectors.stat().st_size,
            sha256=sha(args.vectors),
        ),
        cases=records,
        observedUTC=now(),
    )
    write_json(output / "report.json", result)
    print(f"Recovery crash evidence: {output}")
    print(
        json.dumps(
            {
                k: result[k]
                for k in ["selected", "executed", "passed", "failed", "skipped"]
            }
        )
    )
    return int(result["failed"] != 0)


if __name__ == "__main__":
    raise SystemExit(main())
