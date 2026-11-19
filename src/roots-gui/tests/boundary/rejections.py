# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Synthetic isolated Git scope and link fragments, never application linkage."""

import sys

sys.dont_write_bytecode = True
import argparse
import json
from pathlib import Path
import subprocess
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from support.runtime import evidence_directory, validated_scratch, write_json
from run_boundary import REPO, changed_paths


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--output", type=Path, required=True)
    a = p.parse_args()
    out = Path(
        tempfile.mkdtemp(prefix="boundary-negative-", dir=evidence_directory(a.output))
    )
    checks = []

    def check(ok, id):
        checks.append(dict(id=id, passed=bool(ok)))

    def evaluate(paths, libraries, expected, name):
        fixture = out / (name + ".json")
        write_json(
            fixture,
            dict(kind="synthetic-boundary-input", paths=paths, libraries=libraries),
        )
        argv = [
            sys.executable,
            "-B",
            str(Path(__file__).with_name("run_boundary.py")),
            "--evaluate-fixture",
            str(fixture),
            "--output",
            str(out / name),
        ]
        with (out / (name + ".log")).open("w") as log:
            r = subprocess.run(argv, stdout=log, stderr=subprocess.STDOUT, timeout=10)
        return r.returncode == expected

    with tempfile.TemporaryDirectory(
        prefix="boundary-repo-", dir=validated_scratch()
    ) as folder:
        root = Path(folder)
        subprocess.run(["git", "init", "--quiet", str(root)], check=True)
        allowed = root / "src/roots-gui/fixture.cpp"
        allowed.parent.mkdir(parents=True)
        allowed.write_text("// synthetic scope fixture\n")
        check(
            evaluate(changed_paths(root), ["libroots_gui_ui.a"], 0, "allowed-scope"),
            "actual isolated Git allowed scope accepted",
        )
        forbidden = root / "src/wallet/forbidden.cpp"
        forbidden.parent.mkdir(parents=True)
        forbidden.write_text("// synthetic outside-path fixture; not Core code\n")
        paths = changed_paths(root)
        check(
            "src/wallet/forbidden.cpp" in paths
            and evaluate(paths, ["libroots_gui_ui.a"], 1, "outside-path"),
            "actual isolated Git outside-path rejected with exit1",
        )
        check(
            evaluate(
                [], ["libroots_gui_ui.a", "libroots_gui_platform.a"], 0, "allowed-links"
            ),
            "GUI library fragments accepted",
        )
        for library in [
            "libbitcoin_wallet.a",
            "bitcoin_node",
            "Qt6::Widgets",
            "libbitcoin_ipc.a",
        ]:
            check(
                evaluate(
                    [],
                    ["libroots_gui_ui.a", library],
                    1,
                    "forbidden-link-" + str(len(checks)),
                ),
                "synthetic forbidden link fragment rejected with exit1 " + library,
            )
    # Reuse an existing HEAD object read-only to exercise tracked changes without
    # creating any commit or checking out application source. Only synthetic
    # fixture text is written. Every index/ref operation targets this private repo.
    with tempfile.TemporaryDirectory(
        prefix="boundary-index-", dir=validated_scratch()
    ) as folder:
        root = Path(folder)

        def git(*args):
            return subprocess.check_output(["git", *args], cwd=root).decode().strip()

        subprocess.run(["git", "init", "--quiet", str(root)], check=True)
        objects = Path(
            subprocess.check_output(
                ["git", "rev-parse", "--git-path", "objects"], cwd=REPO, text=True
            ).strip()
        )
        if not objects.is_absolute():
            objects = REPO / objects
        (root / ".git/objects/info/alternates").write_text(
            str(objects.resolve()) + "\n"
        )
        head = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=REPO, text=True
        ).strip()
        git("update-ref", "HEAD", head)

        def reset():
            for p in sorted(root.rglob("*"), reverse=True):
                if ".git" not in p.relative_to(root).parts:
                    if p.is_file():
                        p.unlink()
                    elif p.is_dir():
                        p.rmdir()
            git("read-tree", "--reset", "HEAD")
            paths = subprocess.check_output(["git", "ls-files", "-z"], cwd=root)
            subprocess.run(
                ["git", "update-index", "--skip-worktree", "-z", "--stdin"],
                cwd=root,
                input=paths,
                check=True,
            )

        def write(path):
            p = root / path
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_text("// synthetic boundary fixture\n")
            return p

        def tracked(path):
            git("update-index", "--no-skip-worktree", "--", path)
            return write(path)

        def scope(name, expected, exit=1):
            paths = changed_paths(root)
            check(
                paths == sorted(expected)
                and evaluate(paths, ["libroots_gui_ui.a"], exit, name),
                name,
            )

        reset()
        write("src/wallet/staged.cpp")
        git("add", "--", "src/wallet/staged.cpp")
        scope("staged-outside-addition", ["src/wallet/staged.cpp"])
        reset()
        write("src/roots-gui/staged.cpp")
        git("add", "--", "src/roots-gui/staged.cpp")
        scope("staged-allowed-addition", ["src/roots-gui/staged.cpp"], 0)
        reset()
        tracked("COPYING")
        git("add", "--", "COPYING")
        scope("staged-outside-modification", ["COPYING"])
        reset()
        tracked("COPYING").unlink()
        git("update-index", "--remove", "--", "COPYING")
        scope("staged-outside-deletion", ["COPYING"])
        reset()
        tracked("COPYING")
        (root / "src/roots-gui").mkdir(parents=True)
        git("mv", "--", "COPYING", "src/roots-gui/moved.cpp")
        scope("staged-rename-outside-to-gui", ["COPYING", "src/roots-gui/moved.cpp"])
        reset()
        tracked("CMakeLists.txt")
        (root / "src/wallet").mkdir(parents=True)
        git("mv", "--", "CMakeLists.txt", "src/wallet/moved.cpp")
        scope(
            "staged-rename-gui-hook-to-outside",
            ["CMakeLists.txt", "src/wallet/moved.cpp"],
        )
        reset()
        source = tracked("COPYING")
        destination = write("src/roots-gui/moved.cpp")
        source.rename(destination)
        scope("unstaged-rename-outside-to-gui", ["COPYING", "src/roots-gui/moved.cpp"])
        reset()
        tracked("COPYING")
        git("add", "--", "COPYING")
        (root / "COPYING").write_text("// second synthetic change\n")
        write("src/roots-gui/untracked.cpp")
        outside = write("src/wallet/mixed.cpp")
        git("add", "--", "src/wallet/mixed.cpp")
        outside.unlink()
        scope(
            "mixed-index-worktree-untracked-deduplicated",
            ["COPYING", "src/roots-gui/untracked.cpp", "src/wallet/mixed.cpp"],
        )
    failed = sum(not c["passed"] for c in checks)
    r = dict(
        selected=15,
        executed=len(checks),
        passed=len(checks) - failed,
        failed=failed,
        skipped=0,
        kind="negative contract fixtures; no actual forbidden application linkage or source/index edits; existing commit objects reused without creating commits",
        checks=checks,
    )
    write_json(out / "result.json", r)
    print(json.dumps(r | {"output": str(out)}))
    return int(failed > 0 or len(checks) != 15)


if __name__ == "__main__":
    raise SystemExit(main())
