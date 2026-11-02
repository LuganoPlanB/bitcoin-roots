#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.
"""Bounded, offline paired cache experiment. See cache-bench.md."""

import argparse
import configparser
import datetime
import difflib
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import random
import re
import signal
import statistics
import subprocess
import sys
import tarfile
import time

CORE = "9be056a8a72b624dae9623b2f7bded92c2a21c91"
# CORE remains the historical public constant. Current preparation selects an
# explicit supported profile rather than treating any newer release as v31.
PROFILES = {
    "v31.1": {"core": CORE, "version": (31, 1, 0, 0), "config": "build-issue56/test/config.ini"},
    "v32.1": {"core": "f0de87f040621468b956f01f526a43a010310501", "version": (32, 1, 0, 0), "config": "build/test/config.ini"},
}
# Hash only the two instrumented function bodies, allowing unrelated source
# changes while refusing an unreviewed backend/API at either timing boundary.
BODY_HASHES = {
    "v31.1": {
        "core": ("e31e4c0602ceb149fa5a5a8fcbe5e4b67b260a0cce87b1fb6b5b1560c2fc3d49", "95834a6b7f8eeb78133b909bce1c8991ea9d8be2a0bbf1eca184c3e611e01a42"),
        "roots": ("8e1385985e596b534634525bb75c4feb7d2fde32867065a540ab8d692b33a1b4", "95834a6b7f8eeb78133b909bce1c8991ea9d8be2a0bbf1eca184c3e611e01a42"),
    },
    "v32.1": {
        "core": ("6328f296ce4146bed41b8f0cc4d7098bf3f3bd08769bfb7ec9969f339e43e467", "f39cf7b18b262ed1f2b5eaba30794318ae6506b4ab1fc6cd057b33a1faddaa34"),
        "roots": ("b461e3f0ff9ba2ede1457f9e3857c7912ad943452c5eb0f9feb2ad024fbb1743", "f39cf7b18b262ed1f2b5eaba30794318ae6506b4ab1fc6cd057b33a1faddaa34"),
    },
}
HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
BUILD_FLAGS = ["-DENABLE_IPC=OFF", "-DCMAKE_BUILD_TYPE=RelWithDebInfo", "-DENABLE_WALLET=OFF",
               "-DBUILD_TESTS=OFF", "-DBUILD_GUI=OFF", "-DBUILD_BENCH=OFF"]
INSTRUMENTED = ("src/net_processing.cpp", "src/blockencodings.cpp", "src/issue56_bench.h")
HARNESS_FILES = ("cache_bench.py", "cache_bench_metrics.h", "cache_bench_workload.py")
STOP_GRACE_SECONDS = 15
VARIANTS = {
    "core-default": ("core", []),
    "roots-default": ("roots", []),
    "roots-count100": ("roots", ["-blockreconstructionextratxn=100"]),
    "roots-disabled": ("roots", ["-blockreconstructionextratxn=0"]),
    "roots-count1": ("roots", ["-blockreconstructionextratxn=1"]),
    "roots-bytes10k": ("roots", ["-blockreconstructionextratxnsize=0.01"]),
    "roots-count8-bytes1k": ("roots", ["-blockreconstructionextratxn=8", "-blockreconstructionextratxnsize=0.001"]),
    # A7 only: same cache/settings, same marker fixture, one policy changed.
    "roots-cat21-allowed": ("roots", ["-rejectparasites=0"]),
}
CASES = [(kind, 1, False) for kind in ("accepted", "rejected", "replaced", "orphan", "mixed", "churn", "adversarial")]
CASES += [("rejected", overlap, reverse) for overlap in (0, .5, 1) for reverse in (False, True)
          if (overlap, reverse) != (1, False)]


def case_name(kind, overlap, reverse):
    return f"{kind}-overlap{overlap:g}-{'reverse' if reverse else 'forward'}"


def utc():
    return datetime.datetime.now(datetime.timezone.utc).isoformat()


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def write_json(path, value):
    path = Path(path)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(json.dumps(value, indent=2, sort_keys=True, allow_nan=False) + "\n", encoding="utf8")
    temporary.replace(path)


def output(command, cwd=REPO):
    return subprocess.check_output(command, cwd=cwd, text=True).strip()


def version(text):
    values = []
    for field in ("MAJOR", "MINOR", "BUILD", "RC"):
        matches = re.findall(r"^set\(CLIENT_VERSION_" + field + r" ([0-9]+)\)$", text, re.MULTILINE)
        if len(matches) != 1:
            raise ValueError("Unsupported source version metadata")
        values.append(int(matches[0]))
    return tuple(values)


def select_profile(text, requested=None):
    current = version(text)
    matches = [name for name, profile in PROFILES.items() if profile["version"] == current]
    if len(matches) != 1 or (requested is not None and requested not in matches):
        raise ValueError("Unsupported source/profile version; only exact v31.1 and v32.1 are supported")
    return matches[0]


def harness_hashes():
    return {name: digest(HERE / name) for name in HARNESS_FILES}


def check_environment():
    if platform.system() != "Linux" or not Path("/proc/self/status").is_file():
        raise ValueError("The experiment requires Linux /proc metrics")
    if os.environ.get("LD_PRELOAD"):
        raise ValueError("Unset LD_PRELOAD for the declared default allocator")


def checked_sources(reference, requested=None):
    head = output(["git", "rev-parse", "--verify", "--end-of-options", reference + "^{commit}"])
    profile = select_profile(output(["git", "show", head + ":CMakeLists.txt"]), requested)
    core = PROFILES[profile]["core"]
    if output(["git", "rev-parse", profile + "^{commit}"]) != core:
        raise ValueError("Exact Core tag identity mismatch")
    subprocess.run(["git", "merge-base", "--is-ancestor", core, head], cwd=REPO, check=True)
    return profile, core, head


def stop_group(process):
    """The group was created by this invocation; reap its leader and children."""
    try:
        os.killpg(process.pid, signal.SIGTERM)
    except ProcessLookupError:
        pass
    try:
        process.wait(timeout=STOP_GRACE_SECONDS)
    except subprocess.TimeoutExpired:
        pass
    # A leader may settle while a child ignores TERM. Always finish the group.
    try:
        os.killpg(process.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    process.wait()


def execute(command, *, cwd, log, timeout, env=None):
    """Retain failures/timeouts; never parse an exit-zero as metric validity."""
    if timeout <= 0:
        raise ValueError("Timeout must be positive")
    started = utc()
    with Path(log).open("wb") as stream:
        process = subprocess.Popen(command, cwd=cwd, env=env, stdout=stream, stderr=subprocess.STDOUT,
                                   start_new_session=True)
        timed_out = False
        try:
            code = process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            timed_out = True
            stop_group(process)
            code = process.returncode
        except BaseException:
            stop_group(process)
            raise
    return {"command": command, "cwd": str(cwd), "exit": code, "timed_out": timed_out,
            "started_at": started, "completed_at": utc(), "log": str(log), "log_sha256": digest(log)}


def instrument(source, destination, profile=None, kind=None):
    """Fail on ambiguous anchors; do not edit production working trees."""
    source, destination = Path(source).resolve(), Path(destination)
    if source == REPO.resolve() or (source / ".git").exists():
        raise ValueError("Instrument only disposable Git exports, never working trees")
    changes, pending, bodies = [], {}, []
    anchors = {
        "src/net_processing.cpp": (
            "void PeerManagerImpl::AddToCompactExtraTransactions(const CTransactionRef& tx)\n{\n",
            "    issue56::Insertion issue56_timer;\n"),
        "src/blockencodings.cpp": (
            "ReadStatus PartiallyDownloadedBlock::InitData(const CBlockHeaderAndShortTxIDs& cmpctblock, const std::vector<std::pair<Wtxid, CTransactionRef>>& extra_txn)\n{\n",
            "    issue56::Reconstruction issue56_timer{extra_txn, cmpctblock.header.GetHash().ToString()};\n"),
    }
    for name, (anchor, inserted) in anchors.items():
        target = source / name
        before = target.read_text(encoding="utf8")
        includes = [line for line in ['#include <logging.h>\n', '#include <util/log.h>\n'] if before.count(line) == 1]
        if before.count(anchor) != 1 or not includes:
            raise ValueError(f"Instrumentation anchor mismatch: {name}")
        body = before[before.index(anchor):before.index("\n}\n", before.index(anchor)) + 3]
        bodies.append(hashlib.sha256(body.encode()).hexdigest())
        after = before.replace(anchor, anchor + inserted).replace(
            includes[0], includes[0] + '#include <issue56_bench.h>\n', 1)
        if name == "src/net_processing.cpp":
            end = after.index("\n}\n", after.index(anchor))
            after = after[:end] + "\n    issue56_timer.stored = true;" + after[end:]
        if name == "src/blockencodings.cpp":
            success = '    LogDebug(BCLog::CMPCTBLOCK, "Initialized PartiallyDownloadedBlock'
            if after.count(success) != 1:
                raise ValueError("Reconstruction completion anchor mismatch")
            after = after.replace(success, '    issue56_timer.extra_hits = extra_count;\n'
                                  '    issue56_timer.available = mempool_count;\n' + success)
        pending[target] = after
        changes.extend(difflib.unified_diff(before.splitlines(True), after.splitlines(True),
                                           fromfile="a/" + name, tofile="b/" + name))
    selected = select_profile((source / "CMakeLists.txt").read_text(), profile)
    supported = BODY_HASHES[selected]
    if not any(tuple(bodies) == expected for name, expected in supported.items() if kind is None or name == kind):
        raise ValueError("Unsupported instrumentation function body/profile")
    # Validate both translation units before changing either one.
    for target, after in pending.items():
        target.write_text(after, encoding="utf8")
    header = (HERE / "cache_bench_metrics.h").read_text(encoding="utf8")
    (source / "src/issue56_bench.h").write_text(header, encoding="utf8")
    changes.extend(difflib.unified_diff([], header.splitlines(True),
                                       fromfile="/dev/null", tofile="b/src/issue56_bench.h"))
    destination.write_text("".join(changes), encoding="utf8")


def check_export(source, archive, instrumented=None):
    """Compare an existing export directly with its retained Git archive."""
    source = Path(source)
    expected = set()
    with tarfile.open(archive) as bundle:
        for member in bundle.getmembers():
            if not (member.isfile() or member.issym()):
                continue
            expected.add(member.name)
            path = source / member.name
            if instrumented and member.name in instrumented:
                if digest(path) != instrumented[member.name]:
                    raise ValueError("Instrumented export changed: " + member.name)
            elif member.issym():
                if not path.is_symlink() or os.readlink(path) != member.linkname:
                    raise ValueError("Export symlink changed: " + member.name)
            elif not path.is_file() or path.is_symlink() or path.read_bytes() != bundle.extractfile(member).read():
                raise ValueError("Export source changed: " + member.name)
    if instrumented:
        expected.add("src/issue56_bench.h")
        if digest(source / "src/issue56_bench.h") != instrumented["src/issue56_bench.h"]:
            raise ValueError("Instrumentation header changed")
    actual = {str(path.relative_to(source)) for path in source.rglob('*') if path.is_file() or path.is_symlink()}
    if actual != expected:
        raise ValueError("Unexpected or missing export files")


def build_controls(cache):
    values = dict(re.findall(r"^([^/#:\n][^:\n]*):[^=\n]+=([^\n]*)$", Path(cache).read_text(), re.MULTILINE))
    for flag in BUILD_FLAGS:
        name, expected = flag.removeprefix("-D").split("=", 1)
        if values.get(name) != expected:
            raise ValueError("Configured build profile mismatch: " + name)
    return {name: values.get(name, "") for name in ["CMAKE_CXX_COMPILER", "CMAKE_CXX_COMPILER_LAUNCHER",
        "CMAKE_CXX_FLAGS", "CMAKE_CXX_FLAGS_RELWITHDEBINFO", "CMAKE_BUILD_TYPE"]}


def prepare(args):
    check_environment()
    work = args.work.resolve()
    profile, core, head = checked_sources(getattr(args, "roots_ref", "HEAD"), getattr(args, "profile", None))
    previous = None
    if args.resume:
        previous = json.loads((work / "builds.json").read_text())
        if previous["roots_commit"] != head or previous["core_commit"] != core or previous.get("profile", "v31.1") != profile:
            raise ValueError("Cannot resume another source revision")
    else:
        work.mkdir(parents=True, exist_ok=False, mode=0o700)
    provenance = {"created_at": utc(), "repository": str(REPO), "profile": profile, "roots_commit": head, "core_commit": core,
                  "host": platform.platform(), "python": sys.version, "compiler": output(["c++", "--version"]),
                  "allocator": "system default; no LD_PRELOAD", "instrumentation": "identical header and scope hooks",
                  "builds": {}, "harness": harness_hashes()}
    if previous:
        provenance["previous_build_receipt"] = str(work / ("builds-before-resume-" + str(time.time_ns()) + ".json"))
        write_json(provenance["previous_build_receipt"], previous)
    for name, commit in (("core", core), ("roots", head)):
        source = work / (name + "-source")
        archive = work / (name + ".tar")
        if source.exists():
            if not previous or digest(archive) != previous["builds"][name]["archive_sha256"]:
                raise ValueError("Existing export lacks matching archive provenance")
            # Restore only previously instrumented files. Other sources remain
            # untouched, permitting an incremental recovery from a failed build.
            with tarfile.open(archive) as bundle:
                for relative in ("src/net_processing.cpp", "src/blockencodings.cpp"):
                    (source / relative).write_bytes(bundle.extractfile(relative).read())
            (source / "src/issue56_bench.h").unlink(missing_ok=True)
        else:
            source.mkdir()
            subprocess.run(["git", "archive", "--format=tar", "--output=" + str(archive), commit], cwd=REPO, check=True)
            subprocess.run(["tar", "-xf", str(archive), "-C", str(source)], check=True)
        check_export(source, archive)
        patch = work / (name + "-instrumentation.patch")
        instrument(source, patch, profile, name)
        build = work / (name + "-build")
        commands = [["cmake", "-S", str(source), "-B", str(build), *BUILD_FLAGS],
                    ["cmake", "--build", str(build), "--target", "bitcoind", "-j", "2"]]
        entry = {"commit": commit, "archive_sha256": digest(archive), "source": str(source), "build": str(build),
                 "archive": str(archive), "patch": str(patch), "patch_sha256": digest(patch), "commands": [],
                 "flags": BUILD_FLAGS, "instrumented_source": {path: digest(source / path) for path in INSTRUMENTED}}
        provenance["builds"][name] = entry
        for i, command in enumerate(commands):
            log = work / f"{name}-build-{i}{'-resume-' + str(time.time_ns()) if args.resume else ''}.log"
            record = execute(command, cwd=REPO, log=log, timeout=args.timeout)
            entry["commands"].append(record)
            write_json(work / "builds.json", provenance)
            if record["exit"] != 0 or record["timed_out"]:
                raise RuntimeError(f"Build failed: see {record['log']}")
        entry["binary"] = str(build / "bin/bitcoind")
        entry["binary_sha256"] = digest(entry["binary"])
        entry["cmake_cache_sha256"] = digest(build / "CMakeCache.txt")
        entry["compiler_controls"] = build_controls(build / "CMakeCache.txt")
        write_json(work / "builds.json", provenance)
        print(f"prepared {name} {commit}", flush=True)
    if provenance["builds"]["core"]["compiler_controls"] != provenance["builds"]["roots"]["compiler_controls"]:
        raise ValueError("Controlled compiler/optimization profiles differ")


def parse_metric(line):
    """Strict required schema: absent/duplicate/malformed metrics are failures."""
    if "ISSUE56 " not in line:
        return None
    parts = line.split("ISSUE56 ", 1)[1].split()
    fields = dict(part.split("=", 1) for part in parts)
    required = {"block", "insert_calls", "insert_cpu_ns", "insert_wall_ns", "init_cpu_ns", "init_wall_ns",
                "slots", "slot_bytes", "occupied", "charged_dynamic_bytes", "unique_dynamic_bytes",
                "distinct", "external_refs", "shared_objects", "extra_hits", "available",
                "stored_calls", "stored_cpu_ns", "stored_wall_ns"}
    if set(fields) != required or len(parts) != len(fields) or not re.fullmatch(r"[0-9a-fA-F]{64}", fields["block"]):
        raise ValueError("Incomplete instrumentation metric")
    if any(not re.fullmatch(r"[0-9]+", value) for key, value in fields.items() if key != "block"):
        raise ValueError("Malformed unsigned metric")
    converted = {k: v if k == "block" else int(v) for k, v in fields.items()}
    if any(value > 2**64 - 1 for key, value in converted.items() if key != "block"):
        raise ValueError("Metric exceeds uint64")
    if (converted["occupied"] > converted["slots"] or converted["distinct"] > converted["occupied"]
            or converted["shared_objects"] > converted["distinct"]
            or converted["unique_dynamic_bytes"] > converted["charged_dynamic_bytes"]
            or converted["stored_calls"] > converted["insert_calls"]
            or converted["stored_cpu_ns"] > converted["insert_cpu_ns"]
            or converted["stored_wall_ns"] > converted["insert_wall_ns"]
            or converted["extra_hits"] > converted["available"]):
        raise ValueError("Inconsistent instrumentation metric")
    return converted


def validate_builds(provenance):
    profile, core, roots = checked_sources(provenance["roots_commit"], provenance.get("profile", "v31.1"))
    if core != provenance["core_commit"] or set(provenance["builds"]) != {"core", "roots"}:
        raise ValueError("Both exact controlled builds are required")
    if any(provenance["harness"].get(name) != value for name, value in harness_hashes().items()):
        raise ValueError("Harness or instrumentation changed; prepare both binaries again")
    for kind, entry in provenance["builds"].items():
        if entry["commit"] != (core if kind == "core" else roots) or entry.get("flags") != BUILD_FLAGS:
            raise ValueError("Build source/profile mismatch")
        if len(entry["commands"]) != 2 or any(record["exit"] != 0 or record["timed_out"] for record in entry["commands"]):
            raise ValueError("Incomplete controlled build")
        if set(entry["instrumented_source"]) != set(INSTRUMENTED):
            raise ValueError("Incomplete instrumentation source provenance")
        for path, expected in [(entry["binary"], entry["binary_sha256"]), (entry["patch"], entry["patch_sha256"]),
                               (entry["archive"], entry["archive_sha256"]),
                               (Path(entry["build"]) / "CMakeCache.txt", entry["cmake_cache_sha256"])]:
            if digest(path) != expected:
                raise ValueError("Prepared build input changed: " + str(path))
        check_export(entry["source"], entry["archive"], entry["instrumented_source"])
        if build_controls(Path(entry["build"]) / "CMakeCache.txt") != entry["compiler_controls"]:
            raise ValueError("Compiler controls changed")
    if provenance["builds"]["core"]["compiler_controls"] != provenance["builds"]["roots"]["compiler_controls"]:
        raise ValueError("Controlled compiler/optimization profiles differ")
    return profile


def framework_inputs(configfile, profile):
    configfile = Path(configfile).resolve()
    config = configparser.ConfigParser(interpolation=None)
    with configfile.open() as stream:
        config.read_file(stream)
    source = Path(config["environment"]["SRCDIR"]).resolve()
    select_profile((source / "CMakeLists.txt").read_text(), profile)
    files = sorted((source / "test/functional/test_framework").glob("*.py"))
    if not files or not (source / "test/functional/test_framework/test_framework.py").is_file():
        raise ValueError("Matching functional framework is required")
    return source, {str(path): digest(path) for path in [configfile, *files]}


def run(args):
    check_environment()
    work = args.work.resolve()
    provenance = json.loads((work / "builds.json").read_text())
    profile = validate_builds(provenance)
    configfile = getattr(args, "configfile", None) or REPO / PROFILES[profile]["config"]
    framework, framework_hashes = framework_inputs(configfile, profile)
    variants = args.variants.split(",") if args.variants else list(VARIANTS)
    if not variants or len(set(variants)) != len(variants) or any(v not in VARIANTS for v in variants):
        raise ValueError("Unknown, empty or duplicate variant")
    if not args.smoke and (args.repetitions < 5 or args.warmup < 1):
        raise ValueError("Measurements require warmup and at least five paired repetitions")
    if not 2 <= args.count <= 1000:
        raise ValueError("Bounded count range: 2..1000")
    if args.warmup < 0 or args.repetitions < 1 or args.timeout <= 0 or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.-]*", args.name):
        raise ValueError("Invalid bounded experiment options/name")
    results = work / args.name
    results.mkdir(exist_ok=False)
    design = {"declared_at": utc(), "profile": profile, "smoke": args.smoke, "count": args.count, "seed": args.seed,
              "warmup": 0 if args.smoke else args.warmup, "repetitions": 1 if args.smoke else args.repetitions,
              "variants": {v: VARIANTS[v] for v in variants}, "duration": "fixed counts, no timed-load cutoff",
              "timeout_seconds_per_variant": args.timeout, "builds_sha256": digest(work / "builds.json"),
              "cases": [case_name(*case) for case in CASES],
              "harness": harness_hashes(), "framework": str(framework), "framework_inputs": framework_hashes,
              "configfile": str(Path(configfile).resolve()), "version_checks": []}
    write_json(results / "design.json", design)  # Persist before any warmup or measured sample.
    # Version/help exits occur before node initialization; private regtest data
    # and null configuration still make their isolation explicit.
    version_home = results / "version-home"
    version_home.mkdir(mode=0o700)
    for kind, entry in provenance["builds"].items():
        record = execute([entry["binary"], "-regtest", "-datadir=" + str(version_home), "-conf=/dev/null", "-nosettings", "--version"],
                         cwd=REPO, log=results / (kind + "-version.log"), timeout=args.timeout)
        design["version_checks"].append(record)
        write_json(results / "design.json", design)
        major, minor, _, _ = PROFILES[profile]["version"]
        banner = Path(record["log"]).read_text().splitlines()
        if record["exit"] != 0 or record["timed_out"] or not banner or not re.search(rf"\bv{major}\.{minor}(?:\.0)?(?:[-+\s]|$)", banner[0]):
            raise ValueError("Prepared binary version/profile mismatch; diagnostics retained")
    all_records = []
    failed = False
    for repetition in range(-design["warmup"], design["repetitions"]):
        order = variants.copy()
        random.Random(args.seed + repetition).shuffle(order)
        for variant in order:
            name = f"{'warmup' if repetition < 0 else 'sample'}-{abs(repetition):02d}-{variant}"
            kind, flags = VARIANTS[variant]
            entry = provenance["builds"][kind]
            sample = results / name
            env = dict(os.environ, BITCOIND=entry["binary"], ROOTS_BENCH_FLAGS=json.dumps(flags),
                       ROOTS_BENCH_KIND=kind, ROOTS_BENCH_COUNT=str(args.count),
                       ROOTS_BENCH_RESULT=str(results / (name + ".json")),
                       ROOTS_BENCH_FRAMEWORK=str(framework), ROOTS_BENCH_PROFILE=profile, PYTHONDONTWRITEBYTECODE="1")
            command = [sys.executable, str(HERE / "cache_bench_workload.py"),
                       "--configfile=" + str(Path(configfile).resolve()),
                       "--tmpdir=" + str(sample), "--nocleanup", "--randomseed=" + str(args.seed),
                       "--portseed=" + str(args.seed % 5000), "--v1transport"]
            record = execute(command, cwd=REPO, log=results / (name + ".log"), timeout=args.timeout, env=env)
            record.update(variant=variant, repetition=repetition, warmup=repetition < 0,
                          binary_sha256=entry["binary_sha256"], result=env["ROOTS_BENCH_RESULT"])
            all_records.append(record)
            write_json(results / "runs.json", all_records)
            if record["exit"] != 0 or record["timed_out"]:
                failed = True
            print(f"{name}: exit={record['exit']}", flush=True)
    summary = summarize(results)
    if failed or summary["fixture_mismatches"] or summary["failures"]:
        raise RuntimeError("Failed samples retained; experiment incomplete")
    validate_builds(provenance)
    if framework_inputs(configfile, profile)[1] != framework_hashes:
        raise ValueError("Functional framework changed during experiment")


def summarize(results):
    records = json.loads((results / "runs.json").read_text())
    grouped = {}
    failures = []
    pairing = {}
    design = json.loads((results / "design.json").read_text()) if (results / "design.json").exists() else None
    seen = set()
    for record in records:
        identity = (record["repetition"], record["variant"])
        if identity in seen:
            failures.append({"variant": record["variant"], "repetition": record["repetition"], "reason": "Duplicate run"})
            continue
        seen.add(identity)
        if (type(record["repetition"]) is not int or type(record.get("warmup", False)) is not bool
                or record.get("warmup", False) != (record["repetition"] < 0)
                or (design and (record["variant"] not in design["variants"]
                    or not -design["warmup"] <= record["repetition"] < design["repetitions"]))):
            failures.append({"variant": record["variant"], "repetition": record["repetition"], "reason": "Unplanned run/phase"})
            continue
        if record["exit"] != 0 or record.get("timed_out", False) or not Path(record["result"]).is_file():
            failures.append({"variant": record["variant"], "repetition": record["repetition"], "log": record["log"],
                             "reason": "Timed out" if record.get("timed_out", False) else "Failed/missing sample"})
            continue
        try:
            rows = json.loads(Path(record["result"]).read_text())
        except (OSError, ValueError):
            failures.append({"variant": record["variant"], "repetition": record["repetition"], "reason": "Unreadable/partial sample"})
            continue
        if not isinstance(rows, list) or not rows or any(not isinstance(row, dict) or not isinstance(row.get("case"), str)
                or not isinstance(row.get("fixture_sha256"), str) for row in rows):
            failures.append({"variant": record["variant"], "repetition": record["repetition"], "reason": "Malformed sample"})
            continue
        if (len({row["case"] for row in rows}) != len(rows)
                or (design and sorted(row["case"] for row in rows) != sorted(design["cases"]))):
            failures.append({"variant": record["variant"], "repetition": record["repetition"], "reason": "Incomplete/duplicate case rows"})
            continue
        if any(any((isinstance(value, int) and not isinstance(value, bool) and not 0 <= value <= 2**64 - 1)
                   or (isinstance(value, float) and (not math.isfinite(value) or value < 0))
                   for value in row.values()) or (design and (row.get("block_accepted") is not True
                       or not re.fullmatch(r"[0-9a-f]{64}", row["fixture_sha256"]))) for row in rows):
            failures.append({"variant": record["variant"], "repetition": record["repetition"], "reason": "Invalid/unaccepted measurement"})
            continue
        for row in rows:
            identity = (record["repetition"], row["case"])
            pairing.setdefault(str(identity), set()).add(row["fixture_sha256"])
            if record["warmup"]:
                continue
            key = record["variant"] + "/" + row["case"]
            for field, value in row.items():
                if isinstance(value, (int, float)) and not isinstance(value, bool):
                    grouped.setdefault(key, {}).setdefault(field, []).append(value)
    if design:
        for repetition in range(-design["warmup"], design["repetitions"]):
            for variant in design["variants"]:
                if (repetition, variant) not in seen:
                    failures.append({"variant": variant, "repetition": repetition, "reason": "Run not executed"})
    summary = {"created_at": utc(), "failures": failures,
               "fixture_mismatches": [key for key, values in pairing.items() if len(values) != 1],
               "statistics": {}, "interpretation": "Synthetic local data; no default recommendation. See metric limitations."}
    for key, metrics in grouped.items():
        summary["statistics"][key] = {}
        for field, values in metrics.items():
            summary["statistics"][key][field] = {"n": len(values), "median": statistics.median(values),
                                                  "min": min(values), "max": max(values),
                                                  "sample_stdev": statistics.stdev(values) if len(values) > 1 else None,
                                                  "p95": None if len(values) < 20 else sorted(values)[int(.95 * (len(values) - 1))]}
    write_json(results / "summary.json", summary)
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="action", required=True)
    prep = sub.add_parser("prepare", help="export exact Git sources, instrument, build sequentially with -j2")
    prep.add_argument("--work", type=Path, required=True)
    prep.add_argument("--timeout", type=int, default=2400)
    prep.add_argument("--resume", action="store_true", help="recover an owned failed build with matching archive provenance")
    prep.add_argument("--profile", choices=PROFILES, help="exact supported source/base profile (default: infer from roots-ref)")
    prep.add_argument("--roots-ref", default="HEAD", help="committed Roots source, pinned in builds.json")
    launch = sub.add_parser("run", help="predeclare and run paired samples, retaining failures")
    launch.add_argument("--work", type=Path, required=True)
    launch.add_argument("--name", required=True)
    launch.add_argument("--count", type=int, default=128)
    launch.add_argument("--seed", type=int, default=560031)
    launch.add_argument("--warmup", type=int, default=1)
    launch.add_argument("--repetitions", type=int, default=5)
    launch.add_argument("--timeout", type=int, default=900)
    launch.add_argument("--variants")
    launch.add_argument("--smoke", action="store_true")
    launch.add_argument("--configfile", type=Path, help="existing matching-profile functional test config; defaults preserve v31 build-issue56 or v32 build")
    report = sub.add_parser("summarize")
    report.add_argument("results", type=Path)
    args = parser.parse_args()
    if args.action == "prepare":
        prepare(args)
    elif args.action == "run":
        run(args)
    else:
        summarize(args.results)


if __name__ == "__main__":
    main()
