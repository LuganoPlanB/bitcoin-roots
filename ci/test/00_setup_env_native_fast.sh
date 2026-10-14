#!/usr/bin/env bash
# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying file COPYING.
export LC_ALL=C.UTF-8
export CONTAINER_NAME=ci_native_fast
export CI_IMAGE_NAME_TAG="mirror.gcr.io/ubuntu:24.04"
export APT_LLVM_V="22"
export PACKAGES="clang-${APT_LLVM_V} llvm-${APT_LLVM_V} libclang-rt-${APT_LLVM_V}-dev libevent-dev libboost-dev libsqlite3-dev"
export NO_DEPENDS=1
export GOAL="install"
export RUN_UNIT_TESTS=true
export RUN_FUNCTIONAL_TESTS=true
export RUN_FUZZ_TESTS=false
export ROOTS_CI_CADENCE=fast
export BITCOIN_CONFIG="-DENABLE_IPC=OFF -DWITH_USDT=OFF -DWITH_ZMQ=OFF -DBUILD_GUI=OFF -DSANITIZERS=address,undefined -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang"
