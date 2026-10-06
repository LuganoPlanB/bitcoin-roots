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

Start from a clean worktree and name the Core base explicitly. For the v29.4
work, the peeled Core commit is `3fc0865963a38b871e9f7d94e6151c4953563516`.
The following commands inspect history without changing it:

```sh
core_base=3fc0865963a38b871e9f7d94e6151c4953563516
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

The local v29.4 tag is signed, but its signer public key is not available in
this checkout. Do not claim that `git verify-tag v29.4` cryptographically
verified it. Record the peeled commit above and obtain the signer key before
making such a claim.

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
canonical_ref=origin/roots/29.4
git switch --create promote/29.4 origin/main
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

## Release handoff

Complete independent review of the canonical implementation before assigning a
permanent tag. Artifact inspection and post-publication checks complement that
review; they cannot replace it.

Release tags use the Roots form `v<core-version>-roots.<positive-integer>`;
release candidates may use `v<core-version>rc<n>-roots.<positive-integer>`.
Before creating a permanent tag, merge the reviewed promotion so the trusted
workflow is present on the default branch. Then dispatch `Release artifacts`
with the future tag name and the full 40-hex canonical commit. Manual dispatch
fetches the official Core tag from the Bitcoin Core repository, verifies the
commit against `origin/roots/<core-version>`, creates an annotated tag only in
each disposable runner checkout, and builds the same five-platform artifact
set. It has read-only repository permissions and cannot create a GitHub
release. It also refuses to run if the future tag already exists remotely.

Coordinate a stable canonical tip from rehearsal through tag builds and draft
verification. Each release job fetches the branch again and requires tip
equality; advancing it during a run can fail later jobs even though the input
commit was pinned. If it changes before tagging, review and rehearse the new
candidate. After tagging, never move the tag to repair a failed run.

The manual workflow comes from `main`, but build scripts are checked out from
the requested canonical commit; the tag-triggered workflow comes from the tag.
Require the release workflow and its script interface to agree between the
reviewed main revision and the candidate. Include necessary workflow fixes on
the canonical branch before promotion and rehearsal.

For example (substitute the actual reviewed candidate):

```sh
release_tag=v30.3-roots.1
git fetch origin roots/30.3
release_commit=$(git rev-parse origin/roots/30.3^{commit})
gh workflow run release.yml --ref main \
    -f release_tag="$release_tag" -f release_commit="$release_commit"
```

Download and inspect that run's five packages and patch artifact before
assigning the permanent version. After the rehearsal passes, fetch again and
verify the annotated tag target and ancestry immediately before pushing it:

```sh
# In a dedicated release worktree, retain the reviewed rehearsal commit.
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
python3 ci/test/test_prepare_release.py
python3 ci/test/test_prepare_release_source.py
python3 ci/test/test_create_patch_series.py
python3 ci/test/test_sign_manifest.py
python3 ci/test/test_validate_release_tag.py
python3 ci/test/test_validate_release_source.py
python3 ci/test/test_release_version.py
```

GitHub Actions tag workflows check out the tagged commit; a release tag does not
need to be reachable from `main`. Release validation instead requires the tag
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

Pushing the annotated tag builds Linux x86_64, Linux aarch64, macOS x86_64,
macOS arm64, and Windows x86_64 archives. The draft release also contains the
Git-am-compatible patch series and `SHA512SUMS`; `SHA512SUMS.asc` is included
when the release environment has a matching signing key configured. The
tag-triggered workflow creates a draft, not an immediately public release.
Independently verify the draft's archives, patch, checksums, optional signature,
tag target, and source tree before changing only the draft's visibility.


Before publication, verify the release notes and contributor acknowledgements.
Generated GitHub notes can miss changes ported from an earlier generation, and
updating an existing draft only uploads assets; it does not regenerate notes.
Prepare notes before approving publication, then publish the inspected draft
without rebuilding or replacing its assets.

Do not rerun a tag release workflow after publication: its existing-release
path uploads with `--clobber` and can replace public assets. Diagnose failed
pre-publication runs against the immutable tag; never move a tag or rewrite a
public release to hide a failure. Once the canonical branch advances, its tip
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
never rerun the published tag's release workflow, replace assets or move the tag
to repair website visibility. The website-owned
[backfill, cache, verification and recovery guide](../../doc/website/RELEASE-INPUTS.md)
documents the single local build command and publication diagnostics.
