# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Audit delivered sources, include consistency and strict ImKit guards."""

import argparse
import hashlib
import json
from pathlib import Path
import shlex
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    bundled = Path(__file__).resolve().parents[2] / "third_party"
    lock = json.loads((bundled / "ui-lock.json").read_text())
    records = json.loads((bundled / lock["manifest"]).read_text())
    checks = []

    def check(name, condition):
        checks.append(dict(id=name, passed=bool(condition)))

    check(
        "archive-hash",
        hashlib.sha256((bundled / lock["archive"]).read_bytes()).hexdigest()
        == lock["sha256"],
    )
    check(
        "manifest-hash",
        hashlib.sha256((bundled / lock["manifest"]).read_bytes()).hexdigest()
        == lock["manifestSha256"],
    )
    for record in records:
        data = (args.source / record["path"]).read_bytes()
        check(
            "sha256." + record["path"],
            hashlib.sha256(data).hexdigest() == record["sha256"],
        )
        if record["gitBlob"]:
            check(
                "git-blob." + record["path"],
                hashlib.sha1(
                    b"blob " + str(len(data)).encode() + b"\0" + data
                ).hexdigest()
                == record["gitBlob"],
            )
    for path in [
        "imkit/LICENSE",
        "imkit/THIRD_PARTY_NOTICES.md",
        "imgui/LICENSE.txt",
        "glfw/LICENSE.md",
        "curl/COPYING",
        "imkit/assets/fonts/Inter-OFL.txt",
    ]:
        check("notice." + path, (args.source / path).stat().st_size > 100)
    for project in ["libXi", "libXrandr", "libXcursor", "libXinerama", "libXfixes"]:
        check(
            "notice." + project,
            (args.source / "x11/notices" / project / "COPYING").stat().st_size > 100,
        )
    check(
        "SQLite-public-domain",
        "The author disclaims copyright"
        in (args.source / "sqlite/sqlite3.c").read_text(),
    )
    commands = json.loads((args.build / "compile_commands.json").read_text())
    core = [x for x in commands if Path(x["file"]).parent == args.source / "imgui"]
    check(
        "exactly-four-core-units",
        {Path(x["file"]).name for x in core}
        == {"imgui.cpp", "imgui_draw.cpp", "imgui_widgets.cpp", "imgui_tables.cpp"}
        and len(core) == 4,
    )
    kit = [x for x in commands if str(args.source / "imkit/src") in x["file"]]
    check("exactly-fifteen-ImKit-core-units", len(kit) == 15)
    selected = (
        core
        + kit
        + [
            x
            for x in commands
            if "/imgui/backends/" in x["file"]
            or x["file"].endswith("/consumer.cpp")
            or x["file"].endswith("/native_consumer.cpp")
        ]
    )
    for x in selected:
        argv = shlex.split(x["command"])
        check(
            "same-imgui-include." + Path(x["file"]).name,
            "-I" + str(args.source / "imgui") in argv,
        )
        check(
            "same-default-layout." + Path(x["file"]).name,
            not any(s.startswith(("-DIMGUI_", "-DIMGUI_USER_CONFIG")) for s in argv),
        )
    check(
        "no-reference-or-Core-source",
        all(
            Path(x["file"]).is_relative_to(args.source)
            or Path(x["file"]).is_relative_to(Path(__file__).resolve().parent)
            for x in commands
        ),
    )
    dependencies = subprocess.check_output(
        ["ninja", "-C", str(args.build), "-t", "deps"], text=True
    )
    for name, relative in [
        ("imgui.h", "imgui/imgui.h"),
        ("imconfig.h", "imgui/imconfig.h"),
        ("version.h", "imkit/include/imkit/version.h"),
    ]:
        paths = {
            line.strip()
            for line in dependencies.splitlines()
            if line.startswith("    ")
            and (
                line.strip().endswith("/" + name)
                if name != "version.h"
                else line.strip().endswith("/imkit/version.h")
            )
        }
        check("actual-compiled-header." + name, paths == {str(args.source / relative)})
    raw = subprocess.check_output(
        ["ninja", "-C", str(args.build), "-t", "commands"], text=True
    )
    for number, line in enumerate(raw.splitlines()):
        if " -c " in line:
            check(
                "single-ccache." + str(number),
                sum(Path(x).name == "ccache" for x in shlex.split(line)) == 1,
            )
    compiler = next(
        x.split("=", 1)[1]
        for x in (args.build / "CMakeCache.txt").read_text().splitlines()
        if x.startswith("CMAKE_CXX_COMPILER:FILEPATH=")
    )
    # Synthetic header inputs isolate both real unchanged upstream guard branches.
    with tempfile.TemporaryDirectory(prefix="ui-guard-") as temporary:
        folder = Path(temporary)
        (folder / "probe.cpp").write_text("#include <imkit/version.h>\n")
        for name, version, texture, diagnostic in [
            ("wrong-version", 19303, True, "pinned Dear ImGui"),
            ("missing-textures", 19297, False, "dynamic texture support"),
        ]:
            (folder / "imgui.h").write_text(
                f"#define IMGUI_VERSION_NUM {version}\n#define IMGUI_HAS_DOCK\n#define IMGUI_HAS_VIEWPORT\n"
                + ("#define IMGUI_HAS_TEXTURES\n" if texture else "")
            )
            argv = [
                compiler,
                "-std=c++20",
                "-fsyntax-only",
                "-I" + str(folder),
                "-I" + str(args.source / "imkit/include"),
                str(folder / "probe.cpp"),
            ]
            result = subprocess.run(argv, capture_output=True, text=True)
            check(
                "strict-guard." + name,
                result.returncode != 0 and diagnostic in result.stderr,
            )
            checks[-1].update(
                argv=argv,
                exit=result.returncode,
                expectedExit="nonzero",
                diagnostic=result.stderr,
            )
    report = dict(
        executed=len(checks),
        passed=sum(x["passed"] for x in checks),
        failed=sum(not x["passed"] for x in checks),
        skipped=0,
        checks=checks,
    )
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(
        json.dumps({k: report[k] for k in ["executed", "passed", "failed", "skipped"]})
    )
    return bool(report["failed"])


if __name__ == "__main__":
    raise SystemExit(main())
