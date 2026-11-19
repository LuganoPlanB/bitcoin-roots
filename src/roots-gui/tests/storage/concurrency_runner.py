# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Release owned RPC and process barriers only after the matching transition."""

import argparse
from contextlib import contextmanager, closing
from decimal import Decimal
import hashlib
import json
import os
from pathlib import Path
import secrets
import sqlite3
import subprocess
import tempfile
import threading
import time

from crash_runner import (
    GENESIS,
    TIP,
    WALLET,
    FakeHttp,
    Step,
    call,
    check_record,
    now,
    pinned_vector,
    png,
    private,
    sha,
    short_qr_vector,
    wait_marker,
    binary_identity,
    evidence_directory,
    validated_scratch,
    write_json,
)


@contextmanager
def actor(args, root, record, role, port=None, env=None):
    argv = [str(args.binary), role, str(root)] + ([] if port is None else [str(port)])
    start = now()
    with subprocess.Popen(
        argv,
        text=True,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env=env,
    ) as child:
        try:
            yield child
        except Exception:
            child.kill()
            raise
        finally:
            try:
                out, err = child.communicate(timeout=12)
            except subprocess.TimeoutExpired:
                child.kill()
                out, err = child.communicate()
            record["processes"].append(
                dict(
                    argv=argv,
                    cwd=str(Path.cwd()),
                    startUTC=start,
                    endUTC=now(),
                    stdout=out,
                    stderr=err,
                    exit=child.returncode,
                )
            )
    assert child.returncode == 0 and not err, f"{role} failed: {err}"
    assert json.loads(out)["passed"], f"{role} did not report settlement"


def release(child, stage):
    child.stdin.write(stage + "\n")
    child.stdin.flush()


def until(test):
    deadline = time.monotonic() + 8
    while not test():
        assert time.monotonic() < deadline, "owned observation deadline"
        time.sleep(0.002)


def script(
    record,
    method,
    expected,
    result=None,
    *,
    wallet=False,
    gate=None,
    disconnect=False,
    received=None,
):
    def check(path, body, _):
        assert path == ("/wallet/" + WALLET if wallet else "/") and expected(
            body["params"]
        )
        record["requests"].append(
            dict(method=method, scope="wallet" if wallet else "node", utc=now())
        )
        if received:
            received.set()

    return Step(
        method,
        result,
        gate=gate,
        disconnect=disconnect,
        request_check=check,
        gate_timeout=12,
    )


def signer(args, root, record, transition):
    original, signed = pinned_vector(args.vectors, root)
    gate = threading.Event()
    received = threading.Event()
    steps = [
        script(record, "getblockhash", lambda p: p == [0], GENESIS),
        script(
            record,
            "getwalletinfo",
            lambda p: p == [],
            dict(
                walletname=WALLET,
                descriptors=True,
                private_keys_enabled=True,
                external_signer=False,
            ),
            wallet=True,
        ),
        script(
            record,
            "walletprocesspsbt",
            lambda p: (
                p == dict(psbt=original, sign=True, finalize=False, bip32derivs=False)
            ),
            dict(psbt=signed, complete=True),
            wallet=True,
            gate=gate,
            received=received,
        ),
    ]
    with FakeHttp(steps) as node:
        private(root / "fixture-cookie", "fixture:" + node.secret)
        with actor(args, root, record, "signer-" + transition, node.port) as child:
            try:
                wait_marker(root / "boundary", child, "signing-dispatched")
                assert received.wait(7)
                release(child, "signing-dispatched")
                if transition == "shutdown":
                    assert child.wait(timeout=8) == 0
                    assert node.active == 1 and node.completed == [
                        "getblockhash",
                        "getwalletinfo",
                    ]
                    record["transitions"].append(
                        "close retired signer and joined worker before held RPC was released"
                    )
                else:
                    wait_marker(root / "boundary", child, "view-retired")
                    assert (
                        node.completed == ["getblockhash", "getwalletinfo"]
                        and node.active == 1
                    )
                    record["transitions"].append(
                        f"actual {transition} before valid reply is released"
                    )
                    gate.set()
                    until(lambda: node.active == 0 and len(node.completed) == 3)
                    record["transitions"].append("valid reply sent before owner polls")
                    release(child, "view-retired")
            finally:
                gate.set()
        until(lambda: node.active == 0 and len(node.completed) == 3)
        assert not node.check_failures and not node.read_timeouts
        assert node.completed == [step.method for step in steps]
    assert not node.active and not node.thread.is_alive()


def same_wire(facts, candidate):
    return hashlib.sha256(bytes.fromhex(candidate)).hexdigest() == facts["wireDigest"]


def preflight(record, facts, gate=None):
    policy = [
        dict(
            txid=facts["txid"],
            wtxid=facts["wtxid"],
            allowed=True,
            fees=dict(base=facts["fee"]),
        )
    ]
    return [
        script(record, "getblockhash", lambda p: p == [0], GENESIS),
        script(
            record,
            "getblockchaininfo",
            lambda p: p == [],
            dict(chain="regtest", bestblockhash=TIP, initialblockdownload=False),
        ),
        script(
            record,
            "testmempoolaccept",
            lambda p: (
                len(p) == 2
                and len(p[0]) == 1
                and same_wire(facts, p[0][0])
                and p[1] == Decimal("0.001")
            ),
            policy,
        ),
        script(record, "getbestblockhash", lambda p: p == [], TIP, gate=gate),
    ]


def broadcast(args, root, record):
    pinned_vector(args.vectors, root)
    facts = call(args.binary, root, record, "facts")
    record["exactPublicProof"] = facts
    contender, lost = threading.Event(), threading.Event()
    sent = threading.Event()
    steps = (
        preflight(record, facts)
        + preflight(record, facts)
        + preflight(record, facts, contender)
        + preflight(record, facts)
    )
    steps.append(
        script(
            record,
            "sendrawtransaction",
            lambda p: (
                len(p) == 3
                and same_wire(facts, p[0])
                and p[1] == Decimal("0.001")
                and p[2] == 0
            ),
            gate=lost,
            disconnect=True,
            received=sent,
        )
    )
    steps += preflight(record, facts)
    with FakeHttp(steps) as node:
        private(root / "fixture-cookie", "fixture:" + node.secret)
        with actor(args, root, record, "broadcast-first", node.port) as first:
            try:
                wait_marker(root / "boundary", first, "first-ready")
                with actor(args, root, record, "broadcast-contender", node.port):
                    until(
                        lambda: (
                            len(record["requests"]) == 12 and steps[11].entered.is_set()
                        )
                    )
                    release(first, "first-ready")
                    assert sent.wait(7)
                    record["journalAtDispatch"] = check_record(root, facts)
                    record["transitions"].append(
                        "first send accepted by scripted listener with durable Pending"
                    )
                    contender.set()
                assert check_record(root, facts) == record["journalAtDispatch"]
                record["transitions"].append(
                    "contender settled while first acknowledgment was withheld"
                )
            finally:
                contender.set()
                lost.set()
        assert not node.check_failures and not node.read_timeouts
        assert len(node.completed) == 20 and [
            r["method"] for r in record["requests"]
        ] == [s.method for s in steps]
    assert not node.active and not node.thread.is_alive()
    journal = root / "metadata.sqlite"
    with closing(sqlite3.connect(journal.as_uri() + "?mode=ro", uri=True)) as db:
        assert db.execute(
            "SELECT serial,outcome FROM broadcast_details"
        ).fetchall() == [(1, 4)]
        assert db.execute(
            "SELECT genesis,txid,wtxid,wire_digest FROM broadcast_attempts"
        ).fetchall() == [(GENESIS, facts["txid"], facts["wtxid"], facts["wireDigest"])]
    record.update(
        journalAfterLostReply=dict(serial=1, outcome="Uncertain"), sendRequests=1
    )


def retired_broadcast(args, root, record):
    pinned_vector(args.vectors, root)
    facts = call(args.binary, root, record, "facts")
    record["exactPublicProof"] = facts
    gate = threading.Event()
    steps = preflight(record, facts) + preflight(record, facts, gate)
    with FakeHttp(steps) as node:
        private(root / "fixture-cookie", "fixture:" + node.secret)
        with actor(args, root, record, "broadcast-retired", node.port) as child:
            try:
                wait_marker(root / "boundary", child, "policy-recheck-started")
                until(
                    lambda: len(record["requests"]) == 8 and steps[-1].entered.is_set()
                )
                release(child, "policy-recheck-started")
                wait_marker(root / "boundary", child, "policy-recheck-retired")
                record["transitions"].append(
                    "Clear advanced the draft and revoked approval before releasing the final recheck"
                )
                gate.set()
                until(lambda: len(node.completed) == 8 and node.active == 0)
                release(child, "policy-recheck-retired")
            finally:
                gate.set()
        assert not node.check_failures and not node.read_timeouts
        assert (
            node.completed == [step.method for step in steps]
            and len(record["requests"]) == 8
        )
    assert not node.active and not node.thread.is_alive()
    with closing(
        sqlite3.connect((root / "metadata.sqlite").as_uri() + "?mode=ro", uri=True)
    ) as db:
        assert db.execute("SELECT count(*) FROM broadcast_attempts").fetchone() == (0,)
    record.update(sendRequests=0, journalAttempts=0)


def metadata(args, root, record):
    with actor(args, root, record, "metadata-race") as first:
        wait_marker(root / "boundary", first, "archive-staged")
        record["transitions"].append("archive inspected at revision 1")
        call(args.binary, root, record, "metadata-writer")
        record["transitions"].append("independent process committed revision 2")
        release(first, "archive-staged")
        wait_marker(root / "boundary", first, "protected-owned")
        call(args.binary, root, record, "metadata-protected-contender")
        record["transitions"].append(
            "protected second owner refused while first remained unlocked"
        )
        release(first, "protected-owned")
    record["transitions"].append(
        "old CAS and old lock epoch refused before fresh restore at revision 3"
    )


def service(args, root, record, role):
    call(args.binary, root, record, role)


def file_worker(args, root, record, kind):
    if kind == "qr":
        short_qr_vector(args.vectors, root)
        target = root / "source.png"
        target.write_bytes(png(call(args.binary, root, record, "paint", image=True)))
    elif kind == "document":
        pinned_vector(args.vectors, root)
        target = root / "original.psbt"
    elif kind == "batch":
        target = root / "batch.csv"
        target.write_text(
            "Address,Amount,Label,Unit\nbcrt1qrk5wn7dxwy8l6ru39h6xvmdhxnvdh8p0kpjlxw,0.001,CSV fixture,BTC\n"
        )
    else:
        target = root / "release.bin"
        target.write_bytes(b"Public release fixture\n")
        (root / "manifest").write_text(sha(target) + "  " + target.name + "\n")
        (root / "signature").write_text(
            "No detached signature for this local digest fixture.\n"
        )
        (root / "public").write_text("No public key for this local digest fixture.\n")
    entered, finished = root / "file-entered", root / "file-release"
    env = dict(
        os.environ,
        LD_PRELOAD=str(args.hold_library),
        ROOTS_GUI_TEST_HELD_FILE=str(target.resolve()),
        ROOTS_GUI_TEST_FILE_ENTERED=str(entered),
        ROOTS_GUI_TEST_FILE_RELEASE=str(finished),
    )
    record["heldSourceSha256"] = sha(target)
    record["fixtureLibrary"] = binary_identity(args.hold_library)
    with actor(args, root, record, "file-" + kind, env=env) as child:
        try:
            wait_marker(root / "boundary", child, "file-dispatched")
            until(entered.is_file)
            release(child, "file-dispatched")
            wait_marker(root / "boundary", child, "file-retired")
            record["transitions"].append(
                "file operation held at close, old scope retired before controller release"
            )
            finished.touch()
            release(child, "file-retired")
        finally:
            finished.touch()
    record["transitions"].append(
        "obsolete result discarded and worker joined; fresh explicit read adopts"
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ("binary", "vectors", "output", "hold-library"):
        parser.add_argument("--" + option, type=Path, required=True)
    parser.add_argument("--download-worker", action="store_true")
    args = parser.parse_args()
    output = Path(
        tempfile.mkdtemp(prefix="concurrency-", dir=evidence_directory(args.output))
    )
    cases = []
    variants = ("lock", "clear", "review", "close", "shutdown")
    cases_to_run = [("signer-" + variant, signer, (variant,)) for variant in variants]
    cases_to_run += [
        ("broadcast", broadcast, ()),
        ("retired-broadcast", retired_broadcast, ()),
        ("metadata", metadata, ()),
    ]
    cases_to_run += [
        (role, service, (role,)) for role in ("wallet-retirement", "camera-retirement")
    ]
    cases_to_run += [
        ("file-" + kind, file_worker, (kind,)) for kind in ("document", "qr", "batch")
    ]
    if args.download_worker:
        cases_to_run.append(("file-download", file_worker, ("download",)))
    for name, test, extra in cases_to_run:
        record = dict(
            id=name,
            processes=[],
            requests=[],
            transitions=[],
            passed=False,
            startUTC=now(),
        )
        with tempfile.TemporaryDirectory(
            prefix="roots-gui-race-", dir=validated_scratch()
        ) as temp:
            root = Path(temp)
            private(root / "archive-password", secrets.token_urlsafe(28))
            try:
                test(args, root, record, *extra)
                record["passed"] = True
            except Exception as exc:
                record["failure"] = str(exc)
        record.update(scratchRemoved=not root.exists(), endUTC=now())
        cases.append(record)
    passed = sum(case["passed"] and case["scratchRemoved"] for case in cases)
    result = dict(
        selected=len(cases),
        executed=len(cases),
        passed=passed,
        failed=len(cases) - passed,
        skipped=0,
        binary=binary_identity(args.binary),
        vectorsSha256=sha(args.vectors),
        cases=cases,
    )
    write_json(output / "report.json", result)
    print(f"Concurrency evidence {output}")
    print(
        json.dumps(
            {
                key: result[key]
                for key in ("selected", "executed", "passed", "failed", "skipped")
            }
        )
    )
    return int(passed != len(cases))


if __name__ == "__main__":
    raise SystemExit(main())
