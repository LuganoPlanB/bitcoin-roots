#!/usr/bin/env bash
#
# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

export LC_ALL=C.UTF-8

export CONTAINER_NAME=ci_native_extended_functional
export CI_IMAGE_NAME_TAG="mirror.gcr.io/ubuntu:24.04"
export PACKAGES="python3-zmq libzmq3-dev libevent-dev libboost-dev libsqlite3-dev libdb++-dev libminiupnpc-dev"
export NO_DEPENDS=1
export RUN_UNIT_TESTS=false
export RUN_FUNCTIONAL_TESTS=true
export RUN_FUZZ_TESTS=false
export TEST_RUNNER_EXTRA="--extended"
export GOAL="install"
export BITCOIN_CONFIG="-DWITH_ZMQ=ON -DWITH_BDB=ON -DWARN_INCOMPATIBLE_BDB=OFF -DWITH_MINIUPNPC=ON"
