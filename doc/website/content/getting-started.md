---
title: Get started
description: Choose the right path to build, configure, and operate Bitcoin Roots.
---

# Get started

Bitcoin Roots connects to the Bitcoin peer-to-peer network to download and fully
validate blocks and transactions. It also includes a wallet and graphical user
interface, which can be optionally built.

Choose the guide for the system where you plan to run your node:

- [Linux and other Unix-like systems](/doc/build-unix)
- [macOS](/doc/build-osx)
- [Windows](/doc/build-windows)
- [Windows with Microsoft Visual Studio](/doc/build-windows-msvc)
- [FreeBSD](/doc/build-freebsd)
- [OpenBSD](/doc/build-openbsd)
- [NetBSD](/doc/build-netbsd)
- [Headless Docker image](/contrib/docker/README)

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
