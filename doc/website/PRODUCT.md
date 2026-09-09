# Product

<!-- impeccable:product-schema 1 -->

## Platform

web

## Stack

VitePress with Vue 3, using the local `vite-theme` package as the established
visual reference. The homepage is a custom Vue surface; documentation remains
Markdown-driven.

## Users

The primary audience is node operators who want to understand, install, and run
Bitcoin Roots. Developers and contributors remain an important secondary
audience served through the complete documentation library.

## Product Purpose

Bitcoin Roots is a Bitcoin full-node, wallet, GUI, and utility suite that makes
it practical to run a Bitcoin node at home without unnecessarily overloading
network traffic or CPU. Website success means operators can quickly understand
the project, reach trustworthy setup material, and navigate the complete
documentation.

## Positioning

Bitcoin Roots stays connected to the Bitcoin development trunk while preserving
conservative, configurable relay and mempool policy and Bitcoin Core-compatible
consensus. It does not enforce RDTS/BIP-110 consensus rules.

## Operating Context

Visitors evaluate the project, choose a supported build or installation path,
configure a node, operate it safely, and consult technical reference material.
Contributors also use the build, testing, architecture, and development guides.

## Capabilities and Constraints

- The website must reuse the repository's existing documentation without
  changing its contents.
- The primary homepage action is “Get started.”
- Consensus and local policy must remain clearly distinguished.
- Inherited references to Bitcoin Core and Bitcoin Knots are meaningful and
  must not be mass-rebranded.
- The generated site must preserve usable source-relative documentation links.

## Brand Commitments

The product name is Bitcoin Roots. The Plan ₿ Foundation identity and the
local `vite-theme` visual system are binding references. Existing project and
Plan ₿ artwork should be reused rather than replaced with fabricated assets.

## Evidence on Hand

- Product overview and positioning in `README.md`.
- Existing documentation throughout `doc/`, `contrib/`, `src/`, `test/`, `ci/`,
  `depends/`, and repository-root Markdown files.
- Bitcoin Roots logo at `src/qt/res/src/bitcoinroots-logo.svg`.
- Plan ₿ design tokens, page chrome, and assets in the adjacent `vite-theme`
  repository.

No testimonials, adoption metrics, performance claims, or download availability
claims may be invented.

## Product Principles

- Help node operators take the next safe step quickly.
- Preserve source documentation as the authoritative content.
- Make consensus neutrality and conservative policy legible without hype.
- Serve deep technical material without overwhelming first-time operators.
- Keep project provenance and upstream references intact.

## Accessibility & Inclusion

The public documentation experience must support keyboard navigation, visible
focus, reduced motion, semantic landmarks, readable contrast, and responsive
layouts.
