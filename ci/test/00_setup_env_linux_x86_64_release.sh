#!/usr/bin/env bash
#
# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

export LC_ALL=C.UTF-8

export CONTAINER_NAME=ci_linux_x86_64_release
export CI_IMAGE_NAME_TAG="mirror.gcr.io/ubuntu:24.04"
export CI_IMAGE_PLATFORM="linux/amd64"
export HOST=x86_64-pc-linux-gnu
export GOAL="install"
export BITCOIN_CONFIG="\
 -DCMAKE_BUILD_TYPE=Release \
 -DBUILD_TESTS=OFF -DBUILD_BENCH=OFF -DBUILD_FUZZ_BINARY=OFF \
 -DBUILD_TX=ON -DBUILD_UTIL=ON -DBUILD_WALLET_TOOL=ON \
 -DBUILD_GUI=ON -DREDUCE_EXPORTS=ON \
"
