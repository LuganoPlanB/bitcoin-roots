#!/usr/bin/env bash
# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

export LC_ALL=C
set -Eeuo pipefail

if [[ "$#" -ne 2 ]]; then
    printf 'Usage: %s RELEASE_TAG OUTPUT_FILE\n' "$0" >&2
    exit 2
fi

release_tag=$1
output_file=$2
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
"$script_dir/validate-release-source.sh" "$release_tag"

version=${release_tag#v}
core_version=${version%%-roots.*}
core_ref="${RELEASE_CORE_REF:-refs/tags/v${core_version}}"
core_commit="$(git rev-parse --verify "${core_ref}^{commit}")"
tag_commit="$(git rev-parse --verify "refs/tags/${release_tag}^{commit}")"

output_parent=${output_file%/*}
if [[ "$output_parent" == "$output_file" ]]; then
    output_parent=.
fi
mkdir -p -- "$output_parent"
output_parent="$(cd -- "$output_parent" && pwd -P)"
output_path="$output_parent/${output_file##*/}"
patch_tmp="$(mktemp "$output_parent/.roots-patch.XXXXXX")"
replay_dir="$(mktemp -d)"

cleanup() {
    rm -f -- "$patch_tmp"
    rm -rf -- "$replay_dir"
}
trap cleanup EXIT

git format-patch --stdout --binary --full-index --base="$core_commit" \
    "${core_commit}..${tag_commit}" > "$patch_tmp"
if [[ ! -s "$patch_tmp" ]]; then
    printf 'Generated patch series is empty for %s\n' "$release_tag" >&2
    exit 1
fi

git clone --quiet --no-hardlinks --shared . "$replay_dir/repository"
git -C "$replay_dir/repository" checkout --quiet --detach "$core_commit"
git -C "$replay_dir/repository" \
    -c user.name='Bitcoin Roots release verification' \
    -c user.email='release-verification@invalid' \
    am -3 "$patch_tmp" >/dev/null

expected_tree="$(git rev-parse --verify "${tag_commit}^{tree}")"
replayed_tree="$(git -C "$replay_dir/repository" rev-parse --verify 'HEAD^{tree}')"
if [[ "$replayed_tree" != "$expected_tree" ]]; then
    printf 'Patch replay tree does not match release tag %s\n' "$release_tag" >&2
    exit 1
fi

mv -f -- "$patch_tmp" "$output_path"
printf '%s\n' "$output_path"
