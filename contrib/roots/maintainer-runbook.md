# Roots upstream maintenance runbook

This is a fail-closed procedure for reconstructing the accepted 29.3 and 29.4
lineages and preparing a later Bitcoin Core candidate. It never pushes,
publishes, signs release artifacts, or creates/moves a release tag. Run it only
in fresh clones and disposable directories. The future release base is always a
verified Bitcoin Core tag: never a Bitcoin Knots branch and never a previous Roots trunk.

Commands below assume the repository containing this runbook is a clean clone
with the locked Git objects and the validation ref used by the versioned 29.4
fixture. Use absolute paths so replay state cannot escape the disposable root:

```bash
export LC_ALL=C TZ=UTC
ROOTS_SOURCE="$(pwd -P)"
WORK="$(mktemp -d)"
SOURCE="$WORK/source"
REPLAY="$ROOTS_SOURCE/contrib/devtools/roots-replay.py"
git clone --no-local "$ROOTS_SOURCE" "$SOURCE"
SOURCE_HEAD="$(git -C "$SOURCE" rev-parse HEAD)"
git -C "$SOURCE" fetch --no-tags "$ROOTS_SOURCE" \
  refs/remotes/origin/ci/l7-validation/39a5e302:refs/remotes/origin/ci/l7-validation/39a5e302
git -C "$SOURCE" for-each-ref --format='%(refname) %(objectname)' refs/tags >"$WORK/tags.before"
```

Stop if the source is dirty, the validation ref is unavailable, or `WORK`
already contains output from another run. Do not substitute an object merely
because its tree looks similar.

## Verify all locked inputs

Validate methodology digests and the exact Core objects before creating replay
state:

```bash
python3 "$ROOTS_SOURCE/contrib/devtools/roots-methodology.py" \
  "$ROOTS_SOURCE/contrib/roots/methodology-v1.json"
python3 "$REPLAY" verify --repository "$SOURCE" \
  --revision 99003bed87333f1be51bf3070235591b3a72f007 \
  --expected-tree d7910bd5e9335128932f1f848a767d773895c4a4
python3 "$REPLAY" verify --repository "$SOURCE" \
  --revision 3fc0865963a38b871e9f7d94e6151c4953563516 \
  --expected-tree 38ad59b187f59647eb90ad1347bc481485ef4d01
```

For a fetched upstream tag, additionally run `git verify-tag <tag>`, record the
signer, then compare `git rev-parse <tag>^{commit}` and `<tag>^{tree}` with the
reviewed lock. Stop on a signature failure, unsupported object format, dirty
tracked state, graft/alternate/replace object, unknown methodology version, or
any commit/tree mismatch.

## Reconstruct Core 29.3 to Knots provenance and Roots 29.3

Stage A reconstructs the selected Knots provenance from the exact Core 29.3
commit. It does not use Knots as a future release base:

```bash
mkdir "$WORK/core-knots-state" "$WORK/core-knots-review"
python3 "$REPLAY" replay --repository "$SOURCE" \
  --revision 99003bed87333f1be51bf3070235591b3a72f007 \
  --expected-tree d7910bd5e9335128932f1f848a767d773895c4a4 \
  --manifest "$ROOTS_SOURCE/contrib/roots/replay-core-to-knots-29.3/adaptation-manifest-29.3.json" \
  --materials "$ROOTS_SOURCE/contrib/roots/replay-core-to-knots-29.3/replay-materials.json" \
  --materials-root "$ROOTS_SOURCE/contrib/roots/replay-core-to-knots-29.3" \
  --state-directory "$WORK/core-knots-state" --apply
test "$(git -C "$WORK/core-knots-state/owned-candidate" write-tree)" = \
  56f97d3a9199c1fb191e1b8a21f2caae3901b7f6
python3 "$REPLAY" report \
  --state "$WORK/core-knots-state/replay-state-0010.json" \
  --output-directory "$WORK/core-knots-review"
python3 "$REPLAY" export-patches \
  --repository "$WORK/core-knots-state/owned-candidate" \
  --state "$WORK/core-knots-state/replay-state-0010.json" \
  --output "$WORK/core-knots-review/replay-generated-series.patch"
```

Stage B begins from the locked Knots lineage object only after its tree equals
the Stage-A result, then applies the reviewed Roots-only series:

```bash
test "$(git -C "$SOURCE" rev-parse 99ee26e9df0e63a5d1e0ab6bd46b1862ce67648b^{tree})" = \
  56f97d3a9199c1fb191e1b8a21f2caae3901b7f6
mkdir "$WORK/roots-29.3-state" "$WORK/roots-29.3-review"
python3 "$REPLAY" replay --repository "$SOURCE" \
  --revision 99ee26e9df0e63a5d1e0ab6bd46b1862ce67648b \
  --expected-tree 56f97d3a9199c1fb191e1b8a21f2caae3901b7f6 \
  --manifest "$ROOTS_SOURCE/contrib/roots/replay-29.3-release/adaptation-manifest-29.3.json" \
  --materials "$ROOTS_SOURCE/contrib/roots/replay-29.3-release/replay-materials.json" \
  --materials-root "$ROOTS_SOURCE/contrib/roots/replay-29.3-release" \
  --state-directory "$WORK/roots-29.3-state" --apply
test "$(git -C "$WORK/roots-29.3-state/owned-candidate" write-tree)" = \
  a5708dcbf1d2611360fab68fc6a8e504db1ba95d
python3 "$REPLAY" report \
  --state "$WORK/roots-29.3-state/replay-state-0016.json" \
  --output-directory "$WORK/roots-29.3-review"
python3 "$REPLAY" export-patches \
  --repository "$WORK/roots-29.3-state/owned-candidate" \
  --state "$WORK/roots-29.3-state/replay-state-0016.json" \
  --output "$WORK/roots-29.3-review/replay-generated-series.patch"
jq '.reconstructions["core-to-knots-29.3"], .reconstructions["roots-29.3"]' \
  "$ROOTS_SOURCE/contrib/roots/methodology-v1.json"
sha256sum "$WORK"/*-review/replay-review.json \
  "$WORK"/*-review/replay-review.txt \
  "$WORK"/*-review/replay-generated-series.patch
```

The trees and every printed artifact digest must equal the corresponding
`methodology-v1.json` reconstruction. Stage A must report ten units. Stage B
must report sixteen units: seven `applied` and nine reviewed `manual`
boundaries. A missing Knots counterpart is recorded as `defer`; it never
authorizes a substitute base.

## Construct and compare the canonical 29.4 lineage

Run both constructors into new paths:

```bash
bash "$ROOTS_SOURCE/contrib/roots/replay-29.4-proposal/canonical-lineage.bash" \
  "$SOURCE" "$WORK/canonical-29.4" | tee "$WORK/canonical-29.4.txt"
bash "$ROOTS_SOURCE/contrib/roots/replay-29.4-proposal/incremental-oracle.bash" \
  "$SOURCE" "$WORK/incremental-29.4" | tee "$WORK/incremental-29.4.txt"
grep -Fx 'base_commit=3fc0865963a38b871e9f7d94e6151c4953563516' "$WORK/canonical-29.4.txt"
grep -Fx 'target_commit=cbc88cff9b35b95a549c0313e424e13093fcd6a1' "$WORK/canonical-29.4.txt"
grep -Fx 'target_tree=39a5e30207a09962e78ae81c24cc65b1e478ef90' "$WORK/canonical-29.4.txt"
grep -Fx 'conflict_count=12' "$WORK/incremental-29.4.txt"
grep -Fx 'oracle_tree=c676e8944470cc74fcc213e7368aed359ad8ae55' "$WORK/incremental-29.4.txt"
grep -Fx 'oracle_commit=3e29908f7a0131a71309e80a78fe865ec8a50f76' "$WORK/incremental-29.4.txt"
jq -e '.accepted_lineage == false and .fresh_candidate.tree == "sha1:39a5e30207a09962e78ae81c24cc65b1e478ef90"' \
  "$ROOTS_SOURCE/contrib/roots/replay-29.4-proposal/incremental-comparison.json"
jq -e '.status == "accepted" and .candidate.tree == "sha1:39a5e30207a09962e78ae81c24cc65b1e478ef90"' \
  "$ROOTS_SOURCE/contrib/roots/replay-29.4-proposal/acceptance-evidence.json"
```

The incremental oracle is diagnostic and must remain rejected. Equal trees
alone are never sufficient; compare its conflict set, report digests,
adaptation ownership, invariants, and approval with the fresh candidate.

Lock the trusted replay decision:

```bash
mkdir "$WORK/trusted"
python3 "$ROOTS_SOURCE/ci/roots-trusted-replay-gate.py" \
  --mode manual --changed true --manual-run force --later-base none \
  --manifest "$ROOTS_SOURCE/contrib/roots/adaptation-manifest-29.3.json" \
  --methodology "$ROOTS_SOURCE/contrib/roots/methodology-v1.json" \
  --fixture "$ROOTS_SOURCE/contrib/roots/core-29.4-migration-fixture.json" \
  --output "$WORK/trusted/trusted-replay-report.json"
jq -e '.decision == "replay" and .locks["v29.4"].tree == "38ad59b187f59647eb90ad1347bc481485ef4d01"' \
  "$WORK/trusted/trusted-replay-report.json"
```

## Stage 29.4 and prepare a later-Core candidate

Stage only the exact accepted candidate on a disposable integration branch:

```bash
git clone --no-checkout "$SOURCE" "$WORK/staging-29.4"
git -C "$WORK/staging-29.4" switch --create roots/integration-v29.4 \
  3fc0865963a38b871e9f7d94e6151c4953563516
git -C "$WORK/staging-29.4" fetch --no-tags "$WORK/canonical-29.4" \
  refs/heads/roots-29.4-canonical:refs/roots/candidate-29.4
python3 "$REPLAY" stage-candidate --repository "$WORK/staging-29.4" \
  --integration-branch roots/integration-v29.4 \
  --expected-head 3fc0865963a38b871e9f7d94e6151c4953563516 \
  --candidate-revision cbc88cff9b35b95a549c0313e424e13093fcd6a1 \
  --expected-candidate-tree 39a5e30207a09962e78ae81c24cc65b1e478ef90 \
  --confirm I_STAGE_THE_EXACT_CANDIDATE
```

For a subsequent Core release, replace these variables only with reviewed,
verified values:

```bash
LATER_TAG=v30.0
git -C "$SOURCE" fetch --tags --force origin
git -C "$SOURCE" verify-tag "$LATER_TAG"
LATER_COMMIT="$(git -C "$SOURCE" rev-parse "$LATER_TAG^{commit}")"
LATER_TREE="$(git -C "$SOURCE" rev-parse "$LATER_TAG^{tree}")"
git -C "$SOURCE" switch --create "roots/integration-$LATER_TAG" "$LATER_COMMIT"
mkdir "$WORK/later-plan"
python3 "$REPLAY" verify --repository "$SOURCE" \
  --revision "$LATER_COMMIT" --expected-tree "$LATER_TREE"
python3 "$REPLAY" plan --repository "$SOURCE" \
  --revision "$LATER_COMMIT" --expected-tree "$LATER_TREE" \
  --output "$WORK/later-plan/replay-plan.json"
```

The current trusted gate deliberately allowlists no post-29.4 tag. Running it
with `--later-base "$LATER_TAG"` must stop with `later base is not allowlisted`.
Do not replay until a reviewed change adds the tag's commit/tree lock, migration
fixture, materials, outcome map, and tests. Once those versioned inputs exist,
run the explicit replay form below—never reuse the 29.4 materials:

```bash
mkdir "$WORK/later-state"
python3 "$REPLAY" replay --repository "$SOURCE" \
  --revision "$LATER_COMMIT" --expected-tree "$LATER_TREE" \
  --manifest "$LATER_MATERIALS/adaptation-manifest.json" \
  --materials "$LATER_MATERIALS/replay-materials.json" \
  --materials-root "$LATER_MATERIALS" \
  --state-directory "$WORK/later-state" --apply
```

Record every result as `accept`, `rewrite`, `reject`, `defer`, or `upstream`.
After committing only the reviewed candidate and accounting data, validate all
changed hunks with full immutable commit IDs:

```bash
BASE_COMMIT="$(git -C "$SOURCE" rev-parse "$LATER_COMMIT")"
CANDIDATE_COMMIT="$(git -C "$SOURCE" rev-parse HEAD)"
python3 "$ROOTS_SOURCE/contrib/devtools/roots-continuous-accounting.py" \
  --repository "$SOURCE" --base "$BASE_COMMIT" --head "$CANDIDATE_COMMIT" \
  --record "$SOURCE/contrib/roots/continuous-accounting-pr.json" --public-release
```

Stop on an unaccounted hunk, stale digest, unknown owner, unresolved embargo,
failed invariant, missing approval, or any consensus/policy-boundary doubt.

## Release evidence and preparation only

After candidate review, generate and verify provenance evidence from the exact
candidate commit, then prepare checksums. These commands do not sign, tag, or
publish. Set `CANDIDATE_SOURCE` to the clean reviewed repository containing the
accepted replay, accounting, and release-evidence inputs; for a later release
this is the completed integration branch, not the untouched input clone:

```bash
CANDIDATE_SOURCE=/absolute/path/to/reviewed-candidate
SOURCE_REVISION="$(git -C "$CANDIDATE_SOURCE" rev-parse HEAD)"
SOURCE_TREE="$(git -C "$CANDIDATE_SOURCE" rev-parse "$SOURCE_REVISION^{tree}")"
mkdir "$WORK/release"
python3 "$ROOTS_SOURCE/ci/release/roots-release-evidence.py" \
  --ledger "$CANDIDATE_SOURCE/contrib/roots/lineage-ledger.json" \
  --manifest "$CANDIDATE_SOURCE/contrib/roots/adaptation-manifest-29.3.json" \
  --replay-result "$CANDIDATE_SOURCE/contrib/roots/replay-29.4-proposal/acceptance-evidence.json" \
  --source-repository "$CANDIDATE_SOURCE" --source-revision "$SOURCE_REVISION" \
  --candidate-tree "sha1:$SOURCE_TREE" \
  --output "$WORK/release/roots-release-evidence.json"
python3 "$ROOTS_SOURCE/ci/release/roots-release-evidence.py" \
  --ledger "$CANDIDATE_SOURCE/contrib/roots/lineage-ledger.json" \
  --manifest "$CANDIDATE_SOURCE/contrib/roots/adaptation-manifest-29.3.json" \
  --replay-result "$CANDIDATE_SOURCE/contrib/roots/replay-29.4-proposal/acceptance-evidence.json" \
  --source-repository "$CANDIDATE_SOURCE" --source-revision "$SOURCE_REVISION" \
  --candidate-tree "sha1:$SOURCE_TREE" \
  --output "$WORK/release/roots-release-evidence.json" --verify
bash "$ROOTS_SOURCE/ci/release/prepare-release.sh" \
  "$DOWNLOAD_DIR" "$WORK/prepared" \
  "$ROOTS_SOURCE/contrib/release/bitcoin-roots-release-key.asc" \
  "$RELEASE_NAME" "$EXPECTED_PACKAGE_COUNT" \
  "$WORK/release/roots-release-evidence.json" "$CANDIDATE_SOURCE" "$SOURCE_REVISION"
(cd "$WORK/prepared" && sha512sum --check SHA512SUMS)
```

Stop if evidence inputs are not committed at `SOURCE_REVISION`, the candidate
tree differs, an archive root/name/count is wrong, SHA512 verification fails,
or the output directory is nonempty. A human reviewer separately authorizes
any signature, tag, or publication.

## Resume, abandon, and prove tag safety

Inspect and resume an interrupted replay only with the same source lock,
manifest, materials, state directory, and candidate tree:

```bash
python3 "$REPLAY" inspect --state "$STATE_FILE"
python3 "$REPLAY" resume --repository "$SOURCE" \
  --revision "$BASE_COMMIT" --expected-tree "$BASE_TREE" \
  --manifest "$MANIFEST" --materials "$MATERIALS" \
  --materials-root "$MATERIALS_ROOT" --state "$STATE_FILE" \
  --state-directory "$STATE_DIRECTORY"
```

On drift, preserve the state and stop. To abandon, write a deterministic marker
without deleting evidence:

```bash
python3 "$REPLAY" abandon --state "$STATE_FILE" \
  --output "$STATE_DIRECTORY/replay-abandoned.json"
git -C "$SOURCE" for-each-ref --format='%(refname) %(objectname)' refs/tags >"$WORK/tags.after"
cmp "$WORK/tags.before" "$WORK/tags.after"
```

If tag state differs, the rehearsal or release preparation is rejected. Remove
only the explicitly named disposable `WORK` after retaining required reports.
