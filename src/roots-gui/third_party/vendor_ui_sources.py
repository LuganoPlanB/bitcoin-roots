# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Explicit maintainer packaging from pinned Git objects and acquired source archives.

No network access. Never run during configure. See docs/build-inputs.md.
"""

import argparse
import gzip
import hashlib
import io
import json
from pathlib import Path
import re
import subprocess
import tarfile
import zipfile

IMKIT = "986d8d286a74fb7b2f724b9ef5d008af139aa887"
IMGUI = "367b2c24f399988ddafc0bb4628da0106bcc09be"
CORE = "widgets theme theme_text decor components icons accessibility patterns workflow toast charts range_slider ptz shell window_frame".split()
ARCHIVES = {
    "glfw-upstream.tar.gz": (
        "glfw",
        "https://codeload.github.com/glfw/glfw/tar.gz/d9d6f0f1f967807ffade6598ea9a631ebaf37a56",
        "96722514789b1aa1ad9c4debd874450156ea457aea691e546ffaf5b53892d5a3",
    ),
    "curl-8.23.0.tar.xz": (
        "curl",
        "https://curl.se/download/curl-8.23.0.tar.xz",
        "8dadd7d2536a314f982ad05a651b713f426bf7d02cc2c76cffb9796f2218e6c5",
    ),
    "sqlite-amalgamation-3540100.zip": (
        "sqlite",
        "https://www.sqlite.org/2026/sqlite-amalgamation-3540100.zip",
        "0a9c8e34352dad46006aa82836d5383bff9dc6bc9ce5a60553c05aa30e6ad1f9",
    ),
    "libXcursor-1.2.3.tar.xz": (
        "libXcursor",
        "https://www.x.org/releases/individual/lib/libXcursor-1.2.3.tar.xz",
        "fde9402dd4cfe79da71e2d96bb980afc5e6ff4f8a7d74c159e1966afb2b2c2c0",
    ),
    "libXrandr-1.5.4.tar.xz": (
        "libXrandr",
        "https://www.x.org/releases/individual/lib/libXrandr-1.5.4.tar.xz",
        "1ad5b065375f4a85915aa60611cc6407c060492a214d7f9daf214be752c3b4d3",
    ),
    "libXinerama-1.1.5.tar.xz": (
        "libXinerama",
        "https://www.x.org/releases/individual/lib/libXinerama-1.1.5.tar.xz",
        "5094d1f0fcc1828cb1696d0d39d9e866ae32520c54d01f618f1a3c1e30c2085c",
    ),
    "libXi-1.8.2.tar.xz": (
        "libXi",
        "https://www.x.org/releases/individual/lib/libXi-1.8.2.tar.xz",
        "d0e0555e53d6e2114eabfa44226ba162d2708501a25e18d99cfb35c094c6c104",
    ),
    "libXfixes-6.0.1.tar.xz": (
        "libXfixes",
        "https://www.x.org/releases/individual/lib/libXfixes-6.0.1.tar.xz",
        "b695f93cd2499421ab02d22744458e650ccc88c1d4c8130d60200213abc02d58",
    ),
}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--imkit-repository", type=Path, required=True)
    ap.add_argument("--imgui-repository", type=Path, required=True)
    ap.add_argument("--acquired-inputs", type=Path, required=True)
    args = ap.parse_args()
    output = Path(__file__).resolve().parent
    files, records = {}, []

    def add(path, data, source, original, blob=None):
        if path in files:
            raise ValueError("duplicate " + path)
        files[path] = data
        records.append(
            dict(
                path=path,
                sha256=hashlib.sha256(data).hexdigest(),
                bytes=len(data),
                source=source,
                original=original,
                gitBlob=blob,
            )
        )

    def git_bytes(repo, revision, path):
        return subprocess.check_output(["git", "show", revision + ":" + path], cwd=repo)

    def git_add(repo, revision, path, prefix):
        blob = subprocess.check_output(
            ["git", "rev-parse", revision + ":" + path], cwd=repo, text=True
        ).strip()
        add(
            prefix + "/" + path,
            git_bytes(repo, revision, path),
            prefix + "@" + revision,
            path,
            blob,
        )

    # Core public include closure and exact implementation-local support files only.
    pending = ["include/imkit/imkit.h"] + ["src/" + x + ".cpp" for x in CORE]
    seen = set()
    while pending:
        path = pending.pop()
        if path in seen:
            continue
        seen.add(path)
        data = git_bytes(args.imkit_repository, IMKIT, path)
        git_add(args.imkit_repository, IMKIT, path, "imkit")
        for delim, include in re.findall(rb'^#include\s+([<"])([^">]+)', data, re.M):
            name = include.decode()
            if name.startswith("imkit/"):
                pending.append("include/" + name)
            elif delim == b'"':
                pending.append(str(Path(path).parent / name))
    for path in [
        "LICENSE",
        "THIRD_PARTY_NOTICES.md",
        "CMakeLists.txt",
        "assets/fonts/Inter-Regular.ttf",
        "assets/fonts/Inter-SemiBold.ttf",
        "assets/fonts/Inter-OFL.txt",
    ]:
        git_add(args.imkit_repository, IMKIT, path, "imkit")
    for path in [
        "imgui.cpp",
        "imgui_draw.cpp",
        "imgui_widgets.cpp",
        "imgui_tables.cpp",
        "imgui.h",
        "imgui_internal.h",
        "imconfig.h",
        "imstb_rectpack.h",
        "imstb_textedit.h",
        "imstb_truetype.h",
        "LICENSE.txt",
        "backends/imgui_impl_glfw.cpp",
        "backends/imgui_impl_glfw.h",
        "backends/imgui_impl_opengl3.cpp",
        "backends/imgui_impl_opengl3.h",
        "backends/imgui_impl_opengl3_loader.h",
        "backends/imgui_impl_metal.mm",
        "backends/imgui_impl_metal.h",
        "examples/example_glfw_opengl3/main.cpp",
    ]:
        git_add(args.imgui_repository, IMGUI, path, "imgui")
    for filename, (name, url, expected) in ARCHIVES.items():
        raw = (args.acquired_inputs / filename).read_bytes()
        if hashlib.sha256(raw).hexdigest() != expected:
            raise ValueError("archive hash " + filename)
        if filename.endswith(".zip"):
            with zipfile.ZipFile(io.BytesIO(raw)) as archive:
                entries = [
                    (n, archive.read(n))
                    for n in archive.namelist()
                    if n.endswith(("/sqlite3.c", "/sqlite3.h", "/sqlite3ext.h"))
                ]
        else:
            with tarfile.open(fileobj=io.BytesIO(raw)) as archive:
                entries = [
                    (m.name, archive.extractfile(m).read())
                    for m in archive.getmembers()
                    if m.isfile()
                ]
        for original, data in entries:
            relative = original.split("/", 1)[1]
            if name.startswith("libX"):
                if relative.startswith("include/") and relative.endswith(".h"):
                    path = "x11/" + relative
                elif relative == "COPYING":
                    path = "x11/notices/" + name + "/COPYING"
                else:
                    continue
            else:
                path = name + "/" + relative
            add(path, data, url + "#sha256=" + expected, original)
    manifest = json.dumps(sorted(records, key=lambda x: x["path"]), indent=2) + "\n"
    (output / "ui-source-manifest.json").write_text(manifest)
    with (output / "ui-sources.tar.gz").open("wb") as f:
        with gzip.GzipFile(filename="", mode="wb", fileobj=f, mtime=0) as gz:
            with tarfile.open(
                mode="w", fileobj=gz, format=tarfile.PAX_FORMAT
            ) as archive:
                for path, data in sorted(files.items()):
                    entry = tarfile.TarInfo(path)
                    entry.size = len(data)
                    entry.mode = 0o644
                    archive.addfile(entry, io.BytesIO(data))
    lock = dict(
        schema=1,
        archive="ui-sources.tar.gz",
        sha256=hashlib.sha256((output / "ui-sources.tar.gz").read_bytes()).hexdigest(),
        manifest="ui-source-manifest.json",
        manifestSha256=hashlib.sha256(manifest.encode()).hexdigest(),
        imkit=dict(
            version="4.1.0",
            revision=IMKIT,
            url="https://github.com/AokiMotohide/imgui-modern-kit",
            coreSources=CORE,
        ),
        imgui=dict(
            version=19297,
            revision=IMGUI,
            url="https://github.com/ocornut/imgui",
            branch="docking",
        ),
        glfw=dict(version="3.5.1", revision="d9d6f0f1f967807ffade6598ea9a631ebaf37a56"),
        acquiredArchives=ARCHIVES,
        selection="ImKit core include closure, 15 core units and Inter assets; no editor/show/Gallery units. ImGui core/matching selected backends. GLFW/curl full source archives; SQLite amalgamation; X11 headers/notices only.",
        modifications="Source bytes unchanged; selected regular archive members normalized to mode 0644. Original upstream CMake retained as provenance; core-only integration adapter is separate.",
        trust="Exact Git object or HTTPS archive hash; independent signature authenticity not established",
    )
    (output / "ui-lock.json").write_text(json.dumps(lock, indent=2) + "\n")
    print(
        json.dumps(
            dict(
                files=len(files),
                archiveSha256=lock["sha256"],
                manifestSha256=lock["manifestSha256"],
            )
        )
    )


if __name__ == "__main__":
    main()
