# Logical adaptation manifest

`contrib/roots/adaptation-manifest.schema.json` defines the versioned envelope
for the Roots adaptation inventory. `contrib/devtools/roots-adaptation-manifest.py`
is the authoritative offline validator for the semantic rules that cannot be
expressed in JSON Schema alone.

An adaptation unit is a logical change, not a file list or a commit-message
label. Its stable `roots-*` ID records owner area, purpose, Core/Knots/Roots
provenance and confidence, licence evidence, original and current patch IDs,
layer, dependency/conflict edges, paths and targets, generated inputs/outputs,
risk, consensus/policy effect, supported releases, application mechanism,
tests, upstream disposition, and retirement conditions.

The manifest is anchored to the L2 atlas report digest. It must retain that
digest when L3 later turns candidate areas into complete units. This avoids a
second unbound inventory and makes an input change detectable.

Application mechanisms are `commit`, `patch`, `module/data`, `generator`, and
`manual`. Lifecycle is independent: an adaptation can be `active`, `absorbed`,
`obsolete`, or `rejected`, while preserving why it existed and the evidence for
its disposition. Terminal lifecycle entries are retained rather than silently
removed, so an upstream absorption or deliberate exclusion cannot be replayed
accidentally.

## Layer classification

`adaptation-layer-29.3.json` is the deterministic bridge from L2 path records
to L3 logical units. It retains all 718 Core-to-Knots, 40 Knots-to-first-Roots,
and 158 later-Roots records and binds their L2 report, partition, and
classification digests. The 40-path boundary is separately bound to the L1
first-fork evidence digest.

The layer record is intentionally conservative: Core-to-Knots means a selected
Knots capability is present, not that the individual hunk is proven Knots-only.
Those records say `core-or-knots-origin-unresolved` until provenance is
independently established. First-fork and later-Roots records are marked as
Roots changes. This avoids silently importing newer Knots work or presenting
an exclusion as an applied adaptation.

## Dependency ordering

Each logical unit declares a phase (`foundation`, `module`, `behavior`,
`ui-branding`, `tests`, `generated`, or `release-metadata`), dependencies,
provided capabilities, mutual conflicts, and declarative release conditions.
The validator's `application_order()` returns a stable topological order, using
phase and ID only as tie-breakers, and fails before a candidate worktree is
touched on a cycle, missing selected dependency, duplicate provider, or
mutually selected conflict.

Release-conditioned replacements use unit IDs, not procedural code. A selected
active replacement removes only the listed older unit for that release. Units
already `absorbed`, `obsolete`, or `rejected` are never selected for replay.

## Commit topology

`commit-topology.schema.json` specifies the candidate-release topology: an
annotated Bitcoin Core release tag and peeled commit, the Roots target commit
and tree, and an ordered map from every post-base commit to adaptation IDs and
original provenance. The `verify-topology` command requires the exact Core tag
as merge base, requires the first Roots commit to parent that tag directly,
rejects merges, checks complete first-parent commit coverage, rejects unknown
adaptation IDs when a manifest is supplied, and verifies the target tree.
It also requires the manifest-owned aggregate diff digest and the final
Core-base-to-target diff digest to be identical.

```bash
python3 contrib/devtools/roots-adaptation-manifest.py verify-topology \
  path/to/topology.json --repository path/to/candidate \
  --manifest path/to/adaptation-manifest.json
```

The current 29.3 Knots-derived history is provenance evidence, not an instance
of this future topology. A candidate must therefore be rooted at the selected
Core tag; it must not merge an older Roots or Knots trunk.

## Delta coverage

`adaptation-coverage-29.3.json` accounts for every one of the 839 direct
Core-to-Roots raw-entry deltas. Each row preserves its before/after raw tree
entry, blob identity, mode/type information, one primary coverage owner, and
explicit co-owners where paths span layers. Its scoped target digest prevents a
deleted, renamed, mode-only, symlink, binary, or overlapping record from
quietly falling outside the report.

```bash
python3 contrib/devtools/roots-adaptation-manifest.py verify-coverage \
  contrib/roots/adaptation-coverage-29.3.json \
  --partition contrib/roots/atlas-layer-partition-29.3.json \
  --classification contrib/roots/atlas-classification-29.3.json
```

Coverage-owner IDs are deterministic accounting boundaries that the completed
logical manifest must refine into auditable adaptation units. They are not a
licence to combine unrelated risk into a replay commit.

Regenerate it only from accepted L1/L2 evidence:

```bash
python3 contrib/devtools/roots-adaptation-manifest.py classify-layers \
  --ledger contrib/roots/lineage-ledger.json \
  --partition contrib/roots/atlas-layer-partition-29.3.json \
  --report contrib/roots/atlas-report-29.3.json \
  --classification contrib/roots/atlas-classification-29.3.json \
  --output contrib/roots/adaptation-layer-29.3.json
```

Validate a manifest without network or Git access:

```bash
python3 contrib/devtools/roots-adaptation-manifest.py path/to/manifest.json
```

The validator rejects duplicate keys, oversized/deep inputs, unstable or
duplicate identifiers, unclassified risk, missing provenance or licence
evidence, unsafe extension fields, unknown references, dependency cycles, and
an item that is both a conflict and a dependency. L3.2 supplies the actual
layered adaptation inventory; this L3.1 contract deliberately does not claim
that a representative fixture describes the full Roots delta.
