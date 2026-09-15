# Roots portability roadmap

This roadmap is a design and evidence record. It does not authorize a runtime
change, a consensus change, or a refactor merely to reduce a downstream diff.
The maintained hotspot record is
[`contrib/roots/portability-hotspots-29.4.json`](../../contrib/roots/portability-hotspots-29.4.json).

## How the first queue is measured

The 29.3 inventory, accepted 29.4 replay outcomes, manual-resolution proposal,
and retrospective are the sources. The sample has two clean replay attempts and
one Core 29.3-to-29.4 migration window: it can count recorded manual conflict
paths and changed-path surface, but it cannot honestly estimate elapsed
resolution time or multi-release upstream churn. Those values are therefore
recorded as `not-recorded`, not invented as a score.

The queue gives priority to a repeated manual boundary with a cohesive Roots
owner. It explicitly excludes line count, a clean textual merge, and moving
consensus code as positive signals. Rename, squash, and path movement are
resolved through stable adaptation IDs and the replay outcome's declared versus
changed path counts, rather than commit names or raw line counts.

Every queued adaptation records all seven dimensions explicitly: manual conflict
frequency, declared/changed/absorbed diff surface, the bounded upstream-churn
observation, build impact, resolution time, risk, and cohesion/ownership
stability. The deterministic identity fixture exercises rename, squash, and path
movement without letting a path or commit label replace the adaptation ID.

| Rank | Adaptation ID | Recorded signal | Next design question |
| --- | --- | --- | --- |
| 1 | `roots-roots-build-ci-build-or-release` | Three manual CI paths; 48 changed paths | Can Roots CI identity/cache restrictions become a narrow, data-owned seam while preserving Core runner semantics? |
| 2 | `roots-knots-p2p-networking` | `src/node/miner.h` and an absorbed `txrequest` path; 41 changed paths | Can the remaining P2P/node declaration stay explicitly separate from local policy ownership? |
| 3 | `roots-roots-common-maintenance` | One `src/clientversion.cpp` manual path; 18 changed paths | Does a future behavior-specific proposal justify a boundary, rather than treating maintenance as one module? |
| 4 | `roots-knots-policy-local-policy` | One `src/txmempool.h` manual path; 16 changed paths | Can local policy be characterized without any block-validity dependency? |

All four entries are the complete, manifest-owned set of actual manual paths in
the accepted 29.4 evidence. The GUI presentation concern remains a separately
measured future design candidate, not a ranked manual hotspot. Later roadmap
stages must add a contract, migration order, test gates, performance/resource
gates, rollback, and a leave-inline alternative before implementation is
considered.

## Deliberate leave-inline decisions

Consensus-adjacent units remain inline because no conflict reduction justifies
compatibility risk. The P2P `txrequest` boundary was absorbed by Core, while
the separately owned `src/node/miner.h` boundary remains inline pending a P2P
contract. Wallet/database work remains inline because it is
cross-cutting with no recorded manual hotspot. The 275-path common-maintenance
unit is an accounting hotspot, not proof of a cohesive module. Generated release
and documentation output, including archive identity, stays derived from the
candidate rather than hidden in a runtime abstraction. The complete mapping and
reasons are machine-checked in the hotspot record.

The acceptance gate for every future proposal remains explicit: local policy
must not affect block acceptance, and RDTS/BIP110 consensus enforcement must
remain absent.

## Cohesive seam decisions

The machine-readable design contracts are in
[`contrib/roots/portability-seams-29.4.json`](../../contrib/roots/portability-seams-29.4.json).
They deliberately reject runtime plugins, macro forests, universal fork
abstractions, and consensus movement for diff reduction.

The CI candidate is a data-owned identity/cache-policy input only, owned by
`roots-roots-build-ci-build-or-release`: it does not
choose runners, run code, or grant credentials. It is expected to concentrate
the three recorded manual CI paths while preserving Core runner topology and
read-only cache boundaries. If that boundary cannot remain narrow, reviewed
literals stay inline.

The local-policy candidate is explicitly left inline. Its one manual
`src/txmempool.h` boundary and the separately owned P2P `src/node/miner.h`
boundary warrant characterization, not a shared abstraction: any later policy
interface must remain policy-only, outside consensus and block acceptance, with
RDTS/BIP110 enforcement still absent. Its gates cover local rejection versus
block acceptance, startup/help/config compatibility, and mempool resource
behavior.

The GUI candidate is an immutable Qt-only presentation descriptor for identity,
resource names, and deterministic package inputs. It may not be consumed by
node, wallet, kernel, consensus, policy, or util. Its staged migration is gated
by startup/resource, translation, and package-manifest characterization; it is
rolled back to Qt literals if it crosses that boundary or becomes a universal
fork abstraction.

## Upstream dispositions

[`contrib/roots/upstream-dispositions-29.4.json`](../../contrib/roots/upstream-dispositions-29.4.json)
is a deterministic public route record, not an upstream submission claim. It
covers generic fixes, Knots-origin features, Roots defaults, test improvements,
and build fixes. A public link is either a
real submitted link or explicitly `not-submitted`/`not-applicable`; no link is
invented. Only reviewed acceptance evidence can change a local adaptation to
`absorbed`.

Roots policy, identity, and defaults are explicitly Roots-only. In particular,
local policy remains outside block validity and does not imply an upstream
consensus requirement; RDTS/BIP110 enforcement remains absent. Embargoed
coordination is not a public record at all: it exposes no adaptation, paths,
reproducers, or private coordination links. After authorized disclosure it
receives a separate reviewed public route.

Lifecycle is structurally constrained to `active → submitted → absorbed`.
`active` upstreamable records use `not-submitted`; Roots-only policy/default
records use `not-applicable`. An `absorbed` record must carry a
real submitted public link and reviewed absorption evidence naming both that
change and the local adaptation ID. The isolated test fixture uses a synthetic
URL solely to falsify this contract; it is not a claimed upstream submission.

## Delta budget

[`contrib/roots/portability-delta-budget-29.4.json`](../../contrib/roots/portability-delta-budget-29.4.json)
records a 29.3-to-29.4 baseline of 16 logical units, 839 touched upstream
paths, six manual outcomes, six generated outcomes, and all 15 critical
invariants passing. It records longitudinal conflict rate as `not-recorded`;
one migration is not a trend. Its actionable triggers cover new manual hotspots,
unexplained generated churn, policy/consensus boundary changes, and scattering.
Split/squashed patches, moves, generated output, and absorbed units retain their
stable adaptation IDs. The record is a review budget, never a line-count target.
The window manual rate is explicitly 6/40 (1,500 basis points) across four
manifest-owned adaptation units; it is a current-window observation, not a
forecast. The ranked CI, P2P, maintenance, and policy hotspots are sourced from
the path-to-unit queue, while the seam record keeps every multi-unit concern's
surface separate. Synthetic normalization cases execute split, squash, move,
generated, absorption, and scattering behavior rather than relying on labels.
