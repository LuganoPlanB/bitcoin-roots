#!/usr/bin/env bash
# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

export LC_ALL=C
set -Eeuo pipefail

if [[ "$#" -ne 9 ]]; then
    printf 'Usage: %s DOWNLOAD_DIR OUTPUT_DIR PUBLIC_KEY_FILE RELEASE_NAME EXPECTED_PACKAGE_COUNT EVIDENCE_FILE BUILD_EVIDENCE_FILE SOURCE_REPOSITORY SOURCE_REVISION\n' "$0" >&2
    exit 2
fi

download_dir=$1
output_dir=$2
public_key_file=$3
release_name=$4
expected_package_count=$5
evidence_file=$6
build_evidence_file=$7
source_repository=$8
source_revision=$9
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
archive_tool="$script_dir/archive.py"

if [[ ! -d "$download_dir" ]]; then
    printf 'Download directory does not exist: %s\n' "$download_dir" >&2
    exit 1
fi
if [[ ! -f "$public_key_file" ]]; then
    printf 'Public key does not exist: %s\n' "$public_key_file" >&2
    exit 1
fi
if [[ ! "$expected_package_count" =~ ^[1-9][0-9]*$ ]]; then
    printf 'Expected package count must be a positive integer: %s\n' "$expected_package_count" >&2
    exit 1
fi

mkdir -p "$output_dir"
if find "$output_dir" -mindepth 1 -print -quit | grep -q .; then
    printf 'Output directory is not empty: %s\n' "$output_dir" >&2
    exit 1
fi

mapfile -d '' package_paths < <(
    find "$download_dir" -type f \( -name '*.tar.gz' -o -name '*.zip' \) -print0 | sort -z
)
if [[ "${#package_paths[@]}" -ne "$expected_package_count" ]]; then
    printf 'Expected %s release packages, found %s\n' "$expected_package_count" "${#package_paths[@]}" >&2
    exit 1
fi

declare -A package_names=()
for package_path in "${package_paths[@]}"; do
    package_name=${package_path##*/}
    if [[ "$package_name" == *$'\n'* ]]; then
        printf 'Release package names must not contain newlines\n' >&2
        exit 1
    fi
    if [[ -n "${package_names[$package_name]:-}" ]]; then
        printf 'Duplicate release package name: %s\n' "$package_name" >&2
        exit 1
    fi
    package_names[$package_name]=1
done

if [[ ! -f "$evidence_file" || -L "$evidence_file" || "${evidence_file##*/}" != roots-release-evidence.json ]]; then
    printf 'Release evidence is unavailable or unsafe: %s\n' "$evidence_file" >&2
    exit 1
fi
if [[ ! -f "$build_evidence_file" || -L "$build_evidence_file" || "${build_evidence_file##*/}" != roots-release-build-evidence.json ]]; then
    printf 'Release build evidence is unavailable or unsafe: %s\n' "$build_evidence_file" >&2
    exit 1
fi
build_evidence_directory="$(cd -- "$(dirname -- "$build_evidence_file")" && pwd)"
(
    cd "$build_evidence_directory"
    python3 "$script_dir/roots-build-evidence.py" verify \
        --artifacts "$(cd -- "$download_dir" && pwd)" \
        --source-repository "$(cd -- "$source_repository" && pwd)" \
        --source-revision "$source_revision" \
        --expected-count "$expected_package_count" \
        --output roots-release-build-evidence.json
)
python3 "$script_dir/roots-release-evidence.py" \
    --ledger contrib/roots/lineage-ledger.json \
    --manifest contrib/roots/adaptation-manifest-29.3.json \
    --replay-result contrib/roots/replay-29.4-proposal/acceptance-evidence.json \
    --fixture contrib/roots/core-29.4-migration-fixture.json \
    --registry contrib/roots/post-methodology-adaptations.json \
    --accounting contrib/roots/continuous-accounting-pr.json \
    --release-accounting contrib/roots/release-accounting.json \
    --build-evidence "$build_evidence_file" \
    --source-repository "$source_repository" \
    --source-revision "$source_revision" \
    --candidate-tree "sha1:$(git -C "$source_repository" rev-parse "${source_revision}^{tree}")" \
    --output "$evidence_file" --verify
cp -- "$evidence_file" "$output_dir/roots-release-evidence.json"
cp -- "$build_evidence_file" "$output_dir/roots-release-build-evidence.json"

archive_root="$(RELEASE_TAG="$release_name" python3 "$archive_tool" root-name)"
for package_path in "${package_paths[@]}"; do
    package_name=${package_path##*/}
    python3 "$archive_tool" validate "$package_path" "$archive_root"
    cp -- "$package_path" "$output_dir/$package_name"
done

mapfile -d '' package_names_sorted < <(printf '%s\0' "${!package_names[@]}" | sort -z)
manifest="$output_dir/SHA512SUMS"
{
    printf '# Bitcoin Roots release: %s\n' "$release_name"
    printf '# This manifest is signed using the following OpenPGP public key:\n'
    while IFS= read -r line || [[ -n "$line" ]]; do
        printf '# %s\n' "$line"
    done < "$public_key_file"
    printf '#\n'
    for package_name in "${package_names_sorted[@]}"; do
        (cd "$output_dir" && sha512sum -- "$package_name")
    done
    (cd "$output_dir" && sha512sum roots-release-evidence.json)
    (cd "$output_dir" && sha512sum roots-release-build-evidence.json)
} > "$manifest"
