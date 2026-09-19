#!/usr/bin/env bash
# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying file COPYING.

# Change only an already independently verified draft's visibility. This script
# deliberately neither builds, signs, creates, nor moves Git objects or tags.
export LC_ALL=C
set -Eeuo pipefail

if [[ "$#" -ne 3 ]]; then
    printf 'Usage: %s RELEASE_TAG CONFIRM_PUBLICATION VERIFIED_MANIFEST_SHA256\n' "$0" >&2
    exit 2
fi

release_tag=$1
confirmation=$2
manifest_sha256=$3
if [[ "$release_tag" != 'v29.4-roots.1' || "$confirmation" != 'true' || ! "$manifest_sha256" =~ ^[0-9a-f]{64}$ ]]; then
    printf 'Publication requires the locked tag, explicit confirmation, and a verified SHA256 manifest digest\n' >&2
    exit 1
fi
if ! command -v gh >/dev/null 2>&1; then
    printf 'GitHub CLI is required for the guarded publication transition\n' >&2
    exit 1
fi

observed_tag=$(gh release view "$release_tag" --json tagName --jq .tagName)
draft=$(gh release view "$release_tag" --json isDraft --jq .isDraft)
if [[ "$observed_tag" != "$release_tag" || "$draft" == 'null' ]]; then
    printf 'The requested release is not the verified draft for %s\n' "$release_tag" >&2
    exit 1
fi
if [[ "$draft" == 'false' ]]; then
    exit 0
fi
if [[ "$draft" != 'true' ]]; then
    printf 'Release draft state is invalid\n' >&2
    exit 1
fi
verification_directory=$(mktemp -d)
trap 'rm -rf -- "$verification_directory"' EXIT
gh release download "$release_tag" --pattern SHA512SUMS --dir "$verification_directory"
manifest="$verification_directory/SHA512SUMS"
if [[ ! -f "$manifest" || -L "$manifest" || "$(find "$verification_directory" -mindepth 1 -maxdepth 1 -type f | wc -l)" -ne 1 ]]; then
    printf 'Verified draft manifest download is invalid\n' >&2
    exit 1
fi
if [[ "$(sha256sum "$manifest" | awk '{print $1}')" != "$manifest_sha256" ]]; then
    printf 'Verified draft manifest digest differs\n' >&2
    exit 1
fi
gh release edit "$release_tag" --draft=false
