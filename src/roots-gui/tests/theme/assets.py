# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Pinned published brand sources and unmodified identity geometry/notices."""

import hashlib
import json
from pathlib import Path
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
ASSETS = ROOT / "assets/brand"
checks = []


def check(value, identifier):
    checks.append((identifier, bool(value)))


for name, digest in [
    ("tokens.json", "2ade02257affd47398579383bdcb816f370c2848b18b4d135777d7b96dd328ef"),
    ("brand.json", "2053ae7a0e8105ee796c2546eebbef5743e77e221020877fd00b21125adc044a"),
]:
    check(
        hashlib.sha256((ASSETS / name).read_bytes()).hexdigest() == digest,
        "reviewed source " + name,
    )
manifest = json.loads((ASSETS / "roots-manifest.json").read_text())
svgs = [asset for asset in manifest["assets"] if asset["path"].endswith(".svg")]
check(len(svgs) == 3, "three exact reviewed SVG identities")
for asset in svgs:
    path = ASSETS / Path(asset["path"]).name
    check(
        hashlib.sha256(path.read_bytes()).hexdigest() == asset["sha256"],
        "original SVG bytes " + path.name,
    )
    check(
        ET.parse(path).getroot().get("viewBox") == "0 0 512 512",
        "intrinsic proportions " + path.name,
    )
check(
    manifest["orange"] == "#F57C00", "artwork orange independent of exceptional signal"
)
check(
    "rights" in manifest and "existing rights" in manifest["rights"],
    "original asset rights retained",
)
tokens = json.loads((ASSETS / "tokens.json").read_text())
check(
    tokens["light"]["--planb-color-canvas"] == "#fffefa", "published light-first canvas"
)
check(
    tokens["dark"]["--planb-color-accent"] == "#e15364",
    "published dark interaction coral",
)
check("Inter" in tokens["light"]["--planb-font-family"], "published Inter identity")
check(
    "SIL OFL" in (ASSETS / "NOTICES.md").read_text()
    and "ProggyForever" in (ASSETS / "NOTICES.md").read_text(),
    "font source notices",
)
failed = [name for name, passed in checks if not passed]
print(
    json.dumps(
        dict(
            selected=len(checks),
            executed=len(checks),
            passed=len(checks) - len(failed),
            failed=len(failed),
            skipped=0,
            failures=failed,
        )
    )
)
raise SystemExit(bool(failed))
