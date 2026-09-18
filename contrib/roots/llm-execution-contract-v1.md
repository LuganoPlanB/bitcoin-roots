# Bitcoin Roots clean-clone LLM execution contract v1.0.0

This document and `llm-execution-contract-v1.json` are the complete execution
boundary for a capable LLM with no prior conversation. The JSON file is the
machine authority; this document explains and invokes it. A mismatch is a safe
stop. Neither file grants authority to push, create or move a release tag, sign
release artifacts, publish, read credentials, or access private paths.

## Classified statements

Verified facts are limited to versioned evidence: the locked Core commits and
trees, the 29.3 replay artifacts, the accepted Core-rooted 29.4 topology, and
the rejection of the incremental 29.4 oracle. Policy decisions are that every
future release starts at a verified Bitcoin Core tag (never Knots or a previous
Roots trunk), policy stays outside consensus, and every difference and
post-base commit is accounted. Optional recommendations are to use a
network-free locked mirror for rehearsal and retain disposable output through
review. Unresolved questions are the historical Core-tag signer verification
record and every later-Core lock: stop until a human supplies reviewed evidence.

The historical lineage ledger says the Core v29.3 and v29.4 tag signatures
were unverified. Therefore an online release-capable run must fetch the exact
tag refs, run `git verify-tag`, record the signer fingerprint, and still match
the tag-object, peeled-commit, and tree locks. The offline mode below is only a
deterministic rehearsal; it cannot upgrade that signature status.

## Immutable inputs and permitted remotes

Only these fetch-only remotes are allowed:

```text
core   https://github.com/bitcoin/bitcoin.git
knots  https://github.com/bitcoinknots/bitcoin.git (29.3 provenance only)
roots  https://github.com/LuganoPlanB/bitcoin-roots.git
local  ${ROOTS_SOURCE} (network-free rehearsal only)
```

Core v29.3 is tag object
`f2ca1bc7f94f1f662e2d7ee0858f2d90018bb863`, commit
`99003bed87333f1be51bf3070235591b3a72f007`, tree
`d7910bd5e9335128932f1f848a767d773895c4a4`. Core v29.4 is tag object
`4e70eab99b60f7718b78e2158de9fb82726f3cec`, commit
`3fc0865963a38b871e9f7d94e6151c4953563516`, tree
`38ad59b187f59647eb90ad1347bc481485ef4d01`. The JSON contract locks the
SHA-256 of every manifest, methodology, fixture, materials, topology, and
acceptance record consumed below.

Validate before interpreting any other field:

```bash
export LC_ALL=C TZ=UTC
ROOTS_SOURCE="$(pwd -P)"
CONTRACT="$ROOTS_SOURCE/contrib/roots/llm-execution-contract-v1.json"
python3 "$ROOTS_SOURCE/contrib/devtools/roots-llm-contract.py" validate "$CONTRACT"
```

Stop on any `E_*` diagnostic. Do not repair, reorder, substitute, or infer.

## Clean clone and exact tag verification

For a release-capable online run, use new paths and explicit tag refspecs:

```bash
WORK="$(mktemp -d)"
SOURCE="$WORK/source"
git clone --no-tags https://github.com/LuganoPlanB/bitcoin-roots.git "$SOURCE"
git -C "$SOURCE" remote add core https://github.com/bitcoin/bitcoin.git
git -C "$SOURCE" fetch --no-tags core \
  refs/tags/v29.3:refs/tags/v29.3 refs/tags/v29.4:refs/tags/v29.4
test "$(git -C "$SOURCE" rev-parse refs/tags/v29.3)" = f2ca1bc7f94f1f662e2d7ee0858f2d90018bb863
test "$(git -C "$SOURCE" rev-parse refs/tags/v29.4)" = 4e70eab99b60f7718b78e2158de9fb82726f3cec
git -C "$SOURCE" verify-tag v29.3
git -C "$SOURCE" verify-tag v29.4
test "$(git -C "$SOURCE" rev-parse v29.3^{commit})" = 99003bed87333f1be51bf3070235591b3a72f007
test "$(git -C "$SOURCE" rev-parse v29.3^{tree})" = d7910bd5e9335128932f1f848a767d773895c4a4
test "$(git -C "$SOURCE" rev-parse v29.4^{commit})" = 3fc0865963a38b871e9f7d94e6151c4953563516
test "$(git -C "$SOURCE" rev-parse v29.4^{tree})" = 38ad59b187f59647eb90ad1347bc481485ef4d01
test -z "$(git -C "$SOURCE" status --porcelain)"
git -C "$SOURCE" for-each-ref --format='%(refname) %(objectname)' refs/tags >"$WORK/tags.before"
```

Record each successful signature's primary fingerprint. Stop on an unavailable
tag, unknown or unreviewed signer, bad signature, dirty tree, replace/graft or
alternate-object mechanism, unexpected object format, or any object mismatch.

For a network-free rehearsal, clone the locked local mirror and fetch the
validation object. The output explicitly remains `offline-locked-mirror`:

```bash
WORK="$(mktemp -d)"
SOURCE="$WORK/source"
OUTPUT="$WORK/llm-output"
git clone --no-local "$ROOTS_SOURCE" "$SOURCE"
git -C "$SOURCE" fetch --no-tags "$ROOTS_SOURCE" \
  refs/remotes/origin/ci/l7-validation/39a5e302:refs/remotes/origin/ci/l7-validation/39a5e302
python3 "$ROOTS_SOURCE/contrib/devtools/roots-llm-contract.py" rehearse "$CONTRACT" \
  --repository "$SOURCE" --output-directory "$OUTPUT" --offline-locked-mirror
```

This single command executes exact-object verification, Core→Knots 29.3,
Knots→Roots 29.3, the Core-rooted canonical 29.4 constructor, artifact-digest
checks, and first-parent commit-to-adaptation checks. Expected results are:

```text
Core→Knots 29.3 tree  56f97d3a9199c1fb191e1b8a21f2caae3901b7f6
Roots 29.3 tree       a5708dcbf1d2611360fab68fc6a8e504db1ba95d
29.4 target commit   cbc88cff9b35b95a549c0313e424e13093fcd6a1
29.4 target tree     39a5e30207a09962e78ae81c24cc65b1e478ef90
```

The report must say `passed`, contain sixteen first-parent commit mappings,
leave source HEAD and tags unchanged, and match the machine-declared evidence
and stable-report schemas. Repeat from another clean clone and byte-compare
`llm-evidence-bundle.json` and `llm-rehearsal-report.json`.

## Adaptation order, context, and approvals

The JSON `adaptations` array is the only order and maps exactly one adaptation
to each of the sixteen canonical first-parent commits. Its dependency graph is
closed and its path scopes are SHA-256 locked to JSON pointers in
`adaptation-manifest-29.3.json`. Each matching `context_packets` entry gives a
bounded set of provenance, materials, review, generator, and test paths. Read
only that packet and its referenced manifest scope when applying the unit.

For every semantic or manual boundary ask exactly the packet's question and
accept only `accept`, `rewrite`, `reject`, `defer`, or `upstream`, together with
reviewer, rationale, tests, and candidate tree. Silence, ambiguity, a choice
outside the allowlist, or missing evidence means safe stop. Consensus-adjacent,
policy, P2P, wallet, release, and generated-output units never inherit approval
from another unit.

Generated manpages consume candidate binaries, help output, and version
identity. Run only the declared commands and require the six declared outputs
to be fresh. A changed input, output, command, path, dependency, order,
expected result, or manual instruction is a contract-version change, not an
opportunity to guess.

## Incremental oracle, tests, accounting, and release preparation

Run the diagnostic oracle separately:

```bash
bash "$ROOTS_SOURCE/contrib/roots/replay-29.4-proposal/incremental-oracle.bash" \
  "$SOURCE" "$WORK/incremental-29.4" | tee "$WORK/incremental-29.4.txt"
grep -Fx 'conflict_count=12' "$WORK/incremental-29.4.txt"
grep -Fx 'oracle_commit=3e29908f7a0131a71309e80a78fe865ec8a50f76' "$WORK/incremental-29.4.txt"
grep -Fx 'oracle_tree=c676e8944470cc74fcc213e7368aed359ad8ae55' "$WORK/incremental-29.4.txt"
```

It remains rejected even if a later run happens to produce an equal tree.
Run every test path in `required_tests`; then run trusted replay, continuous
accounting, release evidence generation/verification, and release preparation
exactly as specified in `maintainer-runbook.md`. All changed hunks and
post-base commits require an owner, provenance, risk, tests, dependencies,
scope, rationale, and replay impact. Release preparation may create evidence
and SHA512 sums only. It may not sign, tag, push, or publish.

For a later Core tag, fetch and verify its annotated tag, record exact tag
object/commit/tree, and run `roots-replay.py plan`. The current gate rejects
all such tags with `later base is not allowlisted`. Stop until a reviewed
contract version supplies the new lock, fixture, adaptation/material records,
outcome map, generators, expected hashes, tests, and approvals.

## Interrupt, resume, abandon, and safe stop

On interruption, preserve the state directory, owned candidate, reports, and
tag snapshot. Inspect and resume only with the identical base, tree, manifest,
materials, state directory, and candidate tree:

```bash
python3 "$ROOTS_SOURCE/contrib/devtools/roots-replay.py" inspect --state "$STATE_FILE"
python3 "$ROOTS_SOURCE/contrib/devtools/roots-replay.py" resume \
  --repository "$SOURCE" --revision "$BASE_COMMIT" --expected-tree "$BASE_TREE" \
  --manifest "$MANIFEST" --materials "$MATERIALS" --materials-root "$MATERIALS_ROOT" \
  --state "$STATE_FILE" --state-directory "$STATE_DIRECTORY"
```

On drift or rejection, preserve evidence and abandon without deletion:

```bash
python3 "$ROOTS_SOURCE/contrib/devtools/roots-replay.py" abandon \
  --state "$STATE_FILE" --output "$STATE_DIRECTORY/replay-abandoned.json"
git -C "$SOURCE" for-each-ref --format='%(refname) %(objectname)' refs/tags >"$WORK/tags.after"
cmp "$WORK/tags.before" "$WORK/tags.after"
```

The contract runner writes `llm-safe-stop.json` after an owned output directory
exists. It contains a stable error code, preserve-and-review action, resume
flag, and contract digest. Never continue after a lock, schema, path, order,
dependency, generator, expected-result, approval, authority, or invariant
failure. A human alone may authorize later signing, tagging, or publication.
