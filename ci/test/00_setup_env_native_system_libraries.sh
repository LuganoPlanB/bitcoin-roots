#!/usr/bin/env bash
# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying file COPYING.

export LC_ALL=C.UTF-8
export CONTAINER_NAME=ci_native_system_libraries
export CI_IMAGE_NAME_TAG="mirror.gcr.io/ubuntu:26.04"
export PACKAGES="libboost-dev libsqlite3-dev libleveldb-dev libsecp256k1-dev libcapnp-dev capnproto python3-pip"
export PIP_PACKAGES="--break-system-packages pycapnp"
export NO_DEPENDS=1
export GOAL="install"
export RUN_UNIT_TESTS=true
export RUN_FUNCTIONAL_TESTS=true
export RUN_FUZZ_TESTS=false
# Explicitly require both alternatives: an unavailable MemEnv/module fails
# configure and must not silently turn this into embedded-library coverage.
export BITCOIN_CONFIG="-DBUILD_GUI=OFF -DENABLE_IPC=ON -DWITH_SYSTEM_LEVELDB=ON -DWITH_SYSTEM_LIBSECP256K1=ON"
