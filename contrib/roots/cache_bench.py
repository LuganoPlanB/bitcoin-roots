#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.
"""Bounded, offline paired cache experiment. See cache-bench.md."""

import argparse
import datetime
import difflib
import hashlib
import json
import os
from pathlib import Path
import platform
import random
import statistics
import subprocess
import sys
import tarfile
import time

CORE = "9be056a8a72b624dae9623b2f7bded92c2a21c91"
HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
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
    Path(path).write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf8")


def output(command, cwd=REPO):
    return subprocess.check_output(command, cwd=cwd, text=True).strip()


def execute(command, *, cwd, log, timeout, env=None):
    """Retain failures/timeouts; never parse an exit-zero as metric validity."""
    started = utc()
    with Path(log).open("wb") as stream:
        process = subprocess.Popen(command, cwd=cwd, env=env, stdout=stream, stderr=subprocess.STDOUT,
                                   start_new_session=True)
        timed_out = False
        try:
            code = process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            import signal
            timed_out = True
            os.killpg(process.pid, signal.SIGTERM)
            try:
                process.wait(timeout=15)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait()
            code = process.returncode
    return {"command": command, "cwd": str(cwd), "exit": code, "timed_out": timed_out,
            "started_at": started, "completed_at": utc(), "log": str(log), "log_sha256": digest(log)}


def instrument(source, destination):
    """Fail on ambiguous anchors; do not edit production working trees."""
    changes = []
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
        if before.count(anchor) != 1 or '#include <logging.h>\n' not in before:
            raise ValueError(f"Instrumentation anchor mismatch: {name}")
        after = before.replace(anchor, anchor + inserted).replace(
            '#include <logging.h>\n', '#include <logging.h>\n#include <issue56_bench.h>\n', 1)
        if name == "src/net_processing.cpp":
            end = after.index("\n}\n", after.index(anchor))
            after = after[:end] + "\n    issue56_timer.stored = true;" + after[end:]
        if name == "src/blockencodings.cpp":
            success = '    LogDebug(BCLog::CMPCTBLOCK, "Initialized PartiallyDownloadedBlock'
            if after.count(success) != 1:
                raise ValueError("Reconstruction completion anchor mismatch")
            after = after.replace(success, '    issue56_timer.extra_hits = extra_count;\n'
                                  '    issue56_timer.available = mempool_count;\n' + success)
        target.write_text(after, encoding="utf8")
        changes.extend(difflib.unified_diff(before.splitlines(True), after.splitlines(True),
                                           fromfile="a/" + name, tofile="b/" + name))
    header = (HERE / "cache_bench_metrics.h").read_text(encoding="utf8")
    (source / "src/issue56_bench.h").write_text(header, encoding="utf8")
    changes.extend(difflib.unified_diff([], header.splitlines(True),
                                       fromfile="/dev/null", tofile="b/src/issue56_bench.h"))
    destination.write_text("".join(changes), encoding="utf8")


def prepare(args):
    work = args.work.resolve()
    head = output(["git", "rev-parse", "HEAD"])
    previous = None
    if args.resume:
        previous = json.loads((work / "builds.json").read_text())
        if previous["roots_commit"] != head or previous["core_commit"] != CORE:
            raise ValueError("Cannot resume another source revision")
    else:
        work.mkdir(parents=True, exist_ok=False)
    provenance = {"created_at": utc(), "repository": str(REPO), "roots_commit": head, "core_commit": CORE,
                  "host": platform.platform(), "python": sys.version, "compiler": output(["c++", "--version"]),
                  "allocator": "system default; no LD_PRELOAD", "instrumentation": "identical header and scope hooks",
                  "builds": {}, "harness": {p.name: digest(p) for p in HERE.glob("cache_bench*.*")}}
    if previous:
        provenance["previous_build_receipt"] = str(work / ("builds-before-resume-" + str(time.time_ns()) + ".json"))
        write_json(provenance["previous_build_receipt"], previous)
    if os.environ.get("LD_PRELOAD"):
        raise ValueError("Unset LD_PRELOAD for the declared default allocator")
    for name, commit in (("core", CORE), ("roots", head)):
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
        else:
            source.mkdir()
            subprocess.run(["git", "archive", "--format=tar", "--output=" + str(archive), commit], cwd=REPO, check=True)
            subprocess.run(["tar", "-xf", str(archive), "-C", str(source)], check=True)
        patch = work / (name + "-instrumentation.patch")
        instrument(source, patch)
        build = work / (name + "-build")
        flags = ["-DENABLE_IPC=OFF", "-DCMAKE_BUILD_TYPE=RelWithDebInfo", "-DENABLE_WALLET=OFF",
                 "-DBUILD_TESTS=OFF", "-DBUILD_GUI=OFF", "-DBUILD_BENCH=OFF"]
        commands = [["cmake", "-S", str(source), "-B", str(build), *flags],
                    ["cmake", "--build", str(build), "--target", "bitcoind", "-j", "2"]]
        entry = {"commit": commit, "archive_sha256": digest(archive), "source": str(source), "build": str(build),
                 "patch": str(patch), "patch_sha256": digest(patch), "commands": []}
        provenance["builds"][name] = entry
        for i, command in enumerate(commands):
            log = work / f"{name}-build-{i}{'-resume-' + str(time.time_ns()) if args.resume else ''}.log"
            record = execute(command, cwd=REPO, log=log, timeout=args.timeout)
            entry["commands"].append(record)
            write_json(work / "builds.json", provenance)
            if record["exit"] != 0:
                raise RuntimeError(f"Build failed: see {record['log']}")
        entry["binary"] = str(build / "bin/bitcoind")
        entry["binary_sha256"] = digest(entry["binary"])
        entry["cmake_cache_sha256"] = digest(build / "CMakeCache.txt")
        write_json(work / "builds.json", provenance)
        print(f"prepared {name} {commit}", flush=True)


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
    if set(fields) != required or len(parts) != len(fields) or len(fields["block"]) != 64:
        raise ValueError("Incomplete instrumentation metric")
    converted = {k: v if k == "block" else int(v) for k, v in fields.items()}
    if any(value < 0 for key, value in converted.items() if key != "block"):
        raise ValueError("Negative metric")
    if (converted["occupied"] > converted["slots"] or converted["distinct"] > converted["occupied"]
            or converted["shared_objects"] > converted["distinct"]
            or converted["unique_dynamic_bytes"] > converted["charged_dynamic_bytes"]
            or converted["stored_calls"] > converted["insert_calls"]):
        raise ValueError("Inconsistent instrumentation metric")
    return converted


def run(args):
    work = args.work.resolve()
    provenance = json.loads((work / "builds.json").read_text())
    if provenance["harness"]["cache_bench_metrics.h"] != digest(HERE / "cache_bench_metrics.h"):
        raise ValueError("Instrumentation changed; prepare both binaries again")
    if set(provenance["builds"]) != {"core", "roots"}:
        raise ValueError("Both controlled builds are required")
    for entry in provenance["builds"].values():
        if digest(entry["binary"]) != entry["binary_sha256"]:
            raise ValueError("Binary changed after prepare")
        if digest(entry["patch"]) != entry["patch_sha256"]:
            raise ValueError("Instrumentation patch changed after prepare")
    variants = args.variants.split(",") if args.variants else list(VARIANTS)
    if any(v not in VARIANTS for v in variants):
        raise ValueError("Unknown variant")
    if not args.smoke and (args.repetitions < 5 or args.warmup < 1):
        raise ValueError("Measurements require warmup and at least five paired repetitions")
    if not 2 <= args.count <= 1000:
        raise ValueError("Bounded count range: 2..1000")
    results = work / args.name
    results.mkdir(exist_ok=False)
    design = {"declared_at": utc(), "smoke": args.smoke, "count": args.count, "seed": args.seed,
              "warmup": 0 if args.smoke else args.warmup, "repetitions": 1 if args.smoke else args.repetitions,
              "variants": {v: VARIANTS[v] for v in variants}, "duration": "fixed counts, no timed-load cutoff",
              "timeout_seconds_per_variant": args.timeout, "builds_sha256": digest(work / "builds.json"),
              "cases": [case_name(*case) for case in CASES],
              "harness": {p.name: digest(p) for p in HERE.glob("cache_bench*.*")}}
    write_json(results / "design.json", design)  # Persist before any warmup or measured sample.
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
                       ROOTS_BENCH_RESULT=str(results / (name + ".json")))
            command = [sys.executable, str(HERE / "cache_bench_workload.py"),
                       "--configfile=" + str(REPO / "build-issue56/test/config.ini"),
                       "--tmpdir=" + str(sample), "--nocleanup", "--randomseed=" + str(args.seed),
                       "--portseed=" + str(args.seed % 5000), "--v1transport"]
            record = execute(command, cwd=REPO, log=results / (name + ".log"), timeout=args.timeout, env=env)
            record.update(variant=variant, repetition=repetition, warmup=repetition < 0,
                          binary_sha256=entry["binary_sha256"], result=env["ROOTS_BENCH_RESULT"])
            all_records.append(record)
            write_json(results / "runs.json", all_records)
            if record["exit"] != 0:
                failed = True
            print(f"{name}: exit={record['exit']}", flush=True)
    summary = summarize(results)
    if failed or summary["fixture_mismatches"] or summary["failures"]:
        raise RuntimeError("Failed samples retained; experiment incomplete")


def summarize(results):
    records = json.loads((results / "runs.json").read_text())
    grouped = {}
    failures = []
    pairing = {}
    design = json.loads((results / "design.json").read_text()) if (results / "design.json").exists() else None
    seen = set()
    for record in records:
        seen.add((record["repetition"], record["variant"]))
        if record["exit"] != 0 or not Path(record["result"]).is_file():
            failures.append({"variant": record["variant"], "repetition": record["repetition"], "log": record["log"]})
            continue
        if record["warmup"]:
            continue
        rows = json.loads(Path(record["result"]).read_text())
        if design and sorted(row["case"] for row in rows) != sorted(design["cases"]):
            failures.append({"variant": record["variant"], "repetition": record["repetition"], "reason": "Incomplete/duplicate case rows"})
        for row in rows:
            identity = (record["repetition"], row["case"])
            pairing.setdefault(str(identity), set()).add(row["fixture_sha256"])
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
