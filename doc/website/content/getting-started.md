---
title: Get started
description: Choose the right path to build, configure, and operate Bitcoin Roots.
---

# Get started

Bitcoin Roots connects to the Bitcoin peer-to-peer network to download and fully
validate blocks and transactions. It also includes a wallet and graphical user
interface, which can be optionally built.

Choose the guide for the system where you plan to run your node:

This source tree tracks the Core v32.1 port and v32.1-roots.1 source. Read the
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

## Go deeper

The [documentation atlas](/documentation) indexes every Markdown guide shipped
in this repository, including operations, architecture, wallet, API, testing,
release, and embedded-library references.
