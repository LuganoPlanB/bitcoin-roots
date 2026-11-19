# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license; see COPYING.
"""Exercise the pinned provider with public vectors, under offline_exec."""

import argparse
import base64
import csv
import errno
import html
import json
from pathlib import Path
import re
import socket
import subprocess
import sys
import unittest


class Case(unittest.TestCase):
    def __init__(self, name, callback):
        super().__init__()
        self.name = name
        self.callback = callback

    def id(self):
        return self.name

    def shortDescription(self):
        return self.name

    def runTest(self):
        self.callback(self)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--bips", type=Path, required=True)
    parser.add_argument("--provider-source", type=Path, required=True)
    args = parser.parse_args()
    probe = args.probe.resolve()
    tests = []

    def run(*arguments):
        result = subprocess.run(
            [str(probe), *arguments], capture_output=True, text=True, timeout=10
        )
        if result.returncode:
            raise AssertionError(
                f"probe {arguments[0]} exit {result.returncode}: {result.stderr[:300]}"
            )
        return result.stdout.strip()

    def add(name, callback):
        tests.append(Case(name, callback))

    def network(test):
        for family in [socket.AF_INET, socket.AF_INET6]:
            with test.assertRaises(OSError) as error:
                socket.socket(family)
            test.assertEqual(error.exception.errno, errno.EPERM)

    add("offline.network_denied", network)
    add(
        "provider.construct_v0_v2_preserve_unknown_merge_incomplete",
        lambda t: t.assertIn("passed", run("self-test")),
    )

    # Extract only the explicitly valid/invalid parser vectors. Later locktime
    # determination examples in BIP370 are separate behavioral examples.
    for bip in [174, 370, 371]:
        text = (args.bips / f"bip-{bip:04}.mediawiki").read_text()
        text = text.split("==Test Vectors==", 1)[1].split("==Rationale==", 1)[0]
        valid = None
        title = ""
        index = 0
        for line in text.splitlines():
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
                title = html.unescape(re.sub("<[^>]+>", "", line[7:])).strip()
            match = re.search(r"Base64 String: <pre>([^<]+)</pre>", line)
            if match and valid is not None:
                encoded = match.group(1).strip()
                expected = "accepted" if valid else "rejected"
                index += 1
                add(
                    f"bip{bip}.{index:03}.{expected}.{title}",
                    lambda t, value=encoded, wanted=expected: t.assertEqual(
                        run("parse", value), wanted
                    ),
                )
                if (
                    valid
                    and bip == 370
                    and ("undefined flag" in title or "all possible" in title)
                ):

                    def future_bits(test, value=encoded):
                        def globals_map(raw):
                            position, fields = 5, {}
                            # These two curated BIP vectors use one-byte lengths.
                            while raw[position]:
                                key_len = raw[position]
                                position += 1
                                test.assertLess(key_len, 253)
                                key = raw[position : position + key_len]
                                position += key_len
                                value_len = raw[position]
                                position += 1
                                test.assertLess(value_len, 253)
                                fields[key] = raw[position : position + value_len]
                                position += value_len
                            return fields

                        before = globals_map(base64.b64decode(value))
                        after = globals_map(base64.b64decode(run("roundtrip", value)))
                        test.assertEqual(before[b"\x06"], after[b"\x06"])

                    add(f"bip370.{index:03}.reserved_bits_preserved", future_bits)
        if not index:
            raise RuntimeError(f"zero BIP{bip} vectors selected")

    public_key = "0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798"
    xpub = "xpub69H7F5d8KSRgmmdJg2KhpAK8SR3DjMwAdkxj3ZuxV27CprR9LgpeyGmXUbC6wb7ERfvrnKZjXoUmmDznezpbZb7ap6r1D3tgFxHmwMkQTPH"

    def descriptor(test):
        address, script = run("descriptor", f"wpkh({public_key})", "0").split("\t")
        test.assertTrue(address.startswith("bc1q"))
        test.assertEqual(script, "0014751e76e8199196d454941c45d1b3a323f1433bd6")
        for expression in [
            f"sh(wpkh({public_key}))",
            f"wsh(sortedmulti(1,{public_key}))",
            f"tr({public_key[2:]})",
            f"tr({public_key[2:]},pk({public_key[2:]}))",
        ]:
            derived = run("descriptor", expression, "0").split("\t")
            test.assertEqual(len(derived), 2)
            test.assertTrue(derived[0] and derived[1])
        zero = run("descriptor", f"wpkh({xpub}/0/*)", "0")
        one = run("descriptor", f"wpkh({xpub}/0/*)", "1")
        test.assertNotEqual(zero, one)

    add("descriptor.public_static_ranged_nested_multisig_taproot", descriptor)

    data = json.loads((args.provider_source / "src/data/psbt.json").read_text())
    # Use public signed results only: no private key is passed to the probe.
    for index in [6, 7, 8, 9, 12, 13, 14, 15, 16, 17, 18, 19]:
        case = data["signer"][index]
        add(
            "signature." + case["comment"],
            lambda t, value=case["result"]: t.assertEqual(
                run("verify", value), "valid"
            ),
        )
        add(
            "signature_reject_changed_output." + case["comment"],
            lambda t, value=case["result"]: t.assertEqual(
                run("verify-changed-output", value), "invalid"
            ),
        )

    for index in [4, 5, 6, 7]:
        case = data["finalizer"][index]
        add(
            "legacy_signature." + case["comment"],
            lambda t, value=case["psbt"]: t.assertEqual(run("verify", value), "valid"),
        )
        add(
            "legacy_signature_reject_changed_output." + case["comment"],
            lambda t, value=case["psbt"]: t.assertEqual(
                run("verify-changed-output", value), "invalid"
            ),
        )

    # Each finalizer vector supplies an independently serialized expected PSBT.
    # Finalizing either representation must extract identical transaction bytes.
    for index, case in enumerate(data["finalizer"]):
        if case.get("is_pset"):
            continue
        add(
            f"finalize.{index:02}.{case.get('comment', 'public vector')}",
            lambda t, item=case: t.assertEqual(
                run("finalize", item["psbt"]), run("finalize", item["result"])
            ),
        )
    for index, case in enumerate(data["combiner"]):

        def combine(test, item=case):
            value = item["psbts"][0]
            for other in item["psbts"][1:]:
                value = run("combine", value, other)
            test.assertEqual(run("roundtrip", value), run("roundtrip", item["result"]))

        add(f"combine.{index:02}", combine)

    with (args.bips / "bip340-test-vectors.csv").open(newline="") as stream:
        for row in csv.DictReader(stream):
            if len(row["message"]) != 64:
                continue  # API takes a 32-byte message hash; longer-message API is not claimed.
            wanted = "valid" if row["verification result"] == "TRUE" else "invalid"
            add(
                "bip340." + row["index"],
                lambda t, item=row, expected=wanted: t.assertEqual(
                    run(
                        "schnorr",
                        item["public key"],
                        item["message"],
                        item["signature"],
                    ),
                    expected,
                ),
            )

    if not tests:
        raise RuntimeError("zero provider checks")
    result = unittest.TextTestRunner(verbosity=2).run(unittest.TestSuite(tests))
    print(
        f"selected={len(tests)} executed={result.testsRun} passed={result.testsRun - len(result.failures) - len(result.errors) - len(result.skipped)} "
        f"failed={len(result.failures) + len(result.errors)} skipped={len(result.skipped)}"
    )
    return 0 if result.wasSuccessful() and result.testsRun == len(tests) else 1


if __name__ == "__main__":
    sys.exit(main())
