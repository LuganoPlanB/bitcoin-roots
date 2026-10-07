#!/usr/bin/env bash
#
# Copyright (c) 2026 The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

export LC_ALL=C.UTF-8
export CI_IMAGE_NAME_TAG="mirror.gcr.io/ubuntu:24.04"
export CONTAINER_NAME=ci_native_gui
export PACKAGES="python3-zmq qt6-base-dev qt6-tools-dev qt6-l10n-tools libevent-dev libboost-dev libminiupnpc-dev libzmq3-dev libqrencode-dev libsqlite3-dev capnproto libcapnp-dev python3-pip"
export PIP_PACKAGES="--break-system-packages pycapnp"
export NO_DEPENDS=1
export GOAL="install"
export RUN_UNIT_TESTS=true
export RUN_FUNCTIONAL_TESTS=false
export RUN_FUZZ_TESTS=false
export BITCOIN_CONFIG="\
 -DWITH_ZMQ=ON -DBUILD_GUI=ON -DWITH_QRENCODE=ON \
"
