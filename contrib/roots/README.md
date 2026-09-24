# Bitcoin Roots maintainer workflow

Bitcoin Roots is maintained as a small, reviewable Git history on top of
Bitcoin Core. Bitcoin Core v29.4 is the direct upstream base for this release.
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
  exactly the same tree as that canonical tip.
- `archive/*` preserves superseded candidates and retired maintenance systems.
  Archive branches are evidence, not dependencies or release inputs.

For example, `roots/29.4` starts directly at `v29.4`, while `roots/30.0`
starts directly at `v30.0`. Do not merge the complete 29.4 branch into the 30.0
branch. Port the semantic Roots commits to the new Core base, dropping behavior
that Core has adopted and adapting or splitting commits when its APIs changed.

Pull requests that promote a release into `main` and pull requests that review
the canonical patch series have different bases. Never rebase or squash the
canonical series merely to make a promotion pull request appear linear. Build
the promotion from `main`, retain the canonical tip as a merge parent, and
verify tree equality before publishing it.

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
core_tag=v30.0
git remote get-url "$core_remote"
git fetch "$core_remote" --tags
core_commit=$(git rev-parse "$core_tag^{commit}")
git show --no-patch --decorate "$core_commit"
git switch --create roots/30.0 "$core_commit"
git cherry-pick -x <semantic-roots-commit>
```

Repeat the cherry-pick for each semantic commit after deciding whether that
change is still needed. `-x` records the source commit when the new commit is a
true cherry-pick. When a conflict requires a material rewrite, explain the old
commit and the deviation in the new commit message rather than retaining a
misleading cherry-pick identity.

Compare the old and new generations as patch series:

```sh
git range-diff v29.4..roots/29.4 v30.0..roots/30.0
```

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
git diff --exit-code "$canonical_ref"..HEAD
test "$(git rev-parse HEAD^{tree})" = "$(git rev-parse "$canonical_ref^{tree}")"
```

The last two commands are the defining promotion check: the merge records both
histories, but its product tree is identical to the canonical release tip. A
promotion branch is review state and may be rebuilt with a force-with-lease;
the canonical release branch is not rewritten after publication.

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

Release tags use the Roots form `v<core-version>-roots.<positive-integer>`;
release candidates may use `v<core-version>rc<n>-roots.<positive-integer>`.
Before creating or pushing a tag, run the repository checks and verify the
annotated tag target and ancestry:

```sh
release_tag=v29.4-roots.1
git check-ref-format "refs/tags/$release_tag"
git tag --annotate "$release_tag" -m "Bitcoin Roots $release_tag"
git rev-parse "$release_tag^{commit}"
test "$(git rev-parse "$release_tag^{commit}")" = \
    "$(git rev-parse origin/roots/29.4^{commit})"
ci/release/validate-release-source.sh "$release_tag"
```

Run the retained release metadata tests before pushing:

```sh
python3 ci/test/test_prepare_release.py
python3 ci/test/test_validate_release_tag.py
python3 ci/test/test_validate_release_source.py
python3 ci/test/test_release_version.py
```

GitHub Actions tag workflows check out the tagged commit; a release tag does not
need to be reachable from `main`. Release validation instead requires the tag
target to match its canonical `roots/<core-version>` branch. The release patch
is generated from the Core tag and canonical commits at release time and is not
checked into the repository. Do not create nightly, promotion, or
generated-evidence control planes around this workflow.
