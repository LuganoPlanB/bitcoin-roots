---
title: Maintainer release model
description: How Bitcoin Roots turns a verified Bitcoin Core release into a reviewed, tested, and signed Roots release.
pageClass: bitcoin-roots-reading
head:
  - - meta
    - property: og:title
      content: Bitcoin Roots Maintainer Release Model
  - - meta
    - property: og:description
      content: The public review and release path from Bitcoin Core source to a signed Bitcoin Roots release.
---

# Maintainer release model

Bitcoin Roots releases are built as a reviewable line of ordinary Git history,
not as opaque source snapshots.

## The release path

1. **Verified Bitcoin Core tag.** Maintainers fetch the exact annotated Core tag
   into a new disposable repository and verify its tag object, signature, commit,
   and tree.
2. **Reviewed Roots patch stack.** Each retained Roots change is replayed and
   reviewed against that Core release. The resulting commits record normal Git
   ancestry from the verified Core commit.
3. **Trusted CI.** Control-plane validation treats candidate code as untrusted
   input, reproduces the replay, checks the expected source identity, and runs
   the required build and test matrix.
4. **Signed release.** Only an authorized production commit may be tagged,
   packaged, independently checked, signed, and then published.

Bitcoin Core is an external fetch input to this process. Its source is retrieved
only for verification and replay, then the disposable repository may be removed.
Roots does not import Core as a submodule, vendor snapshot, or copied `upstream/`
tree. A production Roots tag is self-contained: a normal clone contains the Core
ancestry and reviewed Roots commits needed to inspect that release.

## Policy remains separate from consensus

Bitcoin Roots provides conservative, configurable mempool, relay, and mining
policy while preserving Bitcoin Core-compatible consensus. A transaction
rejected by local policy may still appear in a valid block, and Roots must accept
that valid block. Bitcoin Roots does not enforce RDTS/BIP110 consensus rules.

## Review the evidence

The [production release runbook](/doc/maintainers/roots-production-release)
contains the complete maintainer procedure, safety stops, recovery decisions,
and immutable 29.4 identifiers. The checked-in evidence includes the
[promotion contract](https://github.com/LuganoPlanB/bitcoin-roots/blob/main/contrib/roots/promotion-29.4.json),
[frozen-production record](https://github.com/LuganoPlanB/bitcoin-roots/blob/main/contrib/roots/frozen-production-29.4.json),
and [production accounting](https://github.com/LuganoPlanB/bitcoin-roots/blob/main/contrib/roots/production-accounting-29.4.json).

Those records bind review claims to exact objects and trees. Their current
authorization values are false, so they do not authorize a push, tag, signature,
release draft, or publication. Publication requires a later reviewed update that
authorizes the exact production identity and passes every release gate.
