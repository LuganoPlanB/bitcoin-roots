# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Make local, synthetic public release fixtures with the installed GnuPG."""

from datetime import datetime, timezone
import hashlib
import subprocess

GPG = "/usr/bin/gpg"
GPGCONF = "/usr/bin/gpgconf"
ENV = {"LC_ALL": "C", "TZ": "UTC", "PATH": "/usr/bin:/bin"}
SIGN_TIME = "1577836800!"
ARTIFACT = b"Public synthetic release bytes for the GUI verifier.\n"


def now():
    return datetime.now(timezone.utc).isoformat()


def make(root, records):
    def command(argv, *, data=None, capture=False, keep_status=False):
        started = now()
        run = subprocess.run(
            argv, cwd=root, env=ENV, input=data, capture_output=True, timeout=15
        )
        records.append(
            dict(
                argv=list(map(str, argv)),
                cwd=str(root),
                startUTC=started,
                endUTC=now(),
                exit=run.returncode,
                status=run.stdout.decode() if keep_status else "",
                stderr=run.stderr.decode(errors="replace"),
            )
        )
        if run.returncode and not keep_status:
            raise RuntimeError(
                f"isolated fixture command failed with exit {run.returncode}"
            )
        return run.stdout if capture else run

    signer = root / "synthetic-signing-only"
    signer.mkdir(mode=0o700)
    fmt = [GPG, "--homedir", str(signer), "--no-options", "--batch", "--no-tty"]
    names = dict(
        current="Current fixture <current@fixture.invalid>",
        expired="Expired fixture <expired@fixture.invalid>",
    )
    (root / "fixture-release.bin").write_bytes(ARTIFACT)
    digest = hashlib.sha256(ARTIFACT).hexdigest()
    manifest = f"{digest}  fixture-release.bin\n".encode()
    (root / "SHA256SUMS").write_bytes(manifest)
    fingerprints = {}
    try:
        for tag, uid in names.items():
            fixed = [*fmt, "--faked-system-time", SIGN_TIME]
            command(
                [
                    *fixed,
                    "--pinentry-mode",
                    "loopback",
                    "--passphrase",
                    "",
                    "--quick-generate-key",
                    uid,
                    "ed25519",
                    "sign",
                    "1d" if tag == "expired" else "0",
                ]
            )
            listing = command(
                [*fmt, "--with-colons", "--fingerprint", "--list-keys", uid],
                capture=True,
            ).decode()
            fingerprints[tag] = next(
                line.split(":")[9]
                for line in listing.splitlines()
                if line.startswith("fpr:")
            )
            key = command([*fmt, "--armor", "--export", uid], capture=True)
            (root / f"{tag}.key").write_bytes(key)
            command(
                [
                    *fixed,
                    "--local-user",
                    uid,
                    "--digest-algo",
                    "SHA256",
                    "--armor",
                    "--output",
                    str(root / f"{tag}.asc"),
                    "--detach-sign",
                ],
                data=manifest,
            )
            binary_key = command([*fmt, "--export", uid], capture=True)
            (root / f"{tag}.gpg").write_bytes(binary_key)
        command(
            [
                *fmt,
                "--faked-system-time",
                SIGN_TIME,
                "--local-user",
                names["current"],
                "--digest-algo",
                "SHA256",
                "--output",
                str(root / "current.sig"),
                "--detach-sign",
            ],
            data=manifest,
        )
        # Independent GnuPG machine-status proof using only the exported public
        # certificate. It uses actual wall clock time, including for expiry.
    finally:
        command([GPGCONF, "--homedir", str(signer), "--kill", "all"])
        import shutil

        shutil.rmtree(signer)

    verification = root / "independent-verifier"
    verification.mkdir(mode=0o700)
    for tag in names:
        argv = [
            GPG,
            "--homedir",
            str(verification),
            "--no-options",
            "--batch",
            "--no-tty",
            "--no-default-keyring",
            "--keyring",
            str(root / f"{tag}.gpg"),
            "--trust-model",
            "always",
            "--no-auto-key-import",
            "--no-auto-key-retrieve",
            "--no-auto-key-locate",
            "--disable-dirmngr",
            "--no-autostart",
            "--lock-never",
            "--status-fd",
            "1",
            "--verify",
            str(root / f"{tag}.asc"),
            str(root / "SHA256SUMS"),
        ]
        verdict = command(argv, keep_status=True)
        assert verdict.returncode == 0 and fingerprints[tag].encode() in verdict.stdout
        marker = b"EXPKEYSIG" if tag == "expired" else b"GOODSIG"
        assert marker in verdict.stdout and b"VALIDSIG" in verdict.stdout
    assert not list(verification.iterdir()), (
        "public verification used no persistent helper or trust state"
    )
    verification.rmdir()
    return fingerprints
