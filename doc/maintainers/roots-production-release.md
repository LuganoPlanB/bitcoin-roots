# Bitcoin Roots production release runbook

This runbook is the operational procedure for moving a reviewed Bitcoin Core
release through the Roots replay, production, and publication boundaries. It
does not itself authorize a push, tag, signature, or release. The checked-in
records, exact Git objects, independent review, and green CI must all agree
before a maintainer crosses the corresponding boundary.

Bitcoin Roots remains compatible with Bitcoin Core consensus. Conservative
transaction relay and mempool rules are local, configurable policy; they must
never become block-validity rules. Roots does not enforce RDTS/BIP110 consensus
rules. Stop on any evidence that changes those invariants.

For the replay tool's detailed guarantees, see the [offline replay engine](replay-engine.md).
For the machine-readable topology, see the [Roots 29.4 promotion contract](roots-promotion-contract.md).

## The two history planes

Keep these histories separate:

- The **control plane** contains trusted validators, workflow definitions,
  manifests, accounting, documentation, and promotion automation. For 29.4 it
  is `refs/heads/codex/roots-29-4-release`, rooted in the previous Roots trunk.
- The **production line** starts directly at the verified Bitcoin Core tag,
  adds the canonical Roots adaptation commits, then adds separately accounted
  production units. The review and production refs are
  `refs/heads/integration/roots-29.4` and `refs/heads/roots/29.4`.

The control checkout may execute trusted validators, but it is not release
source. Candidate code is data supplied to those validators. Conversely, the
production line must not acquire the old Roots trunk as an ancestor.

Bitcoin Core is an external, disposable fetch input. Fetching its annotated tag
into a fresh repository preserves the authenticated object and ordinary Git
ancestry without placing an upstream copy in Roots. Never add a Core snapshot,
submodule, subtree, `upstream/` directory, or other vendor copy. Such copies
lose or duplicate the review boundary, can drift independently, and cannot
substitute for a verified tag object. Never merge a Bitcoin Knots branch or a
previous Roots trunk to construct a future release.

## Non-negotiable stops

Never continue when an expected full object ID, tree, signature, digest, ref,
review, CI result, workflow input, key fingerprint, or authorization bit
differs. In particular:

- never enable trusted Git `rerere`, use replacement objects, grafts,
  alternates, hooks, implicit fetches, or caller Git configuration;
- never force-push, delete and recreate a release ref, move a tag, or replace
  an annotated tag with a lightweight tag;
- never publish a remote branch before the reviewed promotion phase;
- never create a release tag, draft, signature, or public release during
  reconstruction or qualification;
- never make signing credentials available to replay, candidate build, or
  candidate-controlled workflow code; and
- never weaken a validator or test merely to accept the current candidate.

Use new paths for every replay and build. Preserve failed state and logs until
the failure is understood. Run experiments only against disposable
repositories and explicit regtest data directories.

## Reusable release variables

Fill these from a reviewed release record. Use full object IDs; branch or tag
names are never substitutes for the locked values.

```bash
set -Eeuo pipefail
umask 077
export LC_ALL=C LANG=C TZ=UTC
export GIT_CONFIG_NOSYSTEM=1 GIT_CONFIG_GLOBAL=/dev/null
export GIT_NO_REPLACE_OBJECTS=1 GIT_OPTIONAL_LOCKS=0

ROOTS_CONTROL=/absolute/path/to/trusted-control-checkout
WORK="$(mktemp -d)"
OBJECT_REPOSITORY="$WORK/objects"
PRODUCTION_REPOSITORY=/absolute/path/to/fresh-production-checkout
PROMOTION_REPOSITORY=/absolute/path/to/fresh-promotion-checkout
BUILD_EVIDENCE=/absolute/path/to/roots-release-build-evidence.json
CORE_REMOTE=https://github.com/bitcoin/bitcoin.git
CORE_TAG=vX.Y
CORE_TAG_OBJECT=0000000000000000000000000000000000000000
CORE_COMMIT=0000000000000000000000000000000000000000
CORE_TREE=0000000000000000000000000000000000000000
CANDIDATE_TREE=0000000000000000000000000000000000000000
CANONICAL_COMMIT=0000000000000000000000000000000000000000
FROZEN_COMMIT=0000000000000000000000000000000000000000
FROZEN_TREE=0000000000000000000000000000000000000000
INTEGRATION_REF=refs/heads/integration/roots-X.Y
PRODUCTION_REF=refs/heads/roots/X.Y
RELEASE_TAG=vX.Y-roots.N
INITIAL_PRODUCTION_REF_ACTION=verify-existing
```

Leave `INITIAL_PRODUCTION_REF_ACTION` at `verify-existing` unless the reviewed
release plan explicitly authorizes initial creation of an absent production
ref. Set it to `create-if-absent` only for that one boundary; it is not a
general overwrite or repair switch.

Do not reuse `WORK`. Record the Git, Python, CMake, compiler, ccache, and
platform versions in qualification evidence. Do not install a missing
dependency during a release run; stop and repair the controlled environment.
Every local C or C++ configure must explicitly set both
`CMAKE_C_COMPILER_LAUNCHER=ccache` and
`CMAKE_CXX_COMPILER_LAUNCHER=ccache`, verify both generated cache entries, and
record `ccache -s` after compilation.

## 1. Verify the Core tag in a disposable repository

Fetch only the exact annotated tag. Do not use `--tags`, a branch name, a
GitHub-generated source archive, or an already populated maintainer checkout.

```bash
git init "$OBJECT_REPOSITORY"
git -C "$OBJECT_REPOSITORY" -c rerere.enabled=false \
  fetch --no-tags "$CORE_REMOTE" \
  "refs/tags/$CORE_TAG:refs/tags/$CORE_TAG"

test "$(git -C "$OBJECT_REPOSITORY" cat-file -t "refs/tags/$CORE_TAG")" = tag
git -C "$OBJECT_REPOSITORY" verify-tag "refs/tags/$CORE_TAG"
test "$(git -C "$OBJECT_REPOSITORY" rev-parse "refs/tags/$CORE_TAG")" = \
  "$CORE_TAG_OBJECT"
test "$(git -C "$OBJECT_REPOSITORY" rev-parse "refs/tags/$CORE_TAG^{commit}")" = \
  "$CORE_COMMIT"
test "$(git -C "$OBJECT_REPOSITORY" rev-parse "refs/tags/$CORE_TAG^{tree}")" = \
  "$CORE_TREE"
test "$(git -C "$OBJECT_REPOSITORY" rev-parse --is-shallow-repository)" = false
test ! -e "$OBJECT_REPOSITORY/.git/objects/info/alternates"
test ! -e "$OBJECT_REPOSITORY/.git/info/grafts"
test -z "$(git -C "$OBJECT_REPOSITORY" for-each-ref refs/replace)"
test ! -d "$OBJECT_REPOSITORY/.git/rr-cache"
```

Record the tag signer and verification result. A correct peeled commit with a
wrong tag object is a failure. Transfer later Roots objects into this repository
only by full object ID or an exact reviewed bundle; do not add a mutable Roots
remote.

## 2. Reproduce the Roots replay twice

The 29.4 replay consumes the locked Core commit/tree, adaptation manifest, and
materials. The following is the reusable command shape; substitute the
versioned material directory for a future cycle.

```bash
REPLAY="$ROOTS_CONTROL/contrib/devtools/roots-replay.py"
MATERIAL_ROOT="$ROOTS_CONTROL/contrib/roots/replay-X.Y-proposal"
MANIFEST="$MATERIAL_ROOT/adaptation-manifest-29.3.json"
MATERIALS="$MATERIAL_ROOT/replay-materials.json"

for run in a b; do
  state="$WORK/replay-$run-state"
  review="$WORK/replay-$run-review"
  mkdir "$state" "$review"
  python3 "$REPLAY" replay \
    --repository "$OBJECT_REPOSITORY" \
    --revision "$CORE_COMMIT" --expected-tree "$CORE_TREE" \
    --manifest "$MANIFEST" --materials "$MATERIALS" \
    --materials-root "$MATERIAL_ROOT" \
    --state-directory "$state" --apply
  state_file="$(find "$state" -maxdepth 1 -name 'replay-state-*.json' \
    -type f | sort | tail -n 1)"
  python3 "$REPLAY" report --state "$state_file" \
    --output-directory "$review"
  python3 "$REPLAY" export-patches \
    --repository "$state/owned-candidate" --state "$state_file" \
    --output "$review/replay-generated-series.patch"
  test "$(git -C "$state/owned-candidate" write-tree)" = "$CANDIDATE_TREE"
done

cmp "$WORK/replay-a-review/replay-review.json" \
  "$WORK/replay-b-review/replay-review.json"
cmp "$WORK/replay-a-review/replay-review.txt" \
  "$WORK/replay-b-review/replay-review.txt"
cmp "$WORK/replay-a-review/replay-generated-series.patch" \
  "$WORK/replay-b-review/replay-generated-series.patch"
```

The manifest, materials, state boundaries, exact resulting tree, and exported
patch series are authoritative replay products. A clean application is not an
acceptance decision; review every unit and all semantic-safety evidence.

`git range-diff` is an advisory human correlation aid, especially when comparing
the old Roots stack with a Core-rooted reconstruction. It is never an input to
replay, never proves tree equality, and never overrides a manifest, patch,
commit, or outcome decision. Record its exact ranges and Git version. A changed
range-diff digest requires review but does not authorize an automatic rewrite.

## 3. Materialize the canonical Core-rooted commits

Transfer the reviewed canonical commit object into the verified object
repository by its full ID, then run the production constructor into a new
destination:

```bash
git -C "$OBJECT_REPOSITORY" -c rerere.enabled=false \
  fetch --no-tags "$ROOTS_CONTROL" "$CANONICAL_COMMIT"
CANONICAL_REPOSITORY="$WORK/canonical"
bash "$ROOTS_CONTROL/contrib/roots/construct-canonical-29.4.bash" \
  "$OBJECT_REPOSITORY" "$CANONICAL_REPOSITORY" |
  tee "$WORK/canonical-result.txt"

grep -Fx "base_commit=$CORE_COMMIT" "$WORK/canonical-result.txt"
grep -Fx "target_commit=$CANONICAL_COMMIT" "$WORK/canonical-result.txt"
grep -Fx "target_tree=$CANDIDATE_TREE" "$WORK/canonical-result.txt"
test "$(git -C "$CANONICAL_REPOSITORY" rev-parse \
  refs/roots/private/canonical-29.4^{commit})" = "$CANONICAL_COMMIT"
test "$(git -C "$CANONICAL_REPOSITORY" rev-list --first-parent --count \
  "$CORE_COMMIT..$CANONICAL_COMMIT")" = 16
```

Require a direct Core parent for the first adaptation, exactly the declared
non-merge commit count, manifest order, expected per-commit trees, and the final
candidate tree. A matching final tree does not excuse a merge, reordered unit,
empty commit, changed metadata, or missing provenance.

## 4. Account and apply later production units

Infrastructure added after the accepted candidate is a separate layer. Never
blind-cherry-pick the old trunk. Inventory its immutable commits and atoms,
classify each disposition, and replay only the reviewed paths:

```bash
python3 "$ROOTS_CONTROL/contrib/devtools/roots-post-candidate-inventory.py" \
  verify --repository "$ROOTS_CONTROL" \
  --output "$ROOTS_CONTROL/contrib/roots/post-candidate-inventory-29.4.json"

python3 "$ROOTS_CONTROL/contrib/devtools/roots-post-candidate-replay.py" \
  verify --repository "$ROOTS_CONTROL" --root "$ROOTS_CONTROL" \
  --inventory "$ROOTS_CONTROL/contrib/roots/post-candidate-inventory-29.4.json" \
  --manifest "$ROOTS_CONTROL/contrib/roots/post-candidate-replay-29.4.json"

python3 "$ROOTS_CONTROL/contrib/devtools/roots-post-candidate-invariants.py" \
  verify --repository "$ROOTS_CONTROL" \
  --manifest "$ROOTS_CONTROL/contrib/roots/post-candidate-replay-29.4.json" \
  --output "$ROOTS_CONTROL/contrib/roots/post-candidate-invariants-29.4.json"

python3 "$ROOTS_CONTROL/contrib/devtools/roots-freeze-production.py" \
  verify --repository "$ROOTS_CONTROL" --root "$ROOTS_CONTROL" \
  --output "$ROOTS_CONTROL/contrib/roots/frozen-production-29.4.json"
```

Run the post-candidate replay twice in fresh repositories and compare its JSON
result and tree. Only after review may `roots-freeze-production.py anchor`
create its new, owned private ref. It refuses overwrite; never delete or move an
existing private freeze to make a rerun pass.

Production accounting is a separate final gate. It binds the canonical base,
every later production commit and atom, the frozen source tree, and platform
build evidence:

```bash
python3 "$ROOTS_CONTROL/contrib/devtools/roots-production-accounting.py" \
  --record "$ROOTS_CONTROL/contrib/roots/production-accounting-29.4.json" \
  --repository "$PRODUCTION_REPOSITORY" \
  --build-evidence "$BUILD_EVIDENCE"
```

Add `--public-release` only after the record's authorization is true. A
pre-release validation without that flag does not authorize publication.

## 5. Validate with trusted control-plane code

Treat the candidate repository and its workflows as untrusted data. Import the
exact candidate commit or bundle into a clean validation repository, then run
the validator from the reviewed control plane:

```bash
env -i PATH="$PATH" LC_ALL=C LANG=C TZ=UTC \
  GIT_CONFIG_NOSYSTEM=1 GIT_CONFIG_GLOBAL=/dev/null \
  GIT_NO_REPLACE_OBJECTS=1 \
  python3 "$ROOTS_CONTROL/contrib/devtools/roots-promotion-contract.py" \
  "$ROOTS_CONTROL/contrib/roots/promotion-29.4.json" \
  --repository "$PROMOTION_REPOSITORY"
```

Run this twice. It is read-only and, while `authorization` is false, proves
topology only. It does not authorize a push or tag. The checked-in 29.4
contract initially binds both public branch refs to the canonical commit;
reviewed promotion work must update the production binding to the final frozen
commit before release authorization. Never edit the record merely to match an
unexpected ref.

The current trusted replay workflow is
`.github/workflows/roots-trusted-replay.yml`; its only manual input is `run`,
with `auto` or `force`. A forced qualification invocation is:

```bash
gh workflow run roots-trusted-replay.yml \
  --ref codex/roots-29-4-release -f run=force
```

Record the run URL, exact control commit, decision report digest, replay
artifacts, and conclusions. Full qualification also requires the applicable PR
and nightly-equivalent suites from `.github/workflows/ci.yml` and
`.github/workflows/nightly.yml`, plus the five release package rehearsal. A
workflow name alone is not evidence: bind every result to the immutable
candidate SHA.

## 6. Publish review refs, then fast-forward production

This is the first phase allowed to write remote branch refs. Before every push,
resolve the remote again and compare it with the reviewed expected old value.
Use an exact non-force refspec. An unexpected present, absent, or moved ref is a
stop, not a reason to add `--force`.

The 29.4 topology first establishes `roots/29.4` at the canonical commit when
the ref is absent and the reviewed plan explicitly allows it. The following
block is fail-closed for both permitted starting states: an existing production
ref must already equal the canonical commit, while an absent ref requires the
explicit `create-if-absent` action. It rechecks absence immediately before the
only creation push and requires the canonical value afterward.

```bash
production_before="$(git ls-remote --heads origin "$PRODUCTION_REF")"
if [[ -z "$production_before" ]]; then
  if [[ "$INITIAL_PRODUCTION_REF_ACTION" != create-if-absent ]]; then
    printf '%s\n' \
      "production ref is absent and initial creation is not authorized" >&2
    exit 1
  fi

  # Re-resolve immediately before the exact non-force creation push. Perform
  # this boundary only during the reviewed exclusive publication window.
  test -z "$(git ls-remote --heads origin "$PRODUCTION_REF")"
  git push origin "$CANONICAL_COMMIT:$PRODUCTION_REF"
else
  test "$(printf '%s\n' "$production_before" | awk 'END {print NR}')" = 1
  test "$(printf '%s\n' "$production_before" | awk '{print $2}')" = \
    "$PRODUCTION_REF"
  test "$(printf '%s\n' "$production_before" | awk '{print $1}')" = \
    "$CANONICAL_COMMIT"
fi

production_established="$(git ls-remote --heads origin "$PRODUCTION_REF")"
test "$(printf '%s\n' "$production_established" | awk 'END {print NR}')" = 1
test "$(printf '%s\n' "$production_established" | awk '{print $2}')" = \
  "$PRODUCTION_REF"
test "$(printf '%s\n' "$production_established" | awk '{print $1}')" = \
  "$CANONICAL_COMMIT"
```

Any other production OID is a stop. Never delete, rewind, or force the ref to
make this check pass. Initial establishment is distinct from publishing the
frozen candidate for review. Re-resolve both exact refs, require the integration
ref to be absent, then publish it with a separate exact non-force refspec:

```bash
test "$(git ls-remote --heads origin "$PRODUCTION_REF" | awk '{print $1}')" = \
  "$CANONICAL_COMMIT"
test -z "$(git ls-remote --heads origin "$INTEGRATION_REF")"

# Only at the reviewed review-ref boundary:
git push origin "$FROZEN_COMMIT:$INTEGRATION_REF"
test "$(git ls-remote --heads origin "$INTEGRATION_REF" | awk '{print $1}')" = \
  "$FROZEN_COMMIT"
```

After trusted CI and review accept that exact frozen SHA, start from a new
clone and fast-forward production. This is a later boundary, not part of initial
production-ref establishment or integration publication:

```bash
git clone --no-tags --branch "${PRODUCTION_REF#refs/heads/}" origin \
  "$WORK/production-promotion"
git -C "$WORK/production-promotion" fetch --no-tags origin \
  "$FROZEN_COMMIT"
test "$(git -C "$WORK/production-promotion" rev-parse HEAD)" = \
  "$CANONICAL_COMMIT"
git -C "$WORK/production-promotion" merge --ff-only "$FROZEN_COMMIT"
test "$(git -C "$WORK/production-promotion" rev-parse HEAD^{tree})" = \
  "$FROZEN_TREE"

git ls-remote --heads origin "$PRODUCTION_REF" >"$WORK/production-now.txt"
test "$(awk '{print $1}' "$WORK/production-now.txt")" = "$CANONICAL_COMMIT"
git -C "$WORK/production-promotion" push origin \
  "HEAD:$PRODUCTION_REF"

production_final="$(git ls-remote --heads origin "$PRODUCTION_REF")"
integration_final="$(git ls-remote --heads origin "$INTEGRATION_REF")"
test "$(printf '%s\n' "$production_final" | awk 'END {print NR}')" = 1
test "$(printf '%s\n' "$production_final" | awk '{print $2}')" = \
  "$PRODUCTION_REF"
test "$(printf '%s\n' "$production_final" | awk '{print $1}')" = \
  "$FROZEN_COMMIT"
test "$(printf '%s\n' "$integration_final" | awk 'END {print NR}')" = 1
test "$(printf '%s\n' "$integration_final" | awk '{print $2}')" = \
  "$INTEGRATION_REF"
test "$(printf '%s\n' "$integration_final" | awk '{print $1}')" = \
  "$FROZEN_COMMIT"
```

The final commands re-resolve both exact refs and require the same frozen
commit on each. Confirm separately that `main` and all tags are unchanged. Do
not establish production, publish the integration ref, and advance production
in one unreviewed command sequence.

## 7. Create the immutable tag and signed draft

Tagging is permanent. It begins only after production, integration, accounting,
review, and CI all resolve to the authorized commit and the release tag is
absent. The control-plane workflow `.github/workflows/create-release.yml` has
the exact inputs `tag`, `production_ref`, `expected_commit`, and
`confirm_release`:

```bash
gh workflow run create-release.yml \
  --ref codex/roots-29-4-release \
  -f tag="$RELEASE_TAG" \
  -f production_ref="${PRODUCTION_REF#refs/heads/}" \
  -f expected_commit="$FROZEN_COMMIT" \
  -f confirm_release=true
```

Before dispatch, `ci/release/validate-promotion-source.py` must accept the
authorized contract, exact checked-out production commit, and tag. Never create
the tag manually if this workflow rejects.

Also inspect the `release.yml` stored in the exact production commit. It must
accept the authorized production topology. If it still requires the tag commit
to be an ancestor of `main`, stop: the Core-rooted production line cannot pass
that condition. Correct the workflow through a reviewed and accounted
production unit before tagging; never bypass or weaken the check during a run.

The tagged `.github/workflows/release.yml` builds five packages, creates
source-bound build evidence, prepares `SHA512SUMS`, and passes only that
verified manifest to the isolated `release-signing` environment. Candidate
build jobs and draft-publication jobs must not receive
`BITCOIN_ROOTS_GPG_SK`. Verify the expected public-key fingerprint, every
package archive root and version, SHA512 entry, build attestation, source commit
and tree, evidence link, and detached signature in a fresh unauthenticated
client. The release remains a draft after signing.

## 8. Publish only the independently verified draft

Compute the SHA256 of the downloaded, verified `SHA512SUMS`. The guarded
`.github/workflows/publish-release.yml` inputs are `tag`,
`confirm_publication`, and `verified_manifest_sha256`:

```bash
MANIFEST_SHA256="$(sha256sum SHA512SUMS | awk '{print $1}')"
gh workflow run publish-release.yml \
  --ref codex/roots-29-4-release \
  -f tag="$RELEASE_TAG" \
  -f confirm_publication=true \
  -f verified_manifest_sha256="$MANIFEST_SHA256"
```

That workflow may change only the verified draft's visibility. It must not
build, sign, create or move a tag, or alter a branch. After publication, repeat
tag, hash, signature, evidence, asset, and source checks from a fresh client.

## Interruption, recovery, and restart decisions

Use `inspect` before deciding whether an interrupted replay is resumable:

```bash
python3 "$REPLAY" inspect --state "$STATE_FILE"
python3 "$REPLAY" resume \
  --repository "$OBJECT_REPOSITORY" \
  --revision "$CORE_COMMIT" --expected-tree "$CORE_TREE" \
  --manifest "$MANIFEST" --materials "$MATERIALS" \
  --materials-root "$MATERIAL_ROOT" \
  --state "$STATE_FILE" --state-directory "$STATE_DIRECTORY"
```

Resume only when source, tree, manifest, materials, tool/configuration digest,
completed units, and owned-candidate tree are unchanged. On drift, preserve the
state and start a new disposable run. To abandon without deleting evidence:

```bash
python3 "$REPLAY" abandon --state "$STATE_FILE" \
  --output "$STATE_DIRECTORY/replay-abandoned.json"
```

Recovery by boundary:

- Before any remote ref: discard only the explicitly named disposable run and
  reproduce it; retain evidence needed to diagnose the failure.
- After the integration ref: leave it immutable, correct the candidate with a
  new reviewed commit/ref only if the release plan authorizes that change.
- After production fast-forward but before tagging: do not rewind production;
  stop and create a reviewed forward correction.
- After tag creation: never move or delete the tag. Fix code in a new release.
- After draft creation: do not replace verified assets in place; create a new
  authorized release identity if bytes or provenance are wrong.
- After publication: never rewrite history or assets; publish a new corrective
  release and document the incident.

## Future Bitcoin Core cycles

For each Core release, create new versioned manifests, materials, outcome maps,
fixtures, accounting, and promotion records. Start again from the new verified
Core annotated tag; do not rebase the previous production branch or reuse 29.4
materials. Review every old adaptation as accept, rewrite, reject, defer, or
upstream, and update tests before allowlisting the new base in trusted replay.

Keep reusable process in tools and schemas; keep release-specific IDs in a
versioned appendix or record. A later Core tag is not allowed merely because
the replay tool can apply the old patches.

## Bitcoin Roots 29.4 immutable appendix

The following values are the accepted 29.4 inputs as of this runbook. The
machine-readable records remain authoritative.

| Binding | Exact value |
| --- | --- |
| Core tag | `v29.4` |
| Core annotated tag object | `4e70eab99b60f7718b78e2158de9fb82726f3cec` |
| Core peeled commit | `3fc0865963a38b871e9f7d94e6151c4953563516` |
| Core tree | `38ad59b187f59647eb90ad1347bc481485ef4d01` |
| Accepted replay tree | `39a5e30207a09962e78ae81c24cc65b1e478ef90` |
| Canonical sixteen-commit head | `cbc88cff9b35b95a549c0313e424e13093fcd6a1` |
| Frozen production commit | `dfc74d403585f7c23815ef80d2e206b85c33919a` |
| Frozen production tree | `477eb9b3f50098b0b8548a9ff35efaa29fd7ecde` |
| Accepted private frozen ref | `refs/roots/29.4/frozen-production-g2` |
| Rejected legacy private ref | `refs/roots/29.4/frozen-production` at `ad3126495925c93e1b1341146be9cc9512d3d448` |
| Review ref | `refs/heads/integration/roots-29.4` |
| Production ref | `refs/heads/roots/29.4` |
| Release tag | `refs/tags/v29.4-roots.1` |
| Control branch base | `e2387f0d975121869064e55eb0afb99e7639120b` |
| Accepted-candidate source anchor | `dd3050a6101a071a0b642ba71ab7eaddbe4cf5b8` |
| Preserved 29.3 source commit/tree | `42098b53c57fb6818736c7b6732fa84ffe6ad391` / `a5708dcbf1d2611360fab68fc6a8e504db1ba95d` |

Relevant schemas and records are:

- `contrib/roots/promotion-contract.schema.json` and
  `contrib/roots/promotion-29.4.json`;
- `contrib/roots/replay-materials.schema.json` and
  `contrib/roots/replay-plan.schema.json`;
- `contrib/roots/adaptation-manifest.schema.json` and
  `contrib/roots/adaptation-manifest-29.3.json`;
- `contrib/roots/candidate-evidence.schema.json` and
  `contrib/roots/commit-topology.schema.json`;
- `contrib/roots/post-candidate-inventory-29.4.json`,
  `contrib/roots/post-candidate-replay-29.4.json`, and
  `contrib/roots/post-candidate-invariants-29.4.json`;
- `contrib/roots/frozen-production-29.4.json`; and
- `contrib/roots/production-accounting-29.4.json`.

For 29.4, both `authorization` in `promotion-29.4.json` and authorization in
`production-accounting-29.4.json` remain false until the later reviewed
promotion/release boundaries update them. The frozen record itself says
`publication_authorized: false`. None of these records presently authorizes a
push, tag, signature, draft, or publication.
