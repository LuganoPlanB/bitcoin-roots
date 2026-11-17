#!/usr/bin/env bash
# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

export LC_ALL=C.UTF-8
set -Eeuo pipefail

if [[ "$#" -ne 3 ]]; then
    printf 'Usage: %s TARGET_ARCH BUILD_DIR INSTALL_DIR (CC, CXX and RELEASE_TAG required)\n' "$0" >&2
    exit 2
fi
case "$1" in
    x86_64) triple=x86_64-linux-musl ;;
    arm_64) triple=aarch64-linux-musl ;;
    arm_hf) triple=arm-linux-musleabihf ;;
    riscv_64) triple=riscv64-linux-musl ;;
    *) printf 'Unsupported musl target: %s\n' "$1" >&2; exit 2 ;;
esac
: "${CC:?Use the musl action CC output}"
: "${CXX:?Use the musl action CXX output}"
: "${RELEASE_TAG:?Set the candidate Roots release tag}"
if [[ "$("$CC" -dumpmachine)" != "$triple" || "$("$CXX" -dumpmachine)" != "$triple" ]]; then
    printf 'Compiler target does not match %s\n' "$triple" >&2
    exit 1
fi
source_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd -P)"
toolchain_prefix="$(cd -- "$(dirname -- "$CC")/.." && pwd -P)"
target_prefix="$(dirname -- "$toolchain_prefix")/$triple"
build_dir=$2
install_dir=$3
for required in "$toolchain_prefix/settings.cmake" "$target_prefix/include/boost/version.hpp" \
    "$target_prefix/lib/libsqlite3.a" "$target_prefix/lib/libzmq.a"; do
    if [[ ! -f "$required" ]]; then
        printf 'Missing musl SDK input: %s\n' "$required" >&2
        exit 1
    fi
done
# Do not mix a reused build or installation with another target/profile.
for directory in "$build_dir" "$install_dir"; do
    if [[ -e "$directory" || -L "$directory" ]]; then
        printf 'Musl output must be a new directory: %s\n' "$directory" >&2
        exit 1
    fi
done
mkdir -p "$build_dir"
release_flags='-g0 -O2 -fstack-protector-all -D_FORTIFY_SOURCE=2 -fno-strict-overflow'
{
    cmake -S "$source_dir" -B "$build_dir" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$toolchain_prefix/settings.cmake" \
        -DARCH="$triple" -DTOOLCHAIN_PREFIX="$toolchain_prefix" -DTARGET_PREFIX="$target_prefix" \
        -DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" \
        -DCMAKE_C_FLAGS="$release_flags" -DCMAKE_CXX_FLAGS="$release_flags" \
        -DCCACHE=OFF -DWITH_CCACHE=OFF -DFORCE_STATIC=OFF -DBUILD_SHARED_LIBS=OFF \
        -DCMAKE_EXE_LINKER_FLAGS='-static-pie -static-libgcc -static-libstdc++' \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$install_dir" \
        -DCLIENT_VERSION_TAG="$RELEASE_TAG" -DBUILD_GUI=OFF -DENABLE_IPC=OFF \
        -DENABLE_WALLET=ON -DWITH_ZMQ=ON -DWITH_USDT=OFF \
        -DBUILD_TESTS=OFF -DBUILD_BENCH=OFF -DBUILD_FUZZ_BINARY=OFF \
        -DBUILD_BITCOIN_BIN=ON -DBUILD_DAEMON=ON -DBUILD_CLI=ON \
        -DBUILD_TX=ON -DBUILD_UTIL=ON -DBUILD_WALLET_TOOL=ON \
        -DBUILD_KERNEL_LIB=OFF -DBUILD_UTIL_CHAINSTATE=OFF
    cmake --build "$build_dir" --parallel 2
    cmake --install "$build_dir" --strip
} 2>&1 | tee "$build_dir/build.log"
python3 "$source_dir/ci/release/validate-musl.py" "$install_dir" "$1"
