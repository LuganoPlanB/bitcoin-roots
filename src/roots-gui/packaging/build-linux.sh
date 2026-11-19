#!/bin/sh
# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
export LC_ALL=C.UTF-8
set -eu

if [ "$#" -ne 1 ] || [ -z "$1" ]; then
    printf 'Usage: %s NEW_BUILD_DIRECTORY\n' "$0" >&2
    exit 2
fi
source_dir=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd -P)
build_root=$(realpath -m -- "$1")
case "$build_root/" in
    "$source_dir/"*) printf 'Use a build directory outside the source tree.\n' >&2; exit 2 ;;
esac
mkdir -- "$build_root"

# Leave the work directory available for inspection on success and failure.
# The compile flags retain stable diagnostic paths in this distribution.
export CFLAGS="-ffile-prefix-map=$source_dir=roots-gui-source -ffile-prefix-map=$build_root=roots-gui-build"
export CXXFLAGS="$CFLAGS"
export CCACHE_DISABLE=1
cmake -S "$source_dir" -B "$build_root/build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DCMAKE_C_COMPILER_LAUNCHER= -DCMAKE_CXX_COMPILER_LAUNCHER= -DWITH_CCACHE=OFF \
    -DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF -DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF \
    -DCURL_BROTLI=OFF -DCURL_ZLIB=OFF -DCURL_ZSTD=OFF \
    -DFETCHCONTENT_FULLY_DISCONNECTED=ON -DROOTS_GUI_BUILD_TESTS=OFF -DROOTS_GUI_LINUX_PACKAGE=ON
cmake --build "$build_root/build" --parallel 2 --target roots-gui --verbose
cmake --install "$build_root/build" --prefix "$build_root/stage" --strip
python3 -B "$build_root/stage/install.py" seal
tar --create --gzip --file "$build_root/roots-gui-linux-x86_64.tar.gz" --directory "$build_root/stage" .
