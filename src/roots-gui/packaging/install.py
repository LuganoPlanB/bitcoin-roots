#!/usr/bin/env python3
# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Install or remove a Bitcoin Roots Linux distribution in a dedicated prefix."""

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil
import stat
import tempfile


MANIFEST = "manifest.json"


def identity(path):
    flags = path.lstat().st_mode
    if not stat.S_ISREG(flags) or stat.S_IMODE(flags) not in (0o644, 0o755):
        raise ValueError(
            f"expected a regular package file with mode 644 or 755: {path}"
        )
    digest = hashlib.sha256()
    with path.open("rb") as contents:
        for block in iter(lambda: contents.read(1024 * 1024), b""):
            digest.update(block)
    return {
        "bytes": path.stat().st_size,
        "mode": stat.S_IMODE(flags),
        "sha256": digest.hexdigest(),
    }


def inventory(folder):
    files = {}
    for path in sorted(folder.rglob("*")):
        name = path.relative_to(folder).as_posix()
        if stat.S_ISDIR(path.lstat().st_mode):
            continue
        if name != MANIFEST:
            files[name] = identity(path)
    return files


def checked_file(folder, name):
    parts = PurePosixPath(name)
    if (
        not parts.parts
        or parts.is_absolute()
        or ".." in parts.parts
        or name != parts.as_posix()
    ):
        raise ValueError(f"invalid package path: {name}")
    for part in parts.parts[:-1]:
        folder = folder / part
        if not stat.S_ISDIR(folder.lstat().st_mode):
            raise ValueError(f"expected a real package directory: {folder}")
    return folder / parts.name


def read_package(folder):
    receipt = folder / MANIFEST
    identity(receipt)
    files = json.loads(receipt.read_text(encoding="utf-8"))
    for name in files:
        checked_file(folder, name)
    if "bin/roots-gui" not in files or files != inventory(folder):
        raise ValueError("distribution files differ from the package manifest")
    return files


def install(folder, prefix, files):
    if prefix.exists() or prefix.is_symlink():
        raise ValueError(f"install requires a new dedicated prefix: {prefix}")
    with tempfile.TemporaryDirectory(
        prefix=".roots-gui-", dir=prefix.parent
    ) as scratch:
        staged = Path(scratch) / "install"
        staged.mkdir(mode=0o755)
        for name in [*files, MANIFEST]:
            destination = staged / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(folder / name, destination)
        read_package(staged)
        staged.rename(prefix)


def uninstall(folder, prefix, files):
    if not stat.S_ISDIR(prefix.lstat().st_mode) or folder == prefix:
        raise ValueError(
            "use an extracted distribution separate from the installed prefix"
        )
    receipt = prefix / MANIFEST
    if identity(receipt) != identity(folder / MANIFEST):
        raise ValueError("installed receipt differs from this distribution")
    installed = []
    directories = set()
    for name, expected in files.items():
        path = checked_file(prefix, name)
        if identity(path) != expected:
            raise ValueError(f"installed file was modified: {path}")
        installed.append(path)
        directories.update(
            parent for parent in path.parents if prefix in parent.parents
        )
    for path in [*installed, receipt]:
        path.unlink()
    for folder in sorted(directories, key=lambda path: (-len(path.parts), path)) + [
        prefix
    ]:
        if not any(folder.iterdir()):
            folder.rmdir()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    for command in ("install", "uninstall"):
        commands.add_parser(command).add_argument("prefix", type=Path)
    commands.add_parser("seal", help="create a manifest for a freshly staged build")
    args = parser.parse_args()
    folder = Path(__file__).resolve().parent
    try:
        if args.command == "seal":
            with (folder / MANIFEST).open("x", encoding="utf-8") as receipt:
                json.dump(inventory(folder), receipt, indent=2, sort_keys=True)
                receipt.write("\n")
            (folder / MANIFEST).chmod(0o644)
            return
        prefix = Path(args.prefix.absolute())
        if prefix.name in ("", ".", "..") or not prefix.parent.is_dir():
            raise ValueError(
                "prefix must name a dedicated directory with an existing parent"
            )
        prefix = prefix.parent.resolve() / prefix.name
        files = read_package(folder)
        if args.command == "install":
            install(folder, prefix, files)
        else:
            uninstall(folder, prefix, files)
        print(f"{args.command}: {prefix}")
    except (OSError, ValueError) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    main()
