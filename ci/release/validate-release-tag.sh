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
if [[ ! "$release_tag" =~ ^v[0-9][0-9A-Za-z.+-]*$ ]]; then
    printf 'Release tag must start with v and contain only version characters: %s\n' "$release_tag" >&2
    exit 1
fi
if ! git check-ref-format "refs/tags/$release_tag"; then
    printf 'Release tag is not a valid Git tag name: %s\n' "$release_tag" >&2
    exit 1
fi
