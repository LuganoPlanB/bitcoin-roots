# Bitcoin Roots maintainer workflow

Bitcoin Roots is maintained as a small, reviewable Git history on top of
Bitcoin Core. Bitcoin Core v29.4 is the direct upstream base for this release.
Bitcoin Knots is historical provenance for selected Roots policy features, not
an upstream, a dependency, or a source of consensus rules.

Git history is the canonical representation of Roots changes. Do not maintain
replay frameworks, patch manifests, generated evidence, frozen candidate state,
or promotion metadata alongside it. The archived replay branch is historical
evidence only; do not rewrite or depend on it.

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
core_tag=v29.5
git remote get-url "$core_remote"
git fetch "$core_remote" --tags
core_commit=$(git rev-parse "$core_tag^{commit}")
git show --no-patch --decorate "$core_commit"
git switch --create roots/v29.5 "$core_commit"
git cherry-pick <first-roots-commit>^..<last-roots-commit>
```

For a linear Roots series, rebasing is often clearer than replaying individual
commits:

```sh
old_core_base=<old-core-peeled-commit>
git rebase --onto "$core_commit" "$old_core_base"
```

Both commands stop on conflicts. Resolve only the identified files, inspect the
result with `git diff` and targeted tests, then use `git cherry-pick --continue`
or `git rebase --continue`. If the port premise is wrong, stop with
`git cherry-pick --abort` or `git rebase --abort`; do not hide a conflict with a
bulk overwrite. Compare the proposed result to the prior reviewed series using
`git range-diff` before requesting review.

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
git merge-base --is-ancestor "$release_tag^{commit}" origin/main
ci/release/validate-release-source.sh "$release_tag"
```

Run the retained release metadata tests before pushing:

```sh
python3 ci/test/test_prepare_release.py
python3 ci/test/test_validate_release_tag.py
python3 ci/test/test_validate_release_source.py
python3 ci/test/test_release_version.py
```

The tag-triggered workflow builds the minimal Linux artifact, validates
archives, produces a SHA512 manifest, and creates a draft release only in the
protected release environment. Do not create nightly, promotion, or
generated-evidence control planes around this workflow.
