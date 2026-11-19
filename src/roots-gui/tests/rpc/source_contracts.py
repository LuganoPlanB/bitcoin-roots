# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license; see COPYING.
"""Compare actual RPC help defaults/registrations in two exact source trees.

Core source inspection is distinct from live Core binary qualification.
"""

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--repository", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    fixture = Path(__file__).resolve().parents[1] / "fixtures/rpc-contracts.json"
    spec = json.loads(fixture.read_text())
    defaults = re.compile(
        r'\{"([^"\n]+)",\s*RPCArg::Type::\w+,\s*RPCArg::Default[({]([^})]+)[})]'
    )
    checks = []
    for baseline in ["roots", "core"]:
        revision = spec[baseline + "_commit"]
        for contract in spec["contracts"]:
            method, path = contract["method"], contract["file"]
            data = subprocess.check_output(
                ["git", "-C", str(args.repository), "show", revision + ":" + path]
            )
            source = data.decode()
            if baseline not in contract.get("baselines", ["roots", "core"]):
                absent = not re.search(
                    r"\bRPCMethod\s+" + re.escape(method) + r"\s*\(", source
                )
                checks.append(
                    {
                        "id": baseline + "." + method + ".absent",
                        "passed": absent,
                        "revision": revision,
                        "file": path,
                        "source_sha256": hashlib.sha256(data).hexdigest(),
                    }
                )
                continue
            definition = contract.get("definition", method)
            match = re.search(
                r"\bRPCMethod\s+" + re.escape(definition) + r"\s*\(", source
            )
            if not match:
                raise RuntimeError(f"missing definition {baseline}/{method}")
            body = source[match.start() :]
            body = re.split(r"RPCResults?\s*[({]|RPCExamples\s*[({]", body, maxsplit=1)[
                0
            ]
            actual = dict(defaults.findall(body))
            for shared in contract.get("shared_args", []):
                shared_match = re.search(
                    r"\b" + re.escape(shared) + r"\s*\(\s*\)\s*\n\{", source
                )
                if not shared_match:
                    raise RuntimeError("missing shared RPC argument helper")
                shared_body = source[shared_match.end() :].split("\n}\n", 1)[0]
                actual = {**dict(defaults.findall(shared_body)), **actual}
            mismatches = {
                key: {"expected": value, "actual": actual.get(key)}
                for key, value in contract["defaults"].items()
                if actual.get(key) != value
            }
            registry_path = contract.get(
                "registry",
                "src/wallet/rpc/wallet.cpp"
                if "/wallet/" in path
                else "src/rpc/rawtransaction.cpp",
            )
            registry = subprocess.check_output(
                [
                    "git",
                    "-C",
                    str(args.repository),
                    "show",
                    revision + ":" + registry_path,
                ]
            ).decode()
            registered = bool(re.search(r"&" + re.escape(method) + r"\b", registry))
            checks.append(
                {
                    "id": baseline + "." + method,
                    "passed": not mismatches and registered,
                    "revision": revision,
                    "file": path,
                    "line": source[: match.start()].count("\n") + 1,
                    "source_sha256": hashlib.sha256(data).hexdigest(),
                    "defaults": actual,
                    "mismatches": mismatches,
                    "registered": registered,
                }
            )
        for path, declaration in [
            (
                "src/wallet/coincontrol.h",
                "DEFAULT_WALLET_TX_VERSION = CTransaction::CURRENT_VERSION",
            ),
            (
                "src/rpc/rawtransaction.cpp",
                "DEFAULT_RAWTX_VERSION{CTransaction::CURRENT_VERSION}",
            ),
            ("src/primitives/transaction.h", "CURRENT_VERSION{2}"),
        ]:
            data = subprocess.check_output(
                ["git", "-C", str(args.repository), "show", revision + ":" + path]
            )
            checks.append(
                {
                    "id": baseline + ".constant." + path,
                    "passed": declaration in data.decode(),
                    "revision": revision,
                    "file": path,
                    "source_sha256": hashlib.sha256(data).hexdigest(),
                }
            )
    result = {
        "selected": len(checks),
        "executed": len(checks),
        "passed": sum(x["passed"] for x in checks),
        "failed": sum(not x["passed"] for x in checks),
        "skipped": 0,
        "checks": checks,
        "coverage": "Roots/Core source registration and scalar defaults; live Core binary not executed",
    }
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    print(
        json.dumps(
            {
                k: result[k]
                for k in ["selected", "executed", "passed", "failed", "skipped"]
            }
        )
    )
    for check in checks:
        if not check["passed"]:
            print(
                check["id"],
                check.get("mismatches"),
                "registered",
                check.get("registered"),
            )
    return bool(result["failed"])


if __name__ == "__main__":
    raise SystemExit(main())
