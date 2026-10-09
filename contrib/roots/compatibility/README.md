# Core and Roots compatibility review contract

This contract defines the evidence expected from external paired-node
qualification tooling. It is a specification for that tooling, not a report
that the profiles below have already run. Read the
[maintainer workflow](../README.md#editing-policy-safely) and
[contribution guide](../../../CONTRIBUTING.md) alongside it.

## Inputs and evidence ownership

For a Core port or a change affecting policy, validation or serialization,
record the matching Core base and Roots candidate as full 40-hex commit SHAs.
Identify the touched behavior, source disposition, fixture owner and owning
checks in commit messages and the port PR. State expected admission/relay
differences before running comparisons; shared Core policy may reject the same
transaction, so Core admission is not presumed successful.

Each run must identify the two source directories and their cleanliness, binary
paths, SHA256 digests and versions, build commands/configuration and provenance,
effective launch flags, fixture IDs, selected profile, timeouts and results.
A caller-supplied SHA or version string alone does not authenticate a binary.
Locally supplied binaries without trusted build evidence are unverified;
qualification builds must come from the pinned sources with comparable recorded
configuration. Identical resolved binary paths or digests cannot qualify a pair.
Commit pinning and official-tag signer authentication are separate claims.

New external fixtures and their helpers belong in `contrib/roots/compatibility/`;
their tooling tests belong in `ci/test/`. Keep existing Core-derived tests,
including `test/functional/test_framework/`, read-only for this external tooling
work. Reuse helpers through an explicit source path with recorded provenance
rather than copying the framework. Feature owners maintain their fixtures as
the corresponding behavior is ported; reports are CI/local run artifacts, never
a checked-in source inventory or test-results database.

## Required profiles

Both profiles run isolated regtest nodes with separate temporary datadirs,
loopback cookie-authenticated RPC, no peer discovery/listening, bounded startup,
RPC and case timeouts, and cleanup of owned processes only. Neither uses a real
wallet or datadir. A common deterministic mature funding chain permits the same
serialized transaction and block bytes to be supplied to both nodes.

| Case | Smoke | Full | Expected observation |
| --- | --- | --- | --- |
| Ordinary transaction/block control | Yes | Yes | Documented compatible admission; both accept the identical valid block. |
| Aggregate datacarrier boundary | Yes | Yes | Roots rejects the selected transaction locally; both accept its identical valid block. |
| Consensus-invalid block negative control | Yes | Yes | Both reject the same invalid block and retain the prior accepted state. |
| Legacy sigop policy boundary | No | Yes | Roots policy rejection and paired valid-block acceptance. |
| Sub-dust fee-penalty boundary | No | Yes | Below/at threshold admission under a named explicit Roots configuration; paired valid-block acceptance. |
| Disconnect/reconnect and restart | No | Yes | Invalidate/reconsider selected datacarrier and sub-dust blocks, restart using the same owned datadirs, then compare persisted state. |

After each accepted block, compare acceptance, best-block hash/height and
normalized `gettxout` snapshots for fixture outputs. Record mempool outcomes
separately; do not require equal reject strings or mempools where policies
intentionally differ. Assert Core admission only as specified by the fixture.
For sub-dust, document the smallest option deviation that permits the output
form while retaining the fee penalty; never silently disable the tested rule.
Control-transaction mempool persistence requires an explicitly compatible
configuration. Two smoke runs should agree semantically after normalizing
timing and temporary paths.

Initial specifications are
[`mempool_datacarrier.py`](../../../test/functional/mempool_datacarrier.py),
[`mempool_sigoplimit.py`](../../../test/functional/mempool_sigoplimit.py) and
[`mempool_subdust_fee_penalty.py`](../../../test/functional/mempool_subdust_fee_penalty.py).
The first two include block-acceptance examples. The sub-dust file specifies
admission thresholds; external paired block coverage must be added rather than
claimed from that existing test alone.

## Results, failures and coverage limits

The versioned machine-readable run report must account for every required
fixture, expected/observed admission, block/state outcome and lifecycle result,
with bounded diagnostics and durations. Startup, wrong-chain, identity,
provenance, timeout, malformed RPC, cancellation, missing fixture, skipped case
or divergent block/state results fail qualification. Preserve partial failure
reports and bounded logs without RPC credentials; clean up owned nodes on every
exit. Fix the cause and rerun affected checks, then renew complete profile
evidence before qualification. Mocks test tooling failures; they cannot replace
real paired-node evidence.

These profiles demonstrate identical serialized-block acceptance and matching
fixture chain state only for the named inputs/configurations. They do not prove
wallet or relay interoperability, exhaustive consensus equivalence, or the
absence of defects outside the finite corpus. Existing previous-release
compatibility jobs and feature tests retain their separate meanings.

CI integration should select smoke for affected high-risk changes and full for
nightly and release qualification, including any optional release rehearsal.
Evidence must identify the exact candidate SHA, Core base, configuration and
profile. If promotion reuses canonical CI, require fresh compatibility evidence
or equally strong evidence for that exact source/configuration; generic prior
CI success is insufficient. Explicit `ci:*` coverage requests still force
their requested fresh coverage. Source/configuration/profile changes invalidate
prior qualification. A missing or skipped required result fails closed.

This contract complements existing release gates: optional five-platform
rehearsal, canonical COPYING, source validation and patch replay, signed
eight-asset create-only draft handling and independent verification. Expanded
dependency notices remain deferred. Compatibility reports are CI artifacts,
not additional release assets; publication still requires separate authority.

## Proportional review examples

For a hypothetical datacarrier port, the PR names both source SHAs, the old
commit and adapted destination, its owner and changed APIs. It declares the
configured Roots admission rejection and the fixture's explicit Core admission
expectation, links smoke results comparing identical blocks/tips/UTXOs and the
invalid-block control, and supplies full evidence for release qualification.
Missing block acceptance or unverified binaries are gaps, not a passing port.

For a docs-only wording change, state the target branch, rationale and lack of
runtime impact; check links/examples and run applicable documentation/tooling
checks. Mark paired-node evidence inapplicable with that reason. Contributors
need not build nodes or manufacture runtime fixture results for such a change.
