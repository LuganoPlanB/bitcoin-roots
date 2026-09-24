#!/usr/bin/env bash
# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

export LC_ALL=C
set -Eeuo pipefail

if [[ "$#" -ne 3 ]]; then
    printf 'Usage: %s MANIFEST PUBLIC_KEY_FILE EXPECTED_FINGERPRINT\n' "$0" >&2
    exit 2
fi

manifest=$1
public_key_file=$2
expected_fingerprint=$3
signature="${manifest}.asc"

if [[ ! -f "$manifest" ]]; then
    printf 'Manifest does not exist: %s\n' "$manifest" >&2
    exit 1
fi
if [[ -z "${BITCOIN_ROOTS_GPG_SK:-}" ]]; then
    printf 'BITCOIN_ROOTS_GPG_SK is not configured; publishing an unsigned manifest\n' >&2
    exit 0
fi
if [[ ! -f "$public_key_file" ]]; then
    printf 'Public key does not exist: %s\n' "$public_key_file" >&2
    exit 1
fi
if [[ ! "$expected_fingerprint" =~ ^[0-9A-F]{40}$ ]]; then
    printf 'Expected fingerprint must be 40 uppercase hexadecimal characters\n' >&2
    exit 1
fi

gpg_home="$(mktemp -d)"
cleanup() {
    rm -rf -- "$gpg_home"
}
trap cleanup EXIT
chmod 0700 "$gpg_home"
export GNUPGHOME="$gpg_home"

public_fingerprint="$({
    gpg --batch --with-colons --import-options show-only --import "$public_key_file" |
        awk -F: '$1 == "fpr" { print $10; exit }'
})"
if [[ "$public_fingerprint" != "$expected_fingerprint" ]]; then
    printf 'Unexpected release public-key fingerprint: %s\n' "$public_fingerprint" >&2
    exit 1
fi

printf '%s\n' "$BITCOIN_ROOTS_GPG_SK" | gpg --batch --import >/dev/null
secret_fingerprint="$({
    gpg --batch --with-colons --list-secret-keys "$expected_fingerprint" |
        awk -F: '$1 == "fpr" { print $10; exit }'
})"
if [[ "$secret_fingerprint" != "$expected_fingerprint" ]]; then
    printf 'The configured secret key does not match the release public key\n' >&2
    exit 1
fi

gpg --batch --yes --armor --local-user "$expected_fingerprint" \
    --output "$signature" --detach-sign "$manifest"
gpg --batch --verify "$signature" "$manifest"
