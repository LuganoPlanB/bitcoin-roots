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

version=${release_tag#v}
core_version=${version%%-roots.*}
canonical_ref="${RELEASE_CANONICAL_REF:-origin/roots/${core_version}}"
core_ref="${RELEASE_CORE_REF:-refs/tags/v${core_version}}"

if ! canonical_commit="$(git rev-parse --verify --quiet "${canonical_ref}^{commit}")"; then
    printf 'Canonical release branch reference is unavailable: %s\n' "$canonical_ref" >&2
    exit 1
fi
if [[ "$tag_commit" != "$canonical_commit" ]]; then
    printf 'Release tag %s does not match canonical branch tip %s\n' "$release_tag" "$canonical_ref" >&2
    exit 1
fi
if ! core_commit="$(git rev-parse --verify --quiet "${core_ref}^{commit}")"; then
    printf 'Bitcoin Core base reference is unavailable: %s\n' "$core_ref" >&2
    exit 1
fi
if ! git merge-base --is-ancestor "$core_commit" "$tag_commit"; then
    printf 'Release tag %s is not based on %s\n' "$release_tag" "$core_ref" >&2
    exit 1
fi
if [[ "$core_commit" == "$tag_commit" ]]; then
    printf 'Release tag %s has no Roots commits after %s\n' "$release_tag" "$core_ref" >&2
    exit 1
fi
if git rev-list --min-parents=2 "${core_commit}..${tag_commit}" | grep -q .; then
    printf 'Release patch stack must be linear between %s and %s\n' "$core_ref" "$release_tag" >&2
    exit 1
fi
