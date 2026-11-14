---
title: Get started
description: Choose the right path to build, configure, and operate Bitcoin Roots.
---

# Get started

Bitcoin Roots connects to the Bitcoin peer-to-peer network to download and fully
validate blocks and transactions. It also includes a wallet and graphical user
interface, which can be optionally built.

Choose the guide for the system where you plan to run your node:

This source tree tracks the Core v32.1 port and v32.1-roots.2 source. Read the
[32.x port review](/doc/roots-v32-port-review) and
[current build requirements](/doc/roots-build-portability) before building.
Source availability does not establish release publication. Published downloads
and patches are listed in the [release patch browser](/patches/), which excludes
unpublished drafts. Use that catalogue to check which versions have public assets.

- [Linux and other Unix-like systems](/doc/build-unix)
- [macOS](/doc/build-osx)
- [Windows](/doc/build-windows)
- [Windows with Microsoft Visual Studio](/doc/build-windows-msvc)
- [FreeBSD](/doc/build-freebsd)
- [OpenBSD](/doc/build-openbsd)
- [NetBSD](/doc/build-netbsd)

## Before you run

Read the [configuration reference](/doc/bitcoin-conf) before changing defaults.
For a smaller home-node footprint, the existing guides cover
[reducing memory usage](/doc/reduce-memory) and
[reducing traffic](/doc/reduce-traffic).

Bitcoin Roots follows Bitcoin Core-compatible consensus while maintaining
conservative, configurable transaction relay and mempool policy. It does not
enforce RDTS/BIP-110 consensus rules. Read the
[transaction relay policy documentation](/doc/policy/README) for the boundary
between local policy and block validity.

## Recover a legacy wallet offline

The v32.1-roots.2 source includes `bitcoin-wallet dumpwallet` for a supported,
clean offline legacy BDB copy. Choose the input, a new output filename and the
encoding network explicitly with `-wallet`, `-dumpfile` and `-chain`. Retain the
original backup. The export contains unencrypted private keys and private
metadata; keep it in a protected location. Existing output files are never
replaced.

Keep every exported individual key as well as the identified HD roots: a root
does not recover unrelated imported keys. This command rejects descriptor/SQLite
wallets and corrupt or unsupported required records. It does not restore legacy
node-wallet operation, transaction history, a balance or complete wallet state.

Read the [offline legacy wallet export guide](/doc/legacy-wallet-export) for
supported formats, passphrase entry and recovery limits before exporting. Check
the public release catalogue above for version availability; this source guide
does not establish a published download.

## Go deeper

The [documentation atlas](/documentation) indexes every Markdown guide shipped
in this repository, including operations, architecture, wallet, API, testing,
release, and embedded-library references.

Copyright (C) 2026 Plan-B Foundation
