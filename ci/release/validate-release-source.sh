#!/usr/bin/env bash
# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

export LC_ALL=C
set -Eeuo pipefail

if [[ "$#" -ne 1 ]]; then
    printf 'Usage: %s RELEASE_TAG\n' "$0" >&2
    exit 2
fi

release_tag=$1
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
"$script_dir/validate-release-tag.sh" "$release_tag"

if ! tag_object="$(git rev-parse --verify --quiet "refs/tags/${release_tag}^{tag}")"; then
    printf 'Release tag must be annotated: %s\n' "$release_tag" >&2
    exit 1
fi
tag_commit="$(git rev-parse --verify "${tag_object}^{commit}")"
head_commit="$(git rev-parse --verify 'HEAD^{commit}')"
if [[ "$head_commit" != "$tag_commit" ]]; then
    printf 'Checked-out commit does not match release tag %s\n' "$release_tag" >&2
    exit 1
fi

main_ref="${RELEASE_MAIN_REF:-origin/main}"
if ! git rev-parse --verify --quiet "${main_ref}^{commit}" >/dev/null; then
    printf 'Release branch reference is unavailable: %s\n' "$main_ref" >&2
    exit 1
fi
if ! git merge-base --is-ancestor "$tag_commit" "$main_ref"; then
    printf 'Release tag %s is not reachable from %s\n' "$release_tag" "$main_ref" >&2
    exit 1
fi
