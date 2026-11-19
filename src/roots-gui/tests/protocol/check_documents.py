# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Check the actual application adapter with the pinned official BIP PSBT corpus."""

import argparse
import base64
import errno
import re
import socket
import subprocess
import tempfile
from pathlib import Path
import json
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from support.runtime import validated_scratch


def vectors(bips):
    for bip in (174, 370, 371):
        text = (bips / f"bip-{bip:04}.mediawiki").read_text()
        part = text.split("==Test Vectors==", 1)[1].split("==Rationale==", 1)[0]
        valid, title, index = None, "", 0
        for line in part.splitlines():
            lower = line.lower()
            if "following are invalid psbts" in lower:
                valid = False
            elif "following are valid psbts" in lower:
                valid = True
            elif (
                "following test vectors" in lower
                or "lock time" in lower
                and line.startswith("The following")
            ):
                valid = None
            if line.startswith("* Case:"):
                title = line[7:].strip()
            match = re.search(r"Base64 String: <pre>([^<]+)</pre>", line)
            if match and valid is not None:
                index += 1
                yield bip, index, valid, title, match.group(1).strip()
        assert index > 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--bips", type=Path, required=True)
    args = parser.parse_args()
    for family in (socket.AF_INET, socket.AF_INET6):
        try:
            with socket.socket(family):
                raise AssertionError("test was run without network denial")
        except OSError as error:
            assert error.errno == errno.EPERM
    result = subprocess.run(
        [str(args.probe)], text=True, capture_output=True, timeout=10
    )
    print(result.stdout, end="")
    if result.returncode or result.stderr:
        print(result.stderr, end="")
    with tempfile.TemporaryDirectory(
        prefix="offline-codecs-", dir=validated_scratch()
    ) as scratch:
        local = subprocess.run(
            [str(args.probe), "--local", scratch],
            text=True,
            capture_output=True,
            timeout=10,
        )
        print(local.stdout, end="")
        if local.returncode or local.stderr:
            print(local.stderr, end="")
    failures = []
    counts = {}
    for bip, index, valid, title, text in vectors(args.bips):
        run = subprocess.run(
            [str(args.probe), "--parse", text],
            text=True,
            capture_output=True,
            timeout=10,
        )
        parsed, _, data = run.stdout.strip().partition(" ")
        passed = (
            run.returncode == 0
            and not run.stderr
            and parsed == ("accepted" if valid else "rejected")
        )
        if valid and passed:
            passed = base64.b64decode(data, validate=True) == base64.b64decode(
                text, validate=True
            )
        counts.setdefault(bip, [0, 0])[0] += 1
        counts[bip][1] += int(passed)
        print(f"{'PASS' if passed else 'FAIL'} bip{bip}.{index:03} {title} -> {parsed}")
        if not passed:
            failures.append(
                dict(
                    bip=bip,
                    index=index,
                    title=title,
                    exit=run.returncode,
                    detail=data if parsed != "accepted" else "accepted invalid",
                    stderr=run.stderr,
                )
            )
    print(
        json.dumps(
            dict(
                networkDenied=True,
                vectorCounts=counts,
                unitExit=result.returncode,
                localExit=local.returncode,
                scratchRemoved=not Path(scratch).exists(),
                failures=failures,
            )
        )
    )
    return int(
        bool(failures)
        or result.returncode != 0
        or bool(result.stderr)
        or local.returncode != 0
        or bool(local.stderr)
    )


if __name__ == "__main__":
    raise SystemExit(main())
