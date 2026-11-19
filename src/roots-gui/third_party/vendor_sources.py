# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license; see COPYING.
"""Explicit maintainer operation: package already acquired, pinned source inputs.

Normal configure/build/test must never invoke this script or access the network.
Git archives read committed objects, preserving upstream bytes despite local
probe patches. Submodule source is included; Git metadata is excluded.
"""

import argparse
import gzip
import hashlib
import io
import json
from pathlib import Path
import subprocess
import tarfile

WALLY = "f7b0a15b8d6fc79a5c4ed0123955dd482934483e"
SECP = "8e1f96c20e16bf960be6caec7f3c94acb360cdf2"
BIPS = "b34d9f751791d4feb22feff4f2ed00bcac83961b"


def git(path, *args):
    return subprocess.check_output(["git", "-C", str(path), *args])


def pack(destination, entries):
    with (
        destination.open("wb") as stream,
        gzip.GzipFile(fileobj=stream, mode="wb", mtime=0, filename="") as compressed,
    ):
        with tarfile.open(
            fileobj=compressed, mode="w", format=tarfile.PAX_FORMAT
        ) as archive:
            for name, (item, data) in sorted(entries.items()):
                item.name = name
                item.uid = item.gid = item.mtime = 0
                item.uname = item.gname = ""
                item.pax_headers = {}
                archive.addfile(item, io.BytesIO(data) if data is not None else None)
    return {
        "archive": destination.name,
        "sha256": hashlib.sha256(destination.read_bytes()).hexdigest(),
        "bytes": destination.stat().st_size,
        "entries": len(entries),
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--wally-checkout", type=Path, required=True)
    parser.add_argument("--bips-input", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent
    if git(args.wally_checkout, "rev-parse", "HEAD").decode().strip() != WALLY:
        raise RuntimeError("wrong libwally revision")
    secp = args.wally_checkout / "src/secp256k1"
    if git(secp, "rev-parse", "HEAD").decode().strip() != SECP:
        raise RuntimeError("wrong secp256k1-zkp revision")
    entries = {}
    for checkout, revision, prefix in [
        (args.wally_checkout, WALLY, ""),
        (secp, SECP, "src/secp256k1/"),
    ]:
        raw = git(checkout, "archive", "--format=tar", revision)
        with tarfile.open(fileobj=io.BytesIO(raw)) as archive:
            for item in archive:
                data = archive.extractfile(item).read() if item.isfile() else None
                entries["libwally-core-1.6.1/" + prefix + item.name] = (item, data)
    library = pack(root / "libwally-core-1.6.1.tar.gz", entries)
    entries = {}
    inputs = []
    for name in [
        "bip-0174.mediawiki",
        "bip-0340.mediawiki",
        "bip-0370.mediawiki",
        "bip-0371.mediawiki",
        "bip340-test-vectors.csv",
    ]:
        data = (args.bips_input / name).read_bytes()
        item = tarfile.TarInfo("bips-provider-vectors/" + name)
        item.size, item.mode = len(data), 0o644
        entries[item.name] = (item, data)
        upstream = (
            "bip-0340/test-vectors.csv" if name == "bip340-test-vectors.csv" else name
        )
        inputs.append(
            {
                "path": name,
                "url": f"https://raw.githubusercontent.com/bitcoin/bips/{BIPS}/{upstream}",
                "sha256": hashlib.sha256(data).hexdigest(),
            }
        )
    vectors = pack(root / "bips-provider-vectors.tar.gz", entries)
    patch = root / "patches/libwally-1.6.1-psbt-conformance.patch"
    lock = {
        "schema": 1,
        "normal_build_network": "forbidden",
        "libwally": {
            **library,
            "url": "https://github.com/ElementsProject/libwally-core",
            "tag": "release_1.6.1",
            "revision": WALLY,
            "license": "MIT; CCAN CC0/MIT/BSD-2-Clause; Android/Wycheproof Apache-2.0; unused Autoconf macros GPL-3.0-or-later with retained exceptions",
            "secp256k1_zkp": {
                "url": "https://github.com/BlockstreamResearch/secp256k1-zkp",
                "revision": SECP,
                "license": "MIT",
            },
            "patches": [
                {
                    "path": str(patch.relative_to(root)),
                    "sha256": hashlib.sha256(patch.read_bytes()).hexdigest(),
                }
            ],
        },
        "bips_vectors": {
            **vectors,
            "revision": BIPS,
            "license": "BSD-2-Clause; copyright/license statements preserved in source documents",
            "inputs": inputs,
        },
    }
    lock["license_texts"] = [
        {
            "path": str(p.relative_to(root)),
            "sha256": hashlib.sha256(p.read_bytes()).hexdigest(),
        }
        for p in sorted((root / "licenses").glob("*.txt"))
    ]
    lock["license_sources"] = {
        "GPL-3.0.txt": "Debian /usr/share/common-licenses/GPL-3; GNU GPL version 3 full text",
        "Apache-2.0.txt": "Debian /usr/share/common-licenses/Apache-2.0; Apache license version 2.0 full text",
        "Autoconf-exception-3.0.txt": "https://raw.githubusercontent.com/spdx/license-list-data/31846481de7ea17d92f2f46fad76abe58b2e9bb0/text/Autoconf-exception-3.0.txt",
    }
    (root / "provider-lock.json").write_text(json.dumps(lock, indent=2) + "\n")
    print(json.dumps({"libwally": library, "bips": vectors}))


if __name__ == "__main__":
    main()
