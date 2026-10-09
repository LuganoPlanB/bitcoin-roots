# Bitcoin Roots maintainer workflow

Bitcoin Roots is maintained as a small, reviewable Git history on top of
Bitcoin Core. Each canonical line names its exact upstream version; for example,
`roots/29.4` starts at `v29.4` and `roots/30.3` starts at `v30.3`.
Bitcoin Knots is historical provenance for selected Roots policy features, not
an upstream, a dependency, or a source of consensus rules.

Git history is the canonical representation of Roots changes. Do not maintain
replay frameworks, patch manifests, generated evidence, frozen candidate state,
or promotion metadata alongside it. The archived replay branch is historical
evidence only; do not rewrite or depend on it.

## Branch roles

Keep the source patch series, product integration history, and generated
release artifacts separate:

- `roots/<core-version>` is the canonical release line. It starts at the exact
  matching Bitcoin Core tag and contains a linear series of semantic Roots
  commits. Released tips are immutable.
- `topic/<core-version>/<area>` holds work for one reviewable area. Accepted
  commits are applied to the canonical release line.
- `integration/<core-version>` is disposable combined-CI state. It may be
  rebuilt and must not be used as the base for durable work.
- `main` is the currently promoted product history. An explicit promotion merge
  connects it to a reviewed canonical release line; the merge result must have
  the same product files as that canonical tip. Reviewed repository-only
  website, contribution guidance, and website lint integration may remain on
  `main`; list every such difference in the promotion PR.
- `archive/*` preserves superseded candidates and retired maintenance systems.
  Archive branches are evidence, not dependencies or release inputs.

For example, `roots/29.4` starts directly at `v29.4`, while `roots/30.3`
starts directly at `v30.3`. Do not merge the complete 29.4 branch into the 30.3
branch. Port the semantic Roots commits to the new Core base, dropping behavior
that Core has adopted and adapting or splitting commits when its APIs changed.

Pull requests that promote a release into `main` and pull requests that review
the canonical patch series have different bases. Never rebase or squash the
canonical series merely to make a promotion pull request appear linear. Build
the promotion from `main`, retain the canonical tip as a merge parent, and
verify product equality and enumerate repository-only differences before
publishing it.

Run the command sequences below in Bash with `set -euo pipefail`, substitute
reviewed values for placeholders, and stop on any failed check. Use clean,
dedicated worktrees for mutations.

## Inspecting a maintenance branch

Start from a clean worktree and name the Core base explicitly. For the v30.3
work, the peeled Core commit is `49faec4f87f5cd19c88db01a82e5c68b087c8227`.
The following commands inspect history without changing it:

```sh
core_base=49faec4f87f5cd19c88db01a82e5c68b087c8227
git status --short
git show --no-patch --decorate "$core_base"
git log --oneline --decorate "$core_base"..HEAD
git diff --stat "$core_base"..HEAD
git merge-base --is-ancestor "$core_base" HEAD
```

Use `git show-ref --heads --tags` and `git rev-parse --verify <ref>` before
using a local name in automation. `git merge-base --is-ancestor A B` succeeds
only when `A` is an ancestor of `B`; it is preferable to inferring ancestry
from a graph view. Confirm object identities with `git rev-parse <ref>^{commit}`.

Authenticate the official tag and its signer key through independent trusted
sources before claiming verification. A missing local signer key is not proof
of an invalid signature. Run `git verify-tag v30.3` with the authenticated key
and record both the signature result and its trust basis.

## Exporting and reviewing a series

Export the reviewed range directly from Git. Choose an output directory outside
the repository or an ignored disposable directory:

```sh
git format-patch --cover-letter --output-directory /tmp/roots-patches \
    "$core_base"..HEAD
git format-patch --stdout "$core_base"..HEAD > /tmp/roots-series.mbox
git diff --check "$core_base"..HEAD
```

Before and after a rewrite, retain the previous tip in a local variable or
named branch and compare the two series. `range-diff` makes changes to commit
content and ordering visible to reviewers:

```sh
previous_tip=<previous-reviewed-tip>
git range-diff "$core_base"..."$previous_tip" "$core_base"...HEAD
git diff --check "$previous_tip"..HEAD
```

Do not use an exported series as a second source of truth. It is a review or
transfer artifact; the Git commits remain canonical.

## Porting a Core release

Fetch and inspect the intended Core tag or commit first. Configure `core` to
the official Bitcoin Core repository once, or select an already-configured
remote explicitly; do not assume that every checkout has a remote named
`core`. Create a dedicated maintenance branch, then port the Roots series with
ordinary Git operations:

```sh
core_remote=<configured-bitcoin-core-remote>
core_tag=v30.3
git remote get-url "$core_remote"
git fetch --no-tags "$core_remote" "refs/tags/${core_tag}:refs/tags/${core_tag}"
git verify-tag "$core_tag" # Authenticate the signer key separately.
core_commit=$(git rev-parse "$core_tag^{commit}")
git show --no-patch --decorate "$core_commit"
git switch --create roots/30.3 "$core_commit"
git switch --create topic/30.3/port
git cherry-pick -x <semantic-roots-commit>
```

If the destination branch already exists, inspect its ancestry and coordinate
with its owner instead of recreating or overwriting it. Pin the source Roots
tip and account for every source commit as kept, adapted, adopted upstream, or
intentionally dropped. Include implementation, tests, help and recent fixes.

### The semantic review unit

Each new port commit should carry one independently maintainable behavior and
the tests, help and documentation needed to maintain it. File count is not the
criterion: a behavior can cross several libraries, while two unrelated options
in one file may need separate commits. Keep unrelated cleanup out of the port.
For each retained behavior, the commit message and port PR must identify:

- the operator-visible behavior, its owner and why Roots still needs it;
- the full source commit IDs, including follow-up fixes and original attribution;
- which source changes are kept unchanged, adapted, adopted upstream, or
  deliberately dropped, with reasons and destination commit IDs or upstream
  references;
- relevant upstream API or ownership changes and how adaptations preserve the
  intended behavior and policy/consensus boundary;
- the owning focused test commands and results, plus help/default checks and
  any broader verification required by the affected paths.

Account for every source change, not just every source commit: a mixed source
commit may contain all four dispositions. Put retained/adapted provenance in
the destination commit messages; put omitted changes and their reasons in the
port PR. The PR ties those messages together as a review index. Do not create a
checked-in inventory, generated manifest, or patch database alongside Git.

If a cross-cutting change cannot be separated without breaking its invariant,
document the dependency, the inseparable behavior and its combined checks in
the message and PR. Reviewers should be able to explain why the unit belongs
together; do not force an artificial split or impose an arbitrary size limit.

For example, historical `1b73fe3d21` combines policy/network controls across 78
files and `85257a0986` combines runtime controls across 44. Inspect their full
IDs, messages and touched paths before planning a future port:

```sh
git rev-parse 1b73fe3d21^{commit} 85257a0986^{commit}
git show --stat --oneline 1b73fe3d21
git show --stat --oneline 85257a0986
git show --format=fuller 1b73fe3d21 -- src/policy test/functional
```

These published commits are reference inputs, not candidates for rewriting.
A future port can separate independently owned datacarrier, sigop and other
controls, include their focused tests/help in each new unit, and cite the same
old commit in each adapted message. Account explicitly for the remaining
changes rather than treating the source commit as wholly retained.

### Comparing dispositions across generations

Pin both Core bases and Roots tips as full commit IDs before review; branches
can advance. After substituting those four reviewed IDs, these checks are
read-only:

```sh
old_core=REPLACE_WITH_OLD_CORE_40_HEX_COMMIT
old_tip=REPLACE_WITH_OLD_ROOTS_40_HEX_COMMIT
new_core=REPLACE_WITH_NEW_CORE_40_HEX_COMMIT
new_tip=REPLACE_WITH_NEW_ROOTS_40_HEX_COMMIT
git merge-base --is-ancestor "$old_core" "$old_tip"
git merge-base --is-ancestor "$new_core" "$new_tip"
git log --reverse --format='%H %s' "$old_core..$old_tip"
git log --reverse --format='%H %s' "$new_core..$new_tip"
git rev-list --merges "$new_core..$new_tip" # Must print nothing.
git range-diff "$old_core..$old_tip" "$new_core..$new_tip"
git diff --check "$new_core..$new_tip"
```

In range-diff, `=` denotes a matched unchanged patch, `!` a matched changed
patch, `<` an unmatched old commit and `>` an unmatched new commit. Matching
is heuristic: it does not prove semantic preservation or reliably label a
one-to-many split. For a split, the PR must map the full old ID to every new
ID and explain the scope of each. For `<`, explain adoption upstream or a
deliberate drop; for `>`, explain a split, adaptation or justified new work.
Inspect the actual patches and owning tests alongside this comparison.

Use the earlier `git cherry-pick -x` example only for actual faithful picks.
For a material adaptation or split, edit the new implementation and its tests
on an unpublished topic branch, then create each semantic commit normally:

```sh
git diff --check
git add -- path/to/behavior path/to/owning-test path/to/help
git diff --cached
git commit # Name source IDs, dispositions, adaptation and verification.
```

These are placeholders for the reviewed paths, not commands to run against
the historical ports. Perform any split or rebase only in unpublished review
history; preserve published canonical commits and tags.

A new branch at the upstream tag initially lacks Roots CI support. Port the
reviewed workflow and the scripts it invokes together before relying on Roots
PR checks. Some jobs use the PR workflow while explicitly checking out its head;
a newer workflow with an older source tree can fail on missing scripts. Verify
which revision every job tests. Push events alone are not evidence that the
full PR test matrix ran.

Repeat the cherry-pick for each semantic commit after deciding whether that
change is still needed. `-x` records the source commit when the new commit is a
true cherry-pick. When a conflict requires a material rewrite, explain the old
commit and the deviation in the new commit message rather than retaining a
misleading cherry-pick identity.

Compare the old and new generations as patch series:

```sh
git range-diff v29.4..roots/29.4 v30.3..roots/30.3
```

Review topic PRs against the canonical branch and integrate them linearly.
Rebase-merging can change commit IDs; record and verify the final canonical
commit and run any required checks not covered by equivalent tested content.
Never squash the complete port or create merge commits in the canonical range.

Resolve only identified conflicts, inspect the result with `git diff` and
targeted tests, then use `git cherry-pick --continue`. If the port premise is
wrong, stop with `git cherry-pick --abort`; do not hide a conflict with a bulk
overwrite. The range-diff must explain commits that were added, dropped, split,
or materially changed before requesting review.

## Maintaining supported release lines

Apply a security or correctness fix first to the oldest supported Roots line
that needs it. After review and testing, cherry-pick it with `-x` into each newer
affected line. Document deviations required by newer Core code. Do not rewrite
published release commits or tags.

A maintenance release therefore advances its own canonical branch, for example
from `v29.4-roots.1` to `v29.4-roots.2`; it does not require merging a newer
Core line into the older one.

## Promoting a canonical line into `main`

Create the promotion in a separate worktree so that the canonical branch stays
clean:

```sh
canonical_ref=origin/roots/30.3
git switch --create promote/30.3 origin/main
git merge --no-ff --no-commit "$canonical_ref"
# Resolve deliberately, then verify the proposed index and worktree.
git diff --cached --name-status
git commit
git diff --name-status "$canonical_ref"..HEAD
# Review every difference; only explicitly approved repository-only paths
# may differ. Product source, build inputs, and feature tests must match.
git diff --exit-code "$canonical_ref"..HEAD -- src CMakeLists.txt cmake depends
```

For a new Core generation, a normal merge can retain obsolete files or old
product changes from `main`. Review every difference from canonical, including
unconflicted paths, rather than assuming a clean merge is correct. Preserve the
website and other approved repository-only work, but reconcile product files
to the new canonical tree. Merge the promotion PR with **Create a merge commit**;
do not squash or rebase it.

The merge must retain the canonical tip as a parent. Its product files must
match that tip; the source/build command above is a partial check, not a
substitute for reviewing the complete difference list. When there are no
repository-only exceptions, also require complete tree equality. Any exception
must name exact paths and explain why they do not alter the released product.
For website preservation, this can include `doc/website/**`, its deployment
workflow, contribution/maintainer guidance, and the lint exclusions required by
the website mirror. Other CI and feature tests still match the canonical tip.

Release tags and artifacts continue to use the canonical branch, never the
promotion commit. A promotion branch is review state and may be rebuilt with a
force-with-lease; the canonical release branch is not rewritten after publication.

## Editing policy safely

Roots may retain configurable conservative mempool, relay, and mining-template
policy. Keep that behavior in policy/admission/template boundaries and include
operator help/configuration plus feature-owned tests in the same reviewable
commit. Do not import Knots as an upstream dependency.

Policy rejection is not block validity. For every change that can reject a
consensus-valid transaction locally, test both sides of the boundary:

1. the configured node rejects it from admission, relay, or template selection;
2. a consensus-valid block containing it is still accepted.

For Core ports and changes affecting policy, validation or serialization,
include the source-pinned paired block/state evidence described in the
[compatibility review contract](compatibility/README.md). Name expected local
admission differences separately from block acceptance and chain state.
The contract defines smoke/full profiles for external qualification tooling;
this documentation alone does not establish that the tooling has run.
Docs-only changes state why runtime evidence is inapplicable and need no node
build. Keep the existing feature-owned tests and required CI checks.

Never add RDTS/BIP110 consensus enforcement. Audit every essential touch to
`src/consensus/`, `src/script/`, or validation code, and make no incidental
consensus-adjacent cleanup. Run focused unit and functional tests while editing,
then the relevant broad suite. At handoff, also run:

```sh
cmake -S . -B build
cmake --build build --target bitcoind test_bitcoin -j 4
build/test/functional/test_runner.py mempool_dust.py
git diff --check
git status --short
ctest --test-dir build --output-on-failure
```

Use a configured build directory appropriate to the affected targets; see the
repository `AGENTS.md` for safe regtest and build guidance.

## Rewrites and public history

Use interactive rebase only on an unpublished maintenance branch, and review
the resulting series before sharing it:

```sh
git branch reviewed-before-rebase HEAD
git rebase --interactive "$core_base"
git range-diff "$core_base"...reviewed-before-rebase "$core_base"...HEAD
```

Do not rewrite a branch other people may have based work on without explicit
coordination. When a reviewed public branch must be updated, inspect its remote
tracking state and use a lease rather than an unrestricted force push:

```sh
git fetch origin
git log --left-right --graph --cherry-pick origin/<branch>...HEAD
git push --force-with-lease origin HEAD:<branch>
```

Never force-push release tags. Preserve archival branches.

## Reusing canonical CI for a pure main promotion

Run ordinary fix CI, review it and integrate the canonical topic by fast-forward
before opening/updating its main promotion PR. Preserve a two-parent promotion
with current main first and the exact current canonical tip second.
`verify-promotion-ci.py` checks blob/mode/symlink parity outside the reviewed
main-only website/guidance/lint exceptions; CI and release workflows are never
exceptions. It requires the latest complete successful PR Tests run at the exact
candidate, in this repository, targeting its canonical branch, with successful
classifier, lint and required-result jobs. Claimed promotion proof mismatches
fail; they do not waive tests. Ordinary PRs retain normal CI.

A proven promotion reuses compiled-test results, while its classifier/regression
suite, lint and applicable independent main docs/site checks still run. Explicit
`ci:full` or other `ci:*` coverage labels keep requested fresh tests. Open the
promotion after source CI passes so no duplicate matrix is scheduled while
candidate evidence is incomplete. Optional checks do not claim reproducible
binaries, and existing release tags cannot be manually rehearsed.

Matching-Core block compatibility always runs fresh for a promotion candidate.
The existing source/tree/workflow proof still governs reuse of other compiled
checks; generic prior CI success does not establish this fixture evidence.

## Matching Core block compatibility in CI

`matching Core block compatibility` is separate from previous-release wallet
and interoperability checks. PRs select smoke for critical policy, validation,
kernel, script, consensus and transaction changes, compatibility tooling and
workflow changes, and unknown/incomplete changed-file inventories. Ordinary
documentation changes can skip it. `ci:compat` requests both compatibility lanes;
`ci:full` requests full nightly assurance, including one full block comparison,
and avoids an overlapping smoke run.

Nightly `all` includes full comparison. The standalone `block-compatibility`
suite runs it without unrelated assurance matrices. Manual requests run even
when scheduled assurance would skip an unchanged source:

```sh
gh workflow run nightly.yml --ref topic/30.3/release-methodology \
    -f suite=block-compatibility -f context-ref=roots/30.3
```

The workflow derives the official Core tag commit from the reviewed generation
branch convention and verifies its ancestry in candidate and canonical source.
For `main`, committed package metadata narrows the generation, then official tag
pinning and ancestry confirm its reviewed canonical base; a version string alone
does not authenticate it. Unknown or ambiguous generations fail; the current fixtures support Core 30.3 only. A new
Core generation needs reviewed fixture expectations before qualification.
Official repository/tag commit pinning does not authenticate the tag signer.

Both nodes build in-run from immutable Git exports with the same recorded
node-only configuration and no compiled-binary cache. Reports account for all
three smoke or six full cases and record source trees, binary/configuration
digests, fixture/serialization provenance and cleanup. Missing, skipped,
malformed, stale or failed selected evidence fails the required result. Review
bounded sanitized artifacts on failures, fix the cause, then rerun complete
coverage; never reinterpret a skip as qualification. These are finite fixture
results, not exhaustive consensus equivalence or wallet/relay interoperability.

## Release handoff

Complete independent review of the canonical implementation before assigning a
permanent tag. Artifact inspection and post-publication checks complement that
review; they cannot replace it.

Release tags use the Roots form `v<core-version>-roots.<positive-integer>`;
release candidates may use `v<core-version>rc<n>-roots.<positive-integer>`.
Before creating a permanent tag, merge the reviewed promotion so the trusted
workflow is present on the default branch. The default path tags the reviewed
canonical source once; the tag runs fresh five-platform builds, required
manifest signing and new-draft creation. Independently verify that draft before
separate publication approval.

An optional `Release artifacts` manual dispatch rehearses an absent future tag
at the full 40-hex canonical commit. Manual dispatch
fetches the official Core tag from the Bitcoin Core repository, verifies the
commit against `origin/roots/<core-version>`, creates an annotated tag only in
each disposable runner checkout, and builds the same five-platform artifact
set. It has read-only repository permissions and cannot create a GitHub
release. It also refuses to run if the future tag already exists remotely.

Both manual rehearsal and permanent tag builds require full matching-Core block
compatibility at the exact resolved release-source commit before any platform
build. The metadata job retains canonical-source/tag validation and resolves the
matching official Core base; changed source/configuration/profile requires new
evidence. Failed or skipped comparison blocks platform builds and draft creation.
Compatibility evidence is a CI artifact, separate from the eight release assets.

Inspect the run and download its report alongside the existing patch/package
checks (substitute the run ID and full immutable identities):

```sh
gh run view "$RUN_ID" --repo LuganoPlanB/bitcoin-roots
gh run download "$RUN_ID" --repo LuganoPlanB/bitcoin-roots \
    --name "block-compatibility-full-${ROOTS_SHA}-${CORE_SHA}" --dir compatibility-evidence
```

Require `status=passed`, `qualified=true`, the full six-case inventory, both
expected source commits/trees, in-run build provenance and current configuration.
Inspect `sources.json` and `status.json` for source/run identity and confirm the
artifact came from the selected run. Sanitized reports retain bounded diagnostics;
credential-like lines are redacted. A local/mock test does not substitute for an
actual successful reusable workflow run.

Coordinate a stable canonical tip through required CI, immutable tagging and
signed CI draft verification. Each build job and the CI draft helper refreshes
canonical-tip equality; pinned inputs alone do not prevent branch advancement
from invalidating a gate. If source changes before tagging, renew source review
and required CI. The permanent tag triggers fresh builds of all five platforms;
prior rehearsal packages are inspection evidence, not final release assets.
After tagging, never move the tag to repair a failed handoff.

The manual workflow comes from `main`, but build scripts are checked out from
the requested canonical commit. A permanent tag push runs the workflow and
scripts from that tagged source, builds all five platforms anew, then signs the
manifest and creates a new draft in the `release` environment. Require the
workflow and script interfaces to agree between reviewed main and canonical
revisions. Include necessary workflow fixes on the canonical branch before
promotion and tagging (and any optional rehearsal). Public publication still
needs separate approval after independent signed-draft verification.

For an explicitly requested optional rehearsal (substitute the reviewed candidate):

```sh
release_tag=v30.3-roots.1
git fetch origin roots/30.3
release_commit=$(git rev-parse origin/roots/30.3^{commit})
gh workflow run release.yml --ref main \
    -f release_tag="$release_tag" -f release_commit="$release_commit"
```

When requested, download and inspect the optional rehearsal's packages and
patch. It does not replace the final tag run or establish binary reproducibility.
Corrections renew source review, required CI and affected final-build verification.
An optional rehearsal remains optional; repeat it only when explicitly requested.
A failed final build requires diagnosis and renewed affected verification.
Fetch again and verify the annotated tag target and ancestry
immediately before pushing:

```sh
# In a dedicated release worktree, retain the reviewed canonical commit.
release_tag=v30.3-roots.1
release_commit=REPLACE_WITH_REVIEWED_40_HEX_COMMIT
git check-ref-format "refs/tags/$release_tag"
git fetch origin roots/30.3
git fetch --no-tags https://github.com/bitcoin/bitcoin.git \
    refs/tags/v30.3:refs/tags/v30.3
test "$release_commit" = "$(git rev-parse origin/roots/30.3^{commit})"
git switch --detach "$release_commit"
# Confirm the release tag is absent both locally and remotely before creating it.
git tag --annotate "$release_tag" "$release_commit" -m "Bitcoin Roots $release_tag"
ci/release/validate-release-source.sh "$release_tag"
```

The validator requires `HEAD` to equal the tagged commit, as well as requiring
the tag to equal the current canonical tip. Run it in that release worktree,
not on the promotion merge. An annotated tag is not necessarily signed; verify
and describe tag and checksum signatures separately. Rehearsal tags must live
in disposable clones (worktrees share a tag namespace), so an ephemeral tag
cannot be accidentally pushed as the permanent release.

Run the retained release metadata tests before pushing:

```sh
python3 -m unittest discover -s ci/test
```

A release tag does not need to be reachable from `main`. Permanent tag pushes
start fresh five-platform builds and a tag-only signed-draft job; manual
dispatch remains a read-only rehearsal. Release validation requires the tag
target to match its canonical `roots/<core-version>` branch. The release patch
is generated from the Core tag and canonical commits at release time and is not
checked into the repository. It deliberately excludes `.github/**`: Roots'
GitHub Actions and repository-management files remain in the canonical branch
and release tag, while the portable patch contains the product code, build
support, documentation, and source tests. Replay verification requires every
path outside `.github/**` to match the release tag and confirms that the patch
leaves Core's own `.github/**` tree unchanged.

Before pushing a tag, the patch artifact can be generated and replay-verified
locally without adding it to Git:

```sh
ci/release/create-patch-series.sh "$release_tag" \
    "/tmp/bitcoin-roots-${release_tag#v}.patch"
```

The series contains inherited CRLF files. Apply the released mbox with
`git am -3 --keep-cr <bitcoin-roots-*.patch>` so Git's mail parser preserves
those bytes. Replay verification uses the same command and checks tree equality
outside `.github/**`.

Both workflow paths build Linux x86_64/aarch64, macOS x86_64/arm64 and Windows
x86_64 packages, preserving existing project license files and staging canonical
`COPYING` before archive upload. The user deferred expanded dependency
copyright/REUSE coverage to future releases. Dependency notice collection and
its schema are optional tooling, not a current release gate; no complete
coverage claim is made. See `ci/release/README.md` for that optional interface.

The tag-only `release` environment job waits for all five builds and the patch,
downloads only its own run's outputs and runs `prepare-release.sh`. It requires
the existing organization secret `BITCOIN_ROOTS_GPG_SK`, confined to the signing
step, and verifies checked-in public-key fingerprint
`5EADD53F2CD1F0B7AEEE920D25FC5C29CD528E32`. Missing signing capability fails.
`create-ci-draft.py` independently verifies the detached manifest signature,
packet/source/patch and fresh remote identities, and creates only a new draft
with exactly eight assets: five archives, patch, SHA512SUMS and SHA512SUMS.asc.
Every existing draft/public release blocks creation. There is no update,
clobber, deletion or replacement branch; interrupted creation requires operator
inspection, never an automatic retry that replaces partial assets.

Curated notes retain contributor credit, including #24/#25, and the CI draft
adds the actual fresh run/source identity and signing disclosures. Manifest
signing is separate from Git-tag and platform-code signing. Independently
download and verify all eight draft assets and notes before separately authorized
visibility-only publication. Verify the public release again unauthenticated.
Never replace published tags/assets; maintenance uses a reviewed new release.


Before publication, verify the release notes and contributor acknowledgements.
Curated notes must include changes ported from an earlier generation.
The signed CI draft includes the source-controlled curated notes and actual
run/source provenance; the helper refuses any existing draft and never replaces
its body or assets.
Prepare notes before approving publication, then publish the inspected draft
without rebuilding or replacing its assets.

Never replace public release tags or assets, use `--clobber`, or repeat a
release operation to rewrite public assets. Diagnose a failed pre-publication
handoff against the immutable tag; the helper refuses an existing draft/public
release. Once the canonical branch advances, its tip
is no longer evidence for an older release. Historical verification uses the
immutable tag, authenticated upstream base, public checksums/signatures and
clean patch replay, rather than requiring the old tag to equal today's tip.

After publication, repeat the tag, asset, checksum and signature-if-present
checks through an unauthenticated client. Then refresh the website's historical
patch browser using the separate documentation workflow from trusted `main`:

```sh
gh workflow run deploy-docs.yml --repo LuganoPlanB/bitcoin-roots --ref main
gh run list --repo LuganoPlanB/bitcoin-roots --workflow deploy-docs.yml --branch main --limit 5
gh run watch <refresh-run-id> --repo LuganoPlanB/bitcoin-roots --exit-status
```

Select the newly dispatched run ID. After it succeeds, check the
[public patch index](https://plan-b.foundation/bitcoin-roots/patches/), new release
overview, representative commit/file links and original download/checksum links.
Confirm the public deployment receipt and catalogue account for every public
release, including historical entries without a patch. Matching checksums do not
by themselves authenticate a signature.

The documentation workflow also checks inventory every six hours, plus GitHub
scheduling delay, to catch missed dispatches. Release events use tag refs and
`GITHUB_TOKEN` publication can suppress follow-on event workflows; refresh does
not rely on either. A failed website run preserves the last successful site and
does not invalidate the binary release. Retry `deploy-docs.yml` after diagnosis;
never replace published assets or move the tag to repair website visibility. The website-owned
[backfill, cache, verification and recovery guide](../../doc/website/RELEASE-INPUTS.md)
documents the single local build command and publication diagnostics.
