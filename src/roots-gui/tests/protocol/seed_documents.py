# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Seed the isolated fuzzer with official containers and public transaction data."""

import argparse
import base64
import hashlib
from pathlib import Path
from check_documents import vectors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bips", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir()
    seeds = set()
    for _bip, _index, _valid, _title, text in vectors(args.bips):
        seeds.add(text.encode("ascii"))
        try:
            seeds.add(base64.b64decode(text, validate=True))
        except ValueError:
            pass
    hex_tx = (
        "0200000001" + "11" * 32 + "0000000000feffffff010100000000000000"
        "160014751e76e8199196d454941c45d1b3a323f1433bd600000000"
    )
    seeds.update(
        (
            hex_tx.encode(),
            bytes.fromhex(hex_tx),
            bytes.fromhex("70736274ff01fb040300000000"),
            b"wpkh(0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798)",
        )
    )
    for seed in sorted(seeds):
        (args.output / hashlib.sha256(seed).hexdigest()).write_bytes(seed)
    print(f"retained {len(seeds)} seed files and {sum(map(len, seeds))} bytes")


if __name__ == "__main__":
    main()
