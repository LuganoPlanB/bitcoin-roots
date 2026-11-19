# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Losslessly encode actual PPM readbacks as PNG and inventory native captures."""

import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib


def encode_capture(ppm):
    magic, dimensions, maximum, data = ppm.read_bytes().split(b"\n", 3)
    width, height = map(int, dimensions.split())
    if magic != b"P6" or maximum != b"255" or len(data) != width * height * 3:
        raise ValueError("invalid raw framebuffer capture")

    def chunk(name, payload):
        return (
            struct.pack("!I", len(payload))
            + name
            + payload
            + struct.pack("!I", zlib.crc32(name + payload))
        )

    scanlines = b"".join(
        b"\0" + data[y * width * 3 : (y + 1) * width * 3] for y in range(height)
    )
    encoded = (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack("!2I5B", width, height, 8, 2, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(scanlines, 6))
        + chunk(b"IEND", b"")
    )
    png = ppm.with_suffix(".png")
    png.write_bytes(encoded)
    return dict(
        path=png.name,
        sha256=hashlib.sha256(encoded).hexdigest(),
        bytes=len(encoded),
        width=width,
        height=height,
        rawPath=ppm.name,
        rawSha256=hashlib.sha256(ppm.read_bytes()).hexdigest(),
        kind="native-window-frame-root-crop"
        if ppm.stem.endswith("-frame")
        else "production-client-OpenGL-framebuffer",
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--expected-dpi", type=float, default=1)
    args = parser.parse_args()
    records = [encode_capture(ppm) for ppm in sorted(args.directory.glob("*.ppm"))]
    matrix = sorted(args.directory.glob("bootstrap-*.json"))
    if len(matrix) != 12:
        raise ValueError("expected twelve visual matrix records")
    for path in matrix:
        record = json.loads(path.read_text())
        if abs(record["dpi"] - args.expected_dpi) > 0.01:
            raise ValueError("native DPI observation differs from requested profile")
        if not record["teardown"]:
            raise ValueError("incomplete scenario teardown")
    manifest = dict(
        schema=1,
        renderer="actual production NativeHost + DrawBootstrap; software Mesa/owned Xvfb + real Marco WM",
        expectedDpi=args.expected_dpi,
        matrixCases=12,
        captures=records,
        encoding="PPM RGB pixels encoded losslessly with PNG filter0/zlib; no generated or edited UI pixels",
        metadata="Case JSON contains final scenario state; lifecycle PPM was captured before resize/font transitions. Matrix capture is its final frame.",
    )
    (args.directory / "capture-manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n"
    )
    print(
        json.dumps(
            {"matrixCases": 12, "captures": len(records), "failed": 0, "skipped": 0}
        )
    )


if __name__ == "__main__":
    main()
