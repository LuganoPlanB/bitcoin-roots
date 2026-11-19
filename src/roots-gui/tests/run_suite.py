# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Plan entry points with exact identities, nonempty selection and immutable logs."""

import sys

sys.dont_write_bytecode = True
import argparse
from datetime import datetime, timezone
import json
import os
import signal
import re
from pathlib import Path
import shutil
import subprocess
import tempfile
import xml.etree.ElementTree as ET
from support.runtime import evidence_directory, write_json

RELEASE_WORKSPACE = "roots_gui.visual.release_workspace"
SUITES = {
    "model": (
        "roots-gui.model",
        {
            "roots_gui.models",
            "roots_gui.preferences.models",
            "roots_gui.fixture_contracts",
            "roots_gui.screen_contracts",
            "roots_gui.wallet_import.models",
            "roots_gui.receive.models",
            "roots_gui.settings.models",
            "roots_gui.keystore.models",
            "roots_gui.rescan.models",
            "roots_gui.protocol.documents",
            "roots_gui.protocol.offline_draft",
            *[
                f"roots_gui.{x}"
                for x in [
                    "connection.models",
                    "refresh.models",
                    "history.model",
                    "history.workspace",
                    "history.states",
                    "address.models",
                    "coin.models",
                    "coin.controls",
                    "storage.schema",
                    "storage.protected",
                    "storage.conversion",
                    "storage.recovery",
                ]
            ],
        },
    ),
    "rpc": (
        "rpc",
        {
            "roots_gui.connection.models",
            "roots_gui.refresh.models",
            *[
                f"roots_gui.rpc.{x}"
                for x in [
                    "fake_http",
                    "scratch",
                    "json",
                    "transport",
                    "capabilities",
                    "refresh",
                    "history",
                    "history_bounds",
                    "addresses",
                ]
            ],
        },
    ),
    "ui": (
        "ui",
        {
            "roots_gui.theme.contracts",
            "roots_gui.theme.assets",
            "roots_gui.theme.focus_checker",
            "roots_gui.theme.privacy_checker",
            "roots_gui.theme.shell_checker",
            "roots_gui.theme.status_focus_checker",
            *[
                f"roots_gui.engine.{x}"
                for x in [
                    "preferences",
                    "bootstrap",
                    "appearance",
                    "components",
                    "privacy",
                    "shell",
                    "documents",
                    "commands",
                    "semantics",
                    "server",
                    "history",
                    "addresses",
                    "coins",
                    "coin_controls",
                    "wallet_onboarding",
                ]
            ],
            *[
                f"roots_gui.theme.{x}"
                for x in [
                    "session_recovery",
                    "wallet_locked",
                    "connection_offline_stale",
                    "history",
                    "addresses",
                    "coins",
                    "coin_controls",
                    "wallet_onboarding",
                ]
            ],
            *[
                f"roots_gui.imkit.{x}"
                for x in [
                    "theme",
                    "design_system",
                    "workflow",
                    "charts",
                    "api_compile",
                    "design_api_compile",
                    "charts_api_compile",
                ]
            ],
        },
    ),
    "regtest": (
        "regtest",
        {
            "roots_gui.regtest.owned",
            "roots_gui.rpc.history",
            *[
                f"roots_gui.theme.{x}"
                for x in [
                    "history",
                    "addresses",
                    "coins",
                    "coin_controls",
                    "wallet_onboarding",
                ]
            ],
        },
    ),
    "visual": (
        "visual",
        {
            "roots_gui.visual.native",
            "roots_gui.visual.contracts",
            RELEASE_WORKSPACE,
            "roots_gui.theme.native_gallery",
            "roots_gui.theme.native_appearance",
            "roots_gui.theme.native_components",
            "roots_gui.theme.native_shell",
            "roots_gui.theme.native_shell_appearance",
            "roots_gui.theme.native_documents",
            "roots_gui.theme.native_commands",
            "roots_gui.theme.native_semantics",
        },
    ),
    "boundary": (
        "boundary",
        {
            "roots_gui.boundary.actual",
            "roots_gui.boundary.rejections",
            "roots_gui.build_contract",
            *[
                f"roots_gui.runner.{x}"
                for x in ["failure", "unknown", "count", "undrained", "timeout"]
            ],
        },
    ),
}
ACCESSIBILITY = {
    f"roots_gui.accessibility.{x}"
    for x in ["native", "transport", "input", "orca", "display_allocation"]
}
# The full Linux/explicit-owned-daemon profile is the public aggregate contract.
# Native journeys have separate evidence gates; exclude their repeated matrices
# from the deterministic aggregate rather than converting them into skips.
for native in (
    "wallet_import",
    "receive",
    "settings_policy",
    "keystore",
    "advanced",
    "offline_draft",
):
    for group in ("ui", "regtest"):
        SUITES[group][1].add(f"roots_gui.theme.{native}")
SUITES["ui"][1].add("roots_gui.theme.offline_documents")
SUITES["ui"][1].update(
    {
        "roots_gui.engine.send",
        "roots_gui.engine.transaction_flow",
        "roots_gui.theme.send",
        "roots_gui.theme.funding",
        "roots_gui.theme.funding_live",
        "roots_gui.theme.create_live",
        "roots_gui.theme.fees_live",
        "roots_gui.theme.flow_live",
    }
)
SUITES["model"][1].update({"roots_gui.send.models", "roots_gui.fees.models"})
SUITES["model"][1].add("roots_gui.extensions.models")
SUITES["model"][1].add("roots_gui.custody.models")
SUITES["model"][1].update(
    {"roots_gui.payment_protocols.models", "roots_gui.capabilities.ledger"}
)
SUITES["model"][1].add("roots_gui.batch.models")
SUITES["ui"][1].update({"roots_gui.engine.batch", "roots_gui.theme.batch_live"})
SUITES["ui"][1].add("roots_gui.engine.workbench")
SUITES["ui"][1].update(
    {"roots_gui.engine.signing_review", "roots_gui.theme.review_live"}
)
SUITES["ui"][1].update(
    {"roots_gui.engine.transaction_edit", "roots_gui.theme.edit_live"}
)
SUITES["ui"][1].add("roots_gui.theme.sign_live")
SUITES["rpc"][1].add("roots_gui.rpc.daemon_signer")
DEVICE_SIGNER_RPC = "roots_gui.rpc.device_signers"
DEVICE_SIGNER_UI = "roots_gui.theme.device_signers"
SUITES["rpc"][1].add(DEVICE_SIGNER_RPC)
SUITES["model"][1].add("roots_gui.protocol.signature_exchange")
FINALIZED_TRANSACTION = "roots_gui.theme.finalized_transaction"
BROADCAST_NATIVE = {
    "roots_gui.theme.broadcast_preflight",
    "roots_gui.theme.broadcast_recovery",
    "roots_gui.theme.broadcast_monitor",
}
SUITES["ui"][1].update(
    {"roots_gui.theme.signature_exchange", FINALIZED_TRANSACTION, *BROADCAST_NATIVE}
)
SUITES["regtest"][1].update({FINALIZED_TRANSACTION, *BROADCAST_NATIVE})
SUITES["model"][1].add("roots_gui.storage.broadcast_recovery")
SUITES["model"][1].add("roots_gui.protocol.transaction_edit")
for group in ("rpc", "regtest"):
    SUITES[group][1].add("roots_gui.rpc.funding")
NATIVE_UI = {
    f"roots_gui.theme.{x}"
    for x in [
        "session_recovery",
        "wallet_locked",
        "connection_offline_stale",
        "history",
        "addresses",
        "coins",
        "coin_controls",
        "wallet_onboarding",
        "wallet_import",
        "receive",
        "settings_policy",
        "keystore",
        "advanced",
        "offline_documents",
        "offline_draft",
    ]
}
SUITES["ui"][1].add("roots_gui.theme.extensions_unavailable")
NATIVE_UI.add("roots_gui.theme.extensions_unavailable")
SUITES["ui"][1].add("roots_gui.theme.custody_unavailable")
NATIVE_UI.add("roots_gui.theme.custody_unavailable")
SUITES["ui"][1].add("roots_gui.theme.payment_protocol_unavailable")
NATIVE_UI.add("roots_gui.theme.payment_protocol_unavailable")
SUITES["ui"][1].add("roots_gui.theme.preferences")
NATIVE_UI.add("roots_gui.theme.preferences")
SUITES["ui"][1].update(
    {"roots_gui.engine.wallet_tools", "roots_gui.theme.wallet_tools"}
)
NATIVE_UI.add("roots_gui.theme.wallet_tools")
SUITES["model"][1].add("roots_gui.desktop.models")
SUITES["ui"][1].add("roots_gui.theme.desktop")
NATIVE_UI.add("roots_gui.theme.desktop")
SUITES["model"][1].add("roots_gui.message.models")
SUITES["rpc"][1].add("roots_gui.rpc.message")
SUITES["ui"][1].add("roots_gui.theme.message")
NATIVE_UI.add("roots_gui.theme.message")
SUITES["ui"][1].add("roots_gui.theme.sweep")
NATIVE_UI.add("roots_gui.theme.sweep")
SUITES["model"][1].update({"roots_gui.download.formats", "roots_gui.download.verify"})
SUITES["ui"][1].add("roots_gui.theme.download")
NATIVE_UI.add("roots_gui.theme.download")
SUITES["model"][1].update(
    {"roots_gui.storage.crashes", "roots_gui.storage.concurrency"}
)
SUITES["ui"][1].add("roots_gui.theme.metadata_restore")
NATIVE_UI.add("roots_gui.theme.metadata_restore")
SUITES["ui"][1].add("roots_gui.theme.wallet_metadata")
NATIVE_UI.add("roots_gui.theme.wallet_metadata")
NATIVE_UI.update(
    {
        "roots_gui.theme.send",
        "roots_gui.theme.funding",
        "roots_gui.theme.funding_live",
        "roots_gui.theme.create_live",
        "roots_gui.theme.fees_live",
        "roots_gui.theme.flow_live",
    }
)
NATIVE_UI.add("roots_gui.theme.batch_live")
NATIVE_UI.add("roots_gui.theme.review_live")
NATIVE_UI.add("roots_gui.theme.edit_live")
NATIVE_UI.add("roots_gui.theme.sign_live")
NATIVE_UI.update(
    {"roots_gui.theme.signature_exchange", FINALIZED_TRANSACTION, *BROADCAST_NATIVE}
)
INCREASE_FEE = "roots_gui.theme.increase_fee"
SUITES["ui"][1].add(INCREASE_FEE)
SUITES["regtest"][1].add(INCREASE_FEE)
NATIVE_UI.add(INCREASE_FEE)
ATTEMPT_CANCELLATION = "roots_gui.theme.attempt_cancellation"
SUITES["ui"][1].add(ATTEMPT_CANCELLATION)
SUITES["regtest"][1].add(ATTEMPT_CANCELLATION)
NATIVE_UI.add(ATTEMPT_CANCELLATION)
INCREASE_EFFECTIVE_FEE = "roots_gui.theme.increase_effective_fee"
SUITES["ui"][1].add(INCREASE_EFFECTIVE_FEE)
SUITES["regtest"][1].add(INCREASE_EFFECTIVE_FEE)
NATIVE_UI.add(INCREASE_EFFECTIVE_FEE)
QR_CODEC = "roots_gui.protocol.qr_codec"
QR_EXCHANGE = "roots_gui.theme.qr_exchange"
SUITES["model"][1].add(QR_CODEC)
SUITES["ui"][1].add(QR_EXCHANGE)
NATIVE_UI.add(QR_EXCHANGE)
CAMERA_PROBE = "roots_gui.platform.camera"
CAMERA_UI = "roots_gui.theme.camera"
SUITES["model"][1].add(CAMERA_PROBE)
SUITES["ui"][1].add(CAMERA_UI)
NATIVE_UI.add(CAMERA_UI)
SUITES["ui"][1].add(DEVICE_SIGNER_UI)
NATIVE_UI.add(DEVICE_SIGNER_UI)
INVENTORY = set().union(*(expected for _, expected in SUITES.values()), ACCESSIBILITY)
APPLICABLE = (
    set().union(*(SUITES[x][1] for x in ["model", "rpc", "ui", "regtest", "boundary"]))
    - NATIVE_UI
)
SUITES["applicable"] = ("roots-gui", APPLICABLE)


def configured_suites(build):
    # Optional registrations follow explicit configure inputs, never the observed
    # inventory itself. A node-less build can still run its pure model/RPC suites.
    cache = {}
    for line in (build / "CMakeCache.txt").read_text().splitlines():
        if (
            line
            and not line.startswith(("#", "//"))
            and "=" in line
            and ":" in line.split("=", 1)[0]
        ):
            key, value = line.split("=", 1)
            cache[key.split(":", 1)[0]] = value
    roots = bool(cache.get("ROOTS_GUI_TEST_ROOTS_DAEMON"))
    omitted = (
        set()
        if sys.platform.startswith("linux")
        else {
            "roots_gui.protocol.documents",
            "roots_gui.protocol.offline_draft",
            "roots_gui.theme.offline_documents",
            "roots_gui.theme.offline_draft",
            "roots_gui.theme.send",
            "roots_gui.theme.funding",
            "roots_gui.theme.funding_live",
            "roots_gui.theme.create_live",
            "roots_gui.theme.fees_live",
            "roots_gui.theme.flow_live",
        }
    )
    if not sys.platform.startswith("linux"):
        omitted.update(
            {
                CAMERA_PROBE,
                CAMERA_UI,
                "roots_gui.storage.crashes",
                "roots_gui.storage.concurrency",
                "roots_gui.theme.metadata_restore",
                "roots_gui.theme.wallet_metadata",
                "roots_gui.theme.extensions_unavailable",
                "roots_gui.theme.custody_unavailable",
            }
        )
    if cache.get("ROOTS_GUI_DOWNLOAD_FIXTURES") != "ON":
        omitted.update({"roots_gui.download.verify", "roots_gui.theme.download"})
    if not roots:
        omitted |= {
            "roots_gui.rpc.history",
            "roots_gui.rpc.funding",
            "roots_gui.theme.funding_live",
            "roots_gui.theme.create_live",
            "roots_gui.theme.fees_live",
            "roots_gui.theme.flow_live",
            *BROADCAST_NATIVE,
            *[
                f"roots_gui.theme.{x}"
                for x in [
                    "history",
                    "addresses",
                    "coins",
                    "coin_controls",
                    "wallet_onboarding",
                    "wallet_import",
                    "receive",
                    "settings_policy",
                    "keystore",
                    "advanced",
                    "offline_draft",
                ]
            ],
        }
    if not roots or not sys.platform.startswith("linux"):
        omitted.update(
            {
                INCREASE_FEE,
                ATTEMPT_CANCELLATION,
                INCREASE_EFFECTIVE_FEE,
                "roots_gui.theme.batch_live",
                "roots_gui.theme.review_live",
                "roots_gui.theme.edit_live",
                "roots_gui.theme.sign_live",
            }
        )
    openssl = cache.get("ROOTS_GUI_TEST_OPENSSL", "").upper()
    if (
        not sys.platform.startswith("linux")
        or openssl in {"", "0", "OFF", "NO", "FALSE", "N", "IGNORE", "NOTFOUND"}
        or openssl.endswith("-NOTFOUND")
    ):
        omitted.update(
            {
                RELEASE_WORKSPACE,
                QR_CODEC,
                QR_EXCHANGE,
                "roots_gui.protocol.signature_exchange",
                "roots_gui.theme.signature_exchange",
                FINALIZED_TRANSACTION,
                *BROADCAST_NATIVE,
            }
        )
        omitted.update({DEVICE_SIGNER_RPC, DEVICE_SIGNER_UI})
    if not roots and not cache.get("ROOTS_GUI_TEST_CORE_DAEMON"):
        omitted.add("roots_gui.regtest.owned")
    suites = {
        name: (label, expected - omitted) for name, (label, expected) in SUITES.items()
    }
    if not roots:
        suites["regtest"][1].discard(FINALIZED_TRANSACTION)
    return suites, INVENTORY - omitted, roots


def selection_arguments(suite):
    label, expected = SUITES[suite]
    return (
        ["-R", "^(" + "|".join(re.escape(x) for x in sorted(expected)) + ")$"]
        if suite == "applicable"
        else ["-L", "^" + label + "$"]
    )


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--suite", choices=SUITES, required=True)
    p.add_argument("--build", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    a = p.parse_args()
    parent = evidence_directory(a.output)
    out = Path(tempfile.mkdtemp(prefix=a.suite + "-", dir=parent))
    build = a.build.resolve()
    suites, inventory_expected, roots = configured_suites(build)
    label, expected = suites[a.suite]
    inventory_argv = ["ctest", "--test-dir", str(build), "--show-only=json-v1"]
    inventory_run = subprocess.run(
        inventory_argv, capture_output=True, text=True, timeout=30
    )
    (out / "inventory.json").write_text(inventory_run.stdout)
    (out / "inventory.stderr").write_text(inventory_run.stderr)
    inventory_tests = (
        json.loads(inventory_run.stdout).get("tests", [])
        if inventory_run.returncode == 0
        else []
    )
    inventory = {
        x["name"]
        for x in inventory_tests
        if x["name"].startswith("roots_gui.")
        or "roots-gui"
        in next(
            (p["value"] for p in x.get("properties", []) if p["name"] == "LABELS"), []
        )
    }
    label_mismatches = {}
    for name, (suite_label, suite_expected) in suites.items():
        if name == "applicable":
            continue
        actual = {
            x["name"]
            for x in inventory_tests
            if suite_label
            in next(
                (p["value"] for p in x.get("properties", []) if p["name"] == "LABELS"),
                [],
            )
        }
        if actual != suite_expected:
            label_mismatches[name] = dict(
                missing=sorted(suite_expected - actual),
                unexpected=sorted(actual - suite_expected),
            )
    argv = [
        "ctest",
        "--test-dir",
        str(build),
        *selection_arguments(a.suite),
        "--show-only=json-v1",
    ]
    result = subprocess.run(argv, capture_output=True, text=True, timeout=30)
    (out / "selection.json").write_text(result.stdout)
    selected = (
        {x["name"] for x in json.loads(result.stdout).get("tests", [])}
        if result.returncode == 0
        else set()
    )
    record = dict(
        suite=a.suite,
        label=label,
        selected=sorted(selected),
        selectedCount=len(selected),
        expected=sorted(expected),
        selectionCommand=argv,
        inventoryCommand=inventory_argv,
        inventoryCount=len(inventory),
        inventoryMissing=sorted(inventory_expected - inventory),
        inventoryUnexpected=sorted(inventory - inventory_expected),
        labelMismatches=label_mismatches,
        explicitRootsDaemonConfigured=roots,
        excluded=sorted(inventory - expected),
        startUTC=datetime.now(timezone.utc).isoformat(),
        executed=0,
        passed=0,
        failed=0,
        skipped=0,
    )
    if (
        not expected
        or selected != expected
        or inventory != inventory_expected
        or label_mismatches
        or (a.suite == "applicable" and not roots)
    ):
        record.update(
            runnerErrors=1,
            error="exact intended suite unavailable or selection mismatch",
            exit=2,
        )
        write_json(out / "result.json", record)
        print(json.dumps(record))
        return 2
    # Preserve mutable engine exports before CTest can replace them. CTest JUnit is unique.
    engine_roots = [build / "tests/engine", build / "src/roots-gui/tests/engine"]
    reports = [
        p for folder in engine_roots if folder.is_dir() for p in folder.rglob("*.xml")
    ]
    record["archivedReportCount"] = len(reports)
    record["snapshotScope"] = [
        str(folder) for folder in engine_roots if folder.is_dir()
    ]
    for path in reports:
        target = out / "prior-engine-reports" / path.relative_to(build)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, target)
    argv = [
        "ctest",
        "--test-dir",
        str(build),
        *selection_arguments(a.suite),
        "--output-on-failure",
        "--no-tests=error",
        "--output-junit",
        str(out / "junit.xml"),
        "-j",
        "2",
    ]
    with (out / "ctest.log").open("w") as log:
        child = subprocess.Popen(
            argv, stdout=log, stderr=subprocess.STDOUT, start_new_session=True
        )
        try:
            child.wait(timeout=1200)
        except subprocess.TimeoutExpired:
            record["error"] = "owned CTest timeout"
            record["timedOut"] = True
            os.killpg(child.pid, signal.SIGTERM)
            try:
                child.wait(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(child.pid, signal.SIGKILL)
                child.wait(timeout=3)
        record["childSettled"] = child.poll() is not None
    record.update(
        command=argv,
        exit=child.returncode,
        endUTC=datetime.now(timezone.utc).isoformat(),
    )
    if (out / "junit.xml").is_file():
        cases = ET.parse(out / "junit.xml").getroot().findall(".//testcase")
        executed = [c for c in cases if c.find("skipped") is None]
        record.update(
            executed=len(executed),
            passed=sum(
                c.find("failure") is None and c.find("error") is None for c in executed
            ),
            failed=sum(
                c.find("failure") is not None or c.find("error") is not None
                for c in executed
            ),
            skipped=len(cases) - len(executed),
        )
        if {c.get("name") for c in cases} != expected:
            record["error"] = "JUnit identity mismatch"
    else:
        record["error"] = "missing current JUnit"
    for path in reports:
        if path.is_file():
            target = out / "current-engine-reports" / path.relative_to(build)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, target)
    write_json(out / "result.json", record)
    print(json.dumps(record))
    return int(
        child.returncode != 0
        or record["executed"] != len(expected)
        or record["failed"]
        or record["skipped"]
        or "error" in record
    )


if __name__ == "__main__":
    raise SystemExit(main())
