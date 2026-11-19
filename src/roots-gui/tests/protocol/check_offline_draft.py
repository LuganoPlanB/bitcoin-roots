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

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from support.runtime import validated_scratch
from theme.offline_draft_runner import pack, read_maps
from protocol.signature_exchange import wire_facts


def review_descriptor(group, index, fields, signatures):
    if group == "signer" and index in (6, 7, 8):
        (internal,) = [key[1:].hex() for key in fields if key[0] == 0x16]
        return f"tr({internal})"
    keys = [key[1:].hex() for key in signatures]
    if group != "finalizer":
        return None
    if index in (1, 3, 5):
        (key,) = keys
        return {1: f"sh(wpkh({key}))", 3: f"wpkh({key})", 5: f"pkh({key})"}[index]
    if index in (7, 9):
        script = fields[b"\x04" if index == 7 else b"\x05"]
        assert (
            len(script) == 71
            and script[:2] == b"\x52\x21"
            and script[35] == 0x21
            and script[-2:] == b"\x52\xae"
        )
        multisig = f"multi(2,{script[2:35].hex()},{script[36:69].hex()})"
        return f"sh({multisig})" if index == 7 else f"sh(wsh({multisig}))"
    return None


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--probe", required=True, type=Path)
    p.add_argument("--cases", required=True, type=Path)
    args = p.parse_args()
    for family in (socket.AF_INET, socket.AF_INET6):
        try:
            with socket.socket(family):
                raise AssertionError("network enabled in public policy test")
        except OSError as error:
            assert error.errno == errno.EPERM
    data = json.loads(args.cases.read_text())
    selected = [("finalizer", i, False) for i in range(10)]
    selected += [("signer", i, False) for i in (6, 7, 8, 17, 19)]
    selected += [("signer", 9, True), ("signer", 12, True), ("finalizer", 10, True)]
    results = []
    with tempfile.TemporaryDirectory(
        prefix="offline-policy-", dir=validated_scratch()
    ) as scratch:
        for group, index, rejected in selected:
            item = data[group][index]
            source = item["psbt" if group == "finalizer" else "result"]
            document = base64.b64decode(source, validate=True)
            fields = read_maps(document)
            # Each selected independent vector defines one input. Derive a
            # signature-free source by deleting only that input's signature keys.
            base = [dict(row) for row in fields]
            signatures = [key for key in base[1] if key[0] in (2, 19)]
            assert signatures
            for key in signatures:
                del base[1][key]
            folder = Path(scratch) / f"{group}-{index}"
            folder.mkdir()
            (folder / "base.psbt").write_bytes(pack(base))
            (folder / "signature.psbt").write_bytes(document)
            descriptor = review_descriptor(group, index, fields[1], signatures)
            if descriptor:
                (folder / "descriptor.txt").write_text(descriptor)
            if len(signatures) == 2 and not rejected:
                partial = [dict(row) for row in fields]
                del partial[1][signatures[-1]]
                (folder / "partial.psbt").write_bytes(pack(partial))
            run = subprocess.run(
                [
                    str(args.probe),
                    "policy",
                    str(folder),
                    "reject" if rejected else "accept",
                ],
                text=True,
                capture_output=True,
                timeout=6,
            )
            passed = run.returncode == 0 and not run.stderr
            observed = json.loads(run.stdout) if passed else None
            oracle = None
            if passed and descriptor:
                final = observed["final"]
                oracle = wire_facts(bytes.fromhex(final["wire"]))
                passed = all(final[key] == value for key, value in oracle.items())
            results.append(
                dict(
                    group=group,
                    index=index,
                    title=item["comment"],
                    expectedRejection=rejected,
                    passed=passed,
                    result=observed,
                    independentWireFacts=oracle,
                    stderr=run.stderr,
                    exit=run.returncode,
                )
            )
    print(
        json.dumps(
            dict(
                selected=len(selected),
                executed=len(results),
                passed=sum(r["passed"] for r in results),
                failed=sum(not r["passed"] for r in results),
                skipped=0,
                networkDenied=True,
                scratchRemoved=not Path(scratch).exists(),
                cases=results,
            )
        )
    )
    return int(any(not r["passed"] for r in results))


if __name__ == "__main__":
    raise SystemExit(main())
