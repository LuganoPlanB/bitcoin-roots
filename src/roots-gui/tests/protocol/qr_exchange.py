# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Bounded codec/assembly vectors and one network-denied NativeHost QR exchange."""

import argparse
import base64
from hashlib import sha256
import json
from pathlib import Path
import random
import re
from shutil import copyfile
import struct
import subprocess
import sys
import tempfile
import zlib

TESTS = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(TESTS), str(TESTS / "platform")]
from capture_manifest import encode_capture
from protocol.signature_exchange import (
    KEYS,
    OPAQUE,
    make_fixtures,
    now,
    record,
    wire_facts,
)
from rpc.funding_runner import maps
from support.runtime import (
    binary_identity,
    evidence_directory,
    validated_scratch,
    write_json,
)

ADDRESS = "bcrt1qrk5wn7dxwy8l6ru39h6xvmdhxnvdh8p0kpjlxw"
URI = "bitcoin:" + ADDRESS + "?amount=0.00001234&label=QR%20fixture"
WORDLIST = TESTS.parent / "third_party/qr/bytewords.inc"


def crc(value):
    return zlib.crc32(value)


def bytewords(data):
    alphabet = "".join(re.findall(r'"([a-z]+)"', WORDLIST.read_text()))
    assert len(alphabet) == 1024
    return "".join(
        alphabet[index * 4] + alphabet[index * 4 + 3]
        for index in data + struct.pack(">I", crc(data))
    )


def head(major, n):
    for tag, length in ((24, 1), (25, 2), (26, 4)):
        if n < 24:
            return bytes([(major << 5) | n])
        if n < 1 << (length * 8):
            return bytes([(major << 5) | tag]) + n.to_bytes(length, "big")
    raise ValueError("Bounded CBOR value")


def fragment(seq, count, length, check, part, kind="psbt"):
    header = (
        head(4, 5)
        + b"".join(head(0, n) for n in (seq, count, length, check))
        + head(2, len(part))
    )
    return f"ur:{kind}/{seq}-{count}/" + bytewords(header + part)


def systematic(wire, max_chunk=130):
    message = head(2, len(wire)) + wire
    count = (len(message) + max_chunk - 1) // max_chunk
    pitch = (len(message) + count - 1) // count
    padded = message.ljust(pitch * count, b"\0")
    result = [
        fragment(
            index + 1,
            count,
            len(message),
            crc(message),
            padded[index * pitch : (index + 1) * pitch],
        )
        for index in range(count)
    ]
    return message, pitch, result


def png(pgm, trailing=b"", mode="gray"):
    magic, dimension, maximum, pixels = pgm.split(b"\n", 3)
    cols, rows = map(int, dimension.split())
    assert magic == b"P5" and maximum == b"255" and len(pixels) == cols * rows

    def chunk(kind, value):
        return (
            struct.pack(">I", len(value))
            + kind
            + value
            + struct.pack(">I", crc(kind + value))
        )

    if mode == "adam7":
        lines = []
        for x0, y0, dx, dy in (
            (0, 0, 8, 8),
            (4, 0, 8, 8),
            (0, 4, 4, 8),
            (2, 0, 4, 4),
            (0, 2, 2, 4),
            (1, 0, 2, 2),
            (0, 1, 1, 2),
        ):
            for y in range(y0, rows, dy):
                if x0 < cols:
                    lines.append(
                        b"\0" + bytes(pixels[y * cols + x] for x in range(x0, cols, dx))
                    )
        raw = b"".join(lines)
        depth, color, interlaced = 8, 0, 1
    elif mode == "rgba16":

        def row16(start):
            return b"".join(
                bytes([v]) * 6 + b"\xff\xff" for v in pixels[start : start + cols]
            )

        raw = b"".join(b"\0" + row16(i) for i in range(0, len(pixels), cols))
        depth, color, interlaced = 16, 6, 0
    else:
        assert mode == "gray"
        raw = b"".join(
            b"\0" + pixels[i : i + cols] for i in range(0, len(pixels), cols)
        )
        depth, color, interlaced = 8, 0, 0
    header = struct.pack(">IIBBBBB", cols, rows, depth, color, 0, 0, interlaced)
    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", header)
        + chunk(b"IDAT", zlib.compress(raw + trailing))
        + chunk(b"IEND", b"")
    )


class Checks:
    def __init__(self, options, output):
        self.options = options
        self.output = output
        self.commands = []

    def run(self, label, *argv, expected=0):
        directory = self.output / label
        directory.mkdir()
        command = list(map(str, argv))
        started = now()
        done = subprocess.run(command, capture_output=True, timeout=40)
        self.commands.append(
            dict(
                label=label,
                argv=command,
                startedUTC=started,
                completedUTC=now(),
                exit=done.returncode,
                expected=expected,
                cwd=str(Path.cwd()),
            )
        )
        (directory / "stdout").write_bytes(done.stdout)
        (directory / "stderr").write_bytes(done.stderr)
        assert done.returncode == expected, (label, done.returncode, done.stderr)
        return done.stdout

    def probe(self, label, *args, expected=0):
        return self.run(
            label,
            self.options.offline_exec,
            self.options.probe,
            *args,
            expected=expected,
        )


def vectors(checks, args, fixtures):
    retained = checks.output
    case = fixtures
    published = args.vectors / "bcr-2020-006-psbt.txt"
    wire = bytes.fromhex((args.vectors / "bcr-2020-006-psbt.hex").read_text())
    static = {
        "address": ADDRESS.encode(),
        "uri": URI.encode(),
        "base64": base64.b64encode(wire),
        "ur": published.read_bytes().strip(),
    }
    for name, raw in static.items():
        text = case / (name + ".txt")
        raster = case / (name + ".png")
        text.write_bytes(raw)
        raster.write_bytes(png(checks.probe("paint-" + name, "paint", text)))
        assert checks.probe("read-" + name, "decode", raster) == raw
        if name in ("base64", "ur"):
            out = retained / (name + ".psbt")
            assert checks.probe("parse-" + name, "collect", out, text) == b"PSBT 0\n"
            assert out.read_bytes() == wire
        else:
            expect = (ADDRESS + "\n" + ("1234\n" if name == "uri" else "")).encode()
            assert checks.probe("parse-" + name, "payment", text) == expect
    for name, raw in {
        "wrong-network": "bc1qrk5wn7dxwy8l6ru39h6xvmdhxnvdh8p0d7mhde",
        "url": "https://example.invalid/bitcoin",
        "required-uri": "bitcoin:" + ADDRESS + "?req-other=1",
    }.items():
        text = case / (name + ".txt")
        text.write_text(raw)
        checks.probe(name, "payment", text, expected=1)

    for version in (0, 2):
        folder = fixtures / f"v{version}"
        original = (folder / "complete.psbt").read_bytes()
        decorated, xpub = with_xpub(original)
        augmented = folder / "decorated.psbt"
        augmented.write_bytes(decorated)
        stripped = checks.probe(f"remove-xpub-v{version}", "filter", augmented)
        assert stripped == original and augmented.read_bytes() == decorated
        assert xpub not in stripped
        (retained / f"filtered-v{version}.psbt").write_bytes(stripped)

    data = (fixtures / "v2/complete.psbt").read_bytes()
    msg, pitch, parts = systematic(data)
    _, _, mixed = systematic((fixtures / "v0/wrong.psbt").read_bytes())
    count = len(parts)
    bad_checksum = parts[0][:-1] + ("a" if parts[0][-1] != "a" else "b")
    conflict = fragment(
        1, count, len(msg), crc(msg), bytes([msg[0] ^ 1]) + msg[1:pitch]
    )
    fountain = fragment(count + 1, count, len(msg), crc(msg), msg[:pitch])
    wrong_type = parts[0].replace("ur:psbt", "ur:bytes")
    oversized = fragment(1, 65, 17000, crc(msg), msg[:pitch])
    wrong_full_crc = [
        fragment(
            i + 1,
            count,
            len(msg),
            crc(msg) ^ 1,
            msg.ljust(count * pitch, b"\0")[i * pitch : (i + 1) * pitch],
        )
        for i in range(count)
    ]
    external = {
        "checksum": bad_checksum,
        "conflict": conflict,
        "fountain": fountain,
        "wrong-type": wrong_type,
        "oversized": oversized,
        "bad-count": parts[0].replace("1-" + str(count), "01-" + str(count)),
        "other": mixed[0],
    }
    for i, raw in enumerate(parts):
        external[f"indexed-{i}"] = raw.upper() if i % 2 else raw
    external.update(
        {f"bad-message-{i}": value for i, value in enumerate(wrong_full_crc)}
    )
    for name, raw in external.items():
        (case / name).write_text(raw)

    def frame(name):
        return case / name

    result = retained / "reconstructed-v2.psbt"
    checks.probe("bounded-resource-inputs", "bounds", published)
    wrong_image = case / "malformed.png"
    wrong_image.write_bytes(b"\x89PNG\r\n\x1a\n")
    checks.probe("malformed-image", "decode", wrong_image, expected=1)
    trailing = retained / "unexpected-scanline-byte.png"
    pgm = (retained / "paint-address/stdout").read_bytes()
    trailing.write_bytes(png(pgm, b"\0"))
    good = retained / "valid-after-budget-refusal.png"
    good.write_bytes((case / "address.png").read_bytes())
    budget = checks.probe("png-budget", "png-budget", trailing, good)
    invalid, valid, concurrent = [
        json.loads(line) for line in budget.decode().splitlines()
    ]
    assert (
        invalid["rejectedInflate"]
        and not invalid["decoded"]
        and invalid["activeAfterRelease"] == 0
    )
    assert valid["decoded"] and valid["activeAfterRelease"] == 0
    assert (
        invalid["scanlineLimit"] == valid["scanlineLimit"]
        and invalid["peakBytes"] <= invalid["budgetBytes"]
    )
    assert concurrent == dict(parallelValidDecodes=2, activeAfterJoin=0)
    for mode in ("adam7", "rgba16"):
        alternate = retained / (mode + ".png")
        alternate.write_bytes(png(pgm, mode=mode))
        assert (
            checks.probe("preserve-" + mode, "decode", alternate) == static["address"]
        )
    attempts = list(range(count))
    random.Random(174370).shuffle(attempts)
    kept = [frame(f"indexed-{i}") for i in attempts]
    progress = checks.probe(
        "missing-and-duplicate", "collect", result, kept[0], kept[0], *kept[1:-1]
    )
    progress = progress.decode().splitlines()
    assert (
        progress[:2] == [f"1/{count}"] * 2
        and progress[-1] == f"{count - 1}/{count}"
        and not result.exists()
    )
    status = checks.probe(
        "random-order-restart", "collect", result, kept[0], "--reset", *kept
    )
    assert status.decode().splitlines()[-1] == "PSBT 2" and result.read_bytes() == data
    result.unlink()
    status = checks.probe(
        "mixed-session-and-headers",
        "collect",
        result,
        frame("indexed-0"),
        *map(
            frame,
            [
                "other",
                "conflict",
                "wrong-type",
                "fountain",
                "oversized",
                "checksum",
                "bad-count",
            ],
        ),
        "--expire",
        kept[-1],
    )
    for phrase in [
        "different session",
        "Conflicting UR fragments",
        "UR type is unavailable",
        "Mixed fountain",
        "64-frame limit",
        "checksum is invalid",
        "assembly limit expired",
    ]:
        assert phrase in status.decode(), (phrase, status)
    assert not result.exists()
    status = checks.probe(
        "assembled-message-checksum",
        "collect",
        result,
        *[frame(f"bad-message-{i}") for i in range(count)],
    )
    assert b"assembled UR checksum is invalid" in status and not result.exists()
    checks.probe(
        "completed-after-restart",
        "collect",
        result,
        kept[0],
        "--expire",
        kept[1],
        "--reset",
        *kept,
    )
    assert result.read_bytes() == data
    references = retained / "reference-frames"
    references.mkdir()
    for name, payload in external.items():
        (references / name).write_text(payload)
    (references / "systematic-source.psbt").write_bytes(data)
    return dict(
        indexedParts=count,
        shuffledOrder=attempts,
        officialStaticSha256=sha256(wire).hexdigest(),
        filteredVersions=[0, 2],
        pngBudget=[invalid, valid, concurrent],
        payment="1234 sat exact",
        completedWithoutNetwork=True,
    )


def with_xpub(wire):
    assert wire.startswith(b"psbt\xff")
    public = bytes.fromhex("043587cf") + bytes(9) + bytes([23]) * 32 + KEYS[0]
    assert len(public) == 78
    field = record(b"\x01" + public, bytes(4))
    return wire[:5] + field + wire[5:], field


def native(checks, args, fixtures):
    folder = fixtures / "v2"
    original = folder / "original.psbt"
    original_bytes = original.read_bytes()
    opaque = OPAQUE + b"\xa1"
    unsigned = (
        original_bytes[:5]
        + record(opaque, b"airgap global record")
        + original_bytes[5:]
    )
    decorated, _ = with_xpub(unsigned)
    (folder / "original-filtered.psbt").write_bytes(unsigned)
    original.write_bytes(decorated)
    wire = bytes.fromhex((args.vectors / "bcr-2020-006-psbt.hex").read_text())
    (folder / "static.psbt").write_bytes(wire)
    _, _, external = systematic((folder / "complete.psbt").read_bytes())
    random.Random(219).shuffle(external)
    for i, text in enumerate(external):
        (folder / f"inbound-{i}.txt").write_text(text)
    (folder / "frame-count.txt").write_text(str(len(external)))
    raster = png(checks.probe("external-qr-image", "paint", folder / "inbound-0.txt"))
    (folder / "inbound-0.png").write_bytes(raster)
    out = checks.output
    retained = out / "public-fixtures"
    retained.mkdir()
    for item in folder.iterdir():
        if item.is_file():
            copyfile(item, retained / item.name)
    argv = [
        sys.executable,
        "-B",
        TESTS / "platform/owned_display.py",
        "--output",
        out / "display",
        "--timeout",
        "31",
        "--",
        args.offline_exec,
        "--unix-only",
        args.native,
        out,
        args.fonts,
        folder,
        "--qr",
    ]
    checks.run("owned-native", *argv)
    report = json.loads((out / "native-result.json").read_text())
    display = json.loads((out / "display/processes.json").read_text())
    assert report["networkDenied"] and report["settled"]
    assert display["child"]["settled"] and all(
        p["settled"] and not p.get("remainingLiveGroupMembers")
        for p in display["processes"]
    )
    collected = maps((out / "collected.psbt").read_bytes())
    initial = maps(decorated)
    incoming = maps((folder / "complete.psbt").read_bytes())
    assert len(collected) == len(initial) == len(incoming)
    assert collected == [before | signed for before, signed in zip(initial, incoming)]
    assert collected[0][opaque] == b"airgap global record"
    wire = (out / "airgapped-final.tx").read_bytes()
    assert wire == (folder / "expected.tx").read_bytes()
    assert report["final"] == dict(
        wire_facts(wire), fee=500, sha256=sha256(wire).hexdigest()
    )
    report["independentFinalWireAndMapCheck"] = True
    names = (
        "qr-display-static",
        "qr-display-animated",
        "qr-collected",
        "qr-finalized",
        *[f"qr-out-{i}" for i in range(report["animatedFrames"])],
    )
    for name in names:
        encode_capture(out / (name + ".ppm"))
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in (
        "probe",
        "native",
        "fonts",
        "openssl",
        "offline-exec",
        "vectors",
        "output",
    ):
        parser.add_argument("--" + key, type=Path)
    args = parser.parse_args()
    assert (
        args.probe
        and args.openssl
        and args.offline_exec
        and args.vectors
        and args.output
    )
    out = Path(tempfile.mkdtemp(prefix="attempt-", dir=evidence_directory(args.output)))
    results = dict(
        startedUTC=now(),
        cwd=str(Path.cwd()),
        binary=binary_identity(args.native or args.probe),
    )
    checks = Checks(args, out)
    print("QR exchange evidence:", out, flush=True)
    error = None
    try:
        with tempfile.TemporaryDirectory(
            prefix="qr-exchange-", dir=validated_scratch()
        ) as private:
            fixtures = Path(private)
            make_fixtures(fixtures, args.openssl)
            results["result"] = (
                native(checks, args, fixtures)
                if args.native
                else vectors(checks, args, fixtures)
            )
    except (AssertionError, OSError, subprocess.SubprocessError, ValueError) as exc:
        error = str(exc)
        raise
    finally:
        results.update(
            completedUTC=now(),
            selected=1,
            executed=1,
            passed=int(error is None),
            failed=int(error is not None),
            skipped=0,
            error=error,
            commands=checks.commands,
            scratchRemoved=not Path(private).exists(),
        )
        write_json(out / "report.json", results)


if __name__ == "__main__":
    main()
