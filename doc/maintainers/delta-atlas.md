# Delta-atlas input locks

`contrib/roots/atlas-input-lock-29.3.json` is the L2 input boundary for the
29.3 delta atlas. It is an evidence record, not a claim that an unpacked Core
snapshot has Git provenance or preserved metadata.

Regenerate it only from explicitly supplied inputs:

```bash
python3 contrib/devtools/roots-delta-atlas.py lock-inputs \
  --ledger contrib/roots/lineage-ledger.json \
  --snapshot core-29.3-snapshot=upstream/bitcoin-29.3 \
  --snapshot core-29.4-snapshot=upstream/bitcoin-29.4 \
  --output contrib/roots/atlas-input-lock-29.3.json
```

Then prove the checked-in record is still current:

```bash
python3 contrib/devtools/roots-delta-atlas.py verify-input-lock \
  contrib/roots/atlas-input-lock-29.3.json \
  --ledger contrib/roots/lineage-ledger.json \
  --snapshot core-29.3-snapshot=upstream/bitcoin-29.3 \
  --snapshot core-29.4-snapshot=upstream/bitcoin-29.4
```

The snapshot channels are raw file bytes, path type, and symlink-target bytes.
Their executable-bit and POSIX-mode state is explicitly `unavailable`, because
archive extraction or a filesystem copy can change it without changing source.
The tool omits known workspace/build metadata by construction and rejects path
escapes and case-fold collisions. It uses UTF-8 paths, `/`, `LC_ALL=C`, and no
line-ending conversion.

For mode-sensitive evidence, use `git-manifest` against an explicitly prepared
Git work tree and immutable revision. That channel reads `git ls-tree -r -z`
and raw blob contents: executable bits are authoritative there, unlike in a
snapshot. Archive input also has a separate member-order channel. Rename/copy
classification is deliberately outside this lock; it is advisory until an
upstream blob identity or commit establishes it.

Neither manifest command fetches, clones, checks out, or reads untracked
workspace content. The Knots and Roots object identities are copied from the
validated L1 lineage ledger; Core snapshots intentionally do not assert a tag,
signature, ancestry, tree object, or trusted mode.

## Layer partition

`atlas-layer-partition-29.3.json` composes raw Git-tree transitions in a fixed
order: Core 29.3 to the locked Knots parent, that parent to the first Roots
commit, then first Roots to the Roots release target. It rejects a substituted
first-parent relationship or non-ancestral anchors, reconstructs the final tree
from the ordered changes, and fails if a direct Core-to-Roots path has no layer
owner. It records all multi-layer paths rather than adding layer counts; a
later Roots change on a first-Roots path is marked as an amendment or exact
revert.

```bash
python3 contrib/devtools/roots-delta-atlas.py partition-layers \
  --repository . --ledger contrib/roots/lineage-ledger.json \
  --knots-parent 99ee26e9df0e63a5d1e0ab6bd46b1862ce67648b \
  --first-roots 07580114c35e870e242621316ec8cd051a938787 \
  --output contrib/roots/atlas-layer-partition-29.3.json
```

The partition is a raw-entry accounting boundary, not a semantic or
rename/copy claim. Hunk, symbol, target, and subsystem interpretation belongs
to later atlas stages.

## L3 handoff

`atlas-report-29.3.json` binds the input lock, partition, granular inventory,
and classification by digest. It reports direct-path coverage, risk queues, and
candidate adaptation areas; it is deliberately not an adaptation manifest.
L3 must retain these input digests, turn candidates into logical units, and
resolve every critical/high or ambiguous queue item with subsystem evidence.

```bash
python3 contrib/devtools/roots-delta-atlas.py verify-report \
  contrib/roots/atlas-report-29.3.json \
  --lock contrib/roots/atlas-input-lock-29.3.json \
  --partition contrib/roots/atlas-layer-partition-29.3.json \
  --inventory contrib/roots/atlas-granularity-29.3.json \
  --classification contrib/roots/atlas-classification-29.3.json
```

Any changed input, missing direct-path classification, or changed report bytes
is stale evidence and fails verification.
