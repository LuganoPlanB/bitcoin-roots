#!/usr/bin/env bash
#
# Copyright (c) 2026-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

export LC_ALL=C

# Shared APT refresh helper for Linux CI scripts. CI_RETRY_EXE is a
# space-separated command supplied by ci/test/00_setup_env.sh (normally
# "retry --"), so split it once into an argv array before execution.
ci_retry_apt_update()
{
    local -a retry_command
    read -r -a retry_command <<< "${CI_RETRY_EXE:?CI_RETRY_EXE must be set}"
    "${retry_command[@]}" apt-get -o APT::Update::Error-Mode=any update
}
