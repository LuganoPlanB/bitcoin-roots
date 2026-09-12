#!/usr/bin/env bash
#
# Copyright (c) 2019-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

export LC_ALL=C.UTF-8

# Homebrew's python@3.12 is marked as externally managed (PEP 668).
# Therefore, `--break-system-packages` is needed.
export CONTAINER_NAME="ci_mac_native"  # macos does not use a container, but the env var is needed for logging
export PIP_PACKAGES="--break-system-packages zmq"
export GOAL="deploy"
export CMAKE_GENERATOR="Ninja"
export BITCOIN_CONFIG="\
 -DCMAKE_BUILD_TYPE=Release \
 -DBUILD_TESTS=OFF -DBUILD_BENCH=OFF -DBUILD_FUZZ_BINARY=OFF \
 -DBUILD_TX=ON -DBUILD_UTIL=ON -DBUILD_WALLET_TOOL=ON \
 -DBUILD_GUI=ON -DWITH_QRENCODE=ON -DWITH_ZMQ=ON -DWITH_MINIUPNPC=ON -DREDUCE_EXPORTS=ON \
"
export CI_OS_NAME="macos"
export NO_DEPENDS=1
export NO_WERROR=1
export OSX_SDK=""
