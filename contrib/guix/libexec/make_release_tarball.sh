#!/usr/bin/env bash
# Copyright (c) 2020 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Generate a reproducible source archive from the checked-out Git revision.
# The generated build-info header preserves the reviewed tag in a source tree
# that intentionally no longer carries Git metadata.
export LC_ALL=C
set -Eeuo pipefail

if [[ "$#" -ne 2 || -z "${REFERENCE_DATETIME:-}" ]]; then
    printf 'Usage: REFERENCE_DATETIME=@epoch %s OUTPUT_ARCHIVE DISTNAME\n' "$0" >&2
    exit 2
fi

git_archive=$1
distname=$2
work_dir="$(mktemp -d)"
trap 'rm -rf -- "$work_dir"' EXIT

git archive --prefix="${distname}/" HEAD |
    tar -C "$work_dir" -xp \
        --exclude .cirrus.yml \
        --exclude '.git*' \
        --exclude ci \
        --exclude '*minisketch*' \
        --exclude 'doc/release-notes'

git_build_info="$(cmake -P cmake/script/GenerateBuildInfo.cmake)"
GIT_BUILD_INFO="$git_build_info" python3 - \
    "$work_dir/${distname}/cmake/script/GenerateBuildInfo.cmake" <<'PYTHON'
import os
from pathlib import Path
import sys

path = Path(sys.argv[1])
marker = "// No build information available"
contents = path.read_text(encoding="utf-8")
if contents.count(marker) != 1:
    raise SystemExit("missing generated build-info marker")
path.write_text(contents.replace(marker, os.environ["GIT_BUILD_INFO"]), encoding="utf-8")
PYTHON

mkdir -p "$(dirname -- "$git_archive")"
tar -C "$work_dir" \
    --format=ustar \
    --sort=name \
    --mode='u+rw,go+r-w,a+X' --owner=0 --group=0 \
    --mtime="$REFERENCE_DATETIME" \
    -c "$distname" | gzip -9n > "$git_archive"
