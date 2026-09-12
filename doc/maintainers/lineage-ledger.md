# Immutable lineage ledger

`contrib/roots/lineage-ledger.json` is the machine-readable record for a Roots
release lineage. It is deliberately stricter than a release name: a name is not
an identity until its repository URL, exact tag refspec, tag object, peeled
commit, and tree are recorded. Git branches, abbreviated object IDs, working
trees, and a maintainer's reflog are never release identities.

## Identity vocabulary

Each comparison uses exactly one level. `same` at one level says nothing about
another level.

| Level | What is compared |
| --- | --- |
| `tag_object` | Annotated tag object bytes and signature evidence. |
| `commit` | Peeled commit object and its parent relation. |
| `complete_tree` | Every Git tree entry: path, type, mode, and blob bytes. |
| `maintained_source` | Explicit maintained-source path set after listed exclusions. |
| `generated_release_artifact` | Generated files or release metadata named by the claim. |
| `build_input` | Build scripts, declared options, toolchain inputs, and generators. |
| `reproducible_binary` | Binaries built with an explicit reproducible-build recipe. |
| `consensus` | Block-validity invariants and their named tests. |
| `policy` | Local admission, relay, and mining-policy invariants and their tests. |
| `api_config` | Documented RPC, command-line, and configuration contract. |

Every claim has nonempty `scope`, explicit `exclusions`, argv-form `command`,
and an algorithm-qualified `evidence_digest`. A claim cannot record bare
`same=true`; the ledger uses `status: same`, `different`, `blocked`, or
`manual`. This prevents tree identity from being misrepresented as metadata,
generated-output, build, consensus, policy, or API compatibility.

Layer exception states are `core_base`, `knots_layer`, `roots_layer`,
`absorbed_upstream`, `obsolete`, `rejected`, and `manual`. They classify a
delta or a blocked decision; they do not silently change an upstream object.

## Resolution and trust policy

The only permitted retrieval form is an explicit exact-tag fetch, for example:

```
git init roots-lineage-core
git -C roots-lineage-core remote add origin https://github.com/bitcoin/bitcoin.git
git -C roots-lineage-core fetch --no-tags https://github.com/bitcoin/bitcoin.git \
  refs/tags/v29.3:refs/tags/v29.3
```

Fetch into a disposable repository. Do not fetch a branch as a substitute and
do not update an existing release tag. Inspect `for-each-ref`, `rev-parse
<tag>^{commit}`, `rev-parse <tag>^{tree}`, and `verify-tag --raw` with a
separately provisioned, pinned keyring. `signature: verified` requires the
recorded 40-hexadecimal key fingerprint and `trust_status: verified`. The
deterministic `trust_status` vocabulary records `unverified`, `revoked`,
`expired`, or `not-available`; it records verification evidence but never
performs cryptographic verification itself. Such a status cannot be promoted
to verified by trust-on-first-use. A missing signature requires
`trust_status: not-available`, and `trust_status: verified` requires a verified
signature.

Two fresh non-shallow, explicit exact-tag fetches resolved the annotated Roots
`v29.3-roots.1` tag object `sha1:9b083fbac340b58ccf23c70d01958d62cd7eb79b`,
peeled commit `sha1:42098b53c57fb6818736c7b6732fa84ffe6ad391`, and tree
`sha1:a5708dcbf1d2611360fab68fc6a8e504db1ba95d`. In both clones,
`git verify-tag --raw v29.3-roots.1` returned exit status 1 with `error: no
signature found`; the ledger therefore records `signature: unsigned` and
`trust_status: not-available`. This establishes only the immutable release
object. There is currently no Roots-to-Knots equality claim and no substitute
Knots tag is used.

The initial “1:1” statement is an erratum, not an equality claim: the complete
tree and the classified maintained-source scope are both recorded as
`different`. Generated manpages and build inputs are likewise compared only by
their explicit classifications. The three validation-adjacent paths remain
manual invariant review records; none is used to assert consensus or policy
equivalence.

Each persisted classification record has a `provenance` field that is its
deterministic rationale reference (`build-path`, `generated-path`,
`branding-path`, `validation-adjacent`, and similar bounded values). Reviewers
must use that reference with the record's blob/mode data and, for text, its
hunk digest; it is not a behavioral equivalence claim.

The same two-clone procedure reproduces the Core and Knots tuples already in
the ledger. Both tags contain signatures, but `verify-tag --raw` exits 1 in
each clone because the verification keyring reports `NO_PUBKEY`: Core reports
signing fingerprint `F2CFC4ABD0B99D837EEBB7D09B79B45691DB4173` and Knots
reports `1A3E761F19D2CC7785C5502EA291A2C45D0C504A`. These are observed signing
identifiers, not trusted-key fingerprints. Accordingly, both releases remain
`signature: unverified` and `trust_status: unverified`; the ledger does not
claim signature verification or record either untrusted identifier as a trusted
key.

## Local reproducibility evidence matrix

These checks are complete without network access. They validate the ledger
contract and recorded local provenance; they do not establish a Roots-to-Knots
equivalence claim.

| Completed local requirement | Exact command or test | Required invariant |
| --- | --- | --- |
| Stable ledger schema and values | `LC_ALL=C python3 contrib/devtools/roots-lineage.py validate contrib/roots/lineage-ledger.json` | Current ledger validates; Roots records an annotated tag/commit/tree tuple, unsigned signature result, and no equality claim. |
| Deterministic regeneration | `LC_ALL=C python3 -m unittest ci/test/test_roots_lineage.py` | The fixture regenerates twice byte-identically; an unavailable fixture invokes neither Git nor a network path. |
| Immutable-input rejection | `LC_ALL=C python3 -m unittest ci/test/test_roots_lineage.py` | Tests reject mutable or mismatched refs, malformed/partial object sets, substituted repositories, shallow histories, unsafe URLs/labels, and invalid trust states. |
| First-fork local provenance | `LC_ALL=C python3 -m unittest ci/test/test_roots_lineage.py` | The authorized fork commit, direct parent, tree, 40-path status counts, and raw-diff digest match the checked-out Git history. |
| Two-clone release reconstruction | Exact-tag fetch recipe above, once in each fresh Core, Knots, and Roots clone | Every ledger tag object, peeled commit, tree, parent list, and SHA-1 object format agrees across the pair; Roots is unsigned and the Core/Knots keyring results are explicitly unverified. |
| Documentation and whitespace | `(cd test/lint/test_runner && RUST_BACKTRACE=1 cargo run --offline -- --lint=doc --lint=trailing_whitespace)` | Documentation lint and trailing-whitespace lint pass without fetching dependencies. |

Still required before any initial-alignment claim: the all-level Roots/Knots
comparison and classification required by L1.3. The resolved Roots release
object and recorded signature outcomes do not themselves prove any
Roots-to-Knots equality.

The separately recorded `fork_starts` relation identifies the first Roots fork
commit and its direct parent/tree. It is commit provenance only: it neither
publishes a release tag nor changes the resolved Roots release object or proves
Roots-to-Knots equivalence. Its first-boundary evidence is the
raw, no-rename Git diff from the direct parent to the first fork commit: 40
paths (38 modified, one added, one deleted), with the ledger's SHA-1 digest of
the exact `LC_ALL=C` raw output. This evidence is not a classification of the
40 paths and must not be used to narrow or prove a release-equivalence claim.

To independently recheck this local provenance evidence, make two full local
clones of the recorded Roots history and run the following in each clone:

```
git clone --no-local --no-checkout /path/to/verified-roots-history roots-fork-check
git -C roots-fork-check rev-parse 07580114c35e870e242621316ec8cd051a938787^{commit}
git -C roots-fork-check show -s --format=%P 07580114c35e870e242621316ec8cd051a938787
git -C roots-fork-check rev-parse 07580114c35e870e242621316ec8cd051a938787^{tree}
git -C roots-fork-check diff --raw --no-renames --no-ext-diff \
  99ee26e9df0e63a5d1e0ab6bd46b1862ce67648b \
  07580114c35e870e242621316ec8cd051a938787 | sha1sum
```

Require `rev-parse --is-shallow-repository` to return `false`. This procedure
checks only local commit provenance and the fork boundary. It does not fetch,
verify a remote tag signature, resolve `v29.3-roots.1`, or establish any
Roots-to-Knots release equivalence; those remain the separate authorized
handoff checks below.

## Authorized Roots identity handoff

Resolving a future unavailable Roots record requires an authorized release
maintainer to provide a signed handoff record containing all of these values,
obtained from the canonical publication rather than from a local checkout:

- canonical HTTPS repository URL, `release_label`, project-bound ledger ID,
  short tag spelling (`v<release_label>`), and matching full
  `refs/tags/<tag>` `tag_ref`;
- algorithm-qualified annotated tag-object ID, peeled commit ID, and tree ID;
- tag-signature result, `trust_status`, and, only when verified, the trusted
  40-hexadecimal key fingerprint; and
- two independent, non-shallow clean-clone command transcripts using the exact
  tag refspec and the maintainer's pinned keyring policy.

The receiving maintainer first records the supplied values for review, then
uses two fresh disposable clones. The following commands are a recipe template:
they are not evidence until the authorized handoff supplies their bracketed
values.

```
git clone --no-checkout [CANONICAL_HTTPS_URL] roots-lineage-roots-a
git -C roots-lineage-roots-a fetch --no-tags origin \
  refs/tags/[TAG]:refs/tags/[TAG]
git -C roots-lineage-roots-a rev-parse refs/tags/[TAG]^{tag}
git -C roots-lineage-roots-a rev-parse refs/tags/[TAG]^{commit}
git -C roots-lineage-roots-a rev-parse refs/tags/[TAG]^{tree}
git -C roots-lineage-roots-a verify-tag --raw [TAG]

git clone --no-checkout [CANONICAL_HTTPS_URL] roots-lineage-roots-b
git -C roots-lineage-roots-b fetch --no-tags origin \
  refs/tags/[TAG]:refs/tags/[TAG]
git -C roots-lineage-roots-b rev-parse refs/tags/[TAG]^{tag}
git -C roots-lineage-roots-b rev-parse refs/tags/[TAG]^{commit}
git -C roots-lineage-roots-b rev-parse refs/tags/[TAG]^{tree}
git -C roots-lineage-roots-b verify-tag --raw [TAG]
```

Before accepting either clone, verify `rev-parse --is-shallow-repository` is
`false`, compare every returned value to the handoff record, and run
`roots-lineage.py regenerate` with the supplied clone. The tool never fetches
implicitly. A failed signature, wrong/revoked trust result, missing annotated
tag, or any tag/commit/tree mismatch leaves `resolution_status: unavailable`;
do not replace it with a local commit or begin an equality comparison. An
unsigned tag is recorded as `signature: unsigned` and
`trust_status: not-available`, as for the current Roots record.

## Validation and deterministic regeneration

Python 3.10+ is sufficient; no third-party module or network connection is
used. The validator runs Git using argv arrays under `LC_ALL=C`, rejects
shallow repositories, and writes stable UTF-8 JSON with sorted keys and LF.

```
python3 contrib/devtools/roots-lineage.py validate contrib/roots/lineage-ledger.json
python3 contrib/devtools/roots-lineage.py regenerate contrib/roots/lineage-ledger.json \
  --repository core-29.3=../roots-lineage-core \
  --repository knots-29.3.knots20260507=../roots-lineage-knots \
  --output /tmp/lineage-ledger-a.json
python3 contrib/devtools/roots-lineage.py regenerate contrib/roots/lineage-ledger.json \
  --repository core-29.3=../roots-lineage-core \
  --repository knots-29.3.knots20260507=../roots-lineage-knots \
  --output /tmp/lineage-ledger-b.json
cmp /tmp/lineage-ledger-a.json /tmp/lineage-ledger-b.json
```

`regenerate` verifies supplied complete local repositories only; it never
fetches. It rejects a moved tag, a missing annotated tag/peeled commit, a
wrong object format, stale tag/tree evidence, a substituted repository, and a
shallow history. An unavailable release is intentionally not regenerated until
its exact immutable object exists.

Non-tree comparisons fail closed until their exclusions are explicitly named.
`maintained_source`, `generated_release_artifact`, and `build_input` never
silently reuse a complete-tree result: each requires at least one explicit
classified exclusion, which remains in its JSON output and evidence digest.
`consensus` and `policy` comparisons remain intentionally unsupported by the
generic tool and require an explicit invariant test and review.

Schema versions are append-only. Readers must reject an unknown major
`schema_version`, preserve fields they understand from older versions, and
never infer an omitted identity level. A future version may add a migration
tool, but must retain a deterministic reader for version 1.

## L1 final-gate exception

This exception applies only to final acceptance of this L1. No configured CTest
build tree was available. The repository-wide offline lint runner also fails on
paths unchanged from L1's base commit `42098b53c57fb6818736c7b6732fa84ffe6ad391`:
subtree-history checks under `src/leveldb` and `src/secp256k1`, executable-mode
checks for `ci/release/archive.py` and `ci/test/test_gui_qrencode.py`, and
duplicate-include checks in existing `src/` files. L1 modifies none of those
paths. The accepted L1-specific gate is the focused lineage suite, ledger and
report validation against clean clones, byte-stable regeneration, Python
compilation, documentation/trailing-whitespace lint, and `git diff --check`.
It does not waive any later milestone's full-gate requirement.
