# Bitcoin Roots maintainer workflow

Bitcoin Roots is maintained as a small, reviewable Git history on top of
Bitcoin Core. Bitcoin Core v30.3 is the direct upstream base for this release.
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

For example, `roots/29.4` starts directly at `v29.4`, while `roots/30.3`
starts directly at `v30.3`. Do not merge the complete 29.4 branch into the 30.3
branch. Port the semantic Roots commits to the new Core base, dropping behavior
that Core has adopted and adapting or splitting commits when its APIs changed.

Pull requests that promote a release into `main` and pull requests that review
the canonical patch series have different bases. Never rebase or squash the
canonical series merely to make a promotion pull request appear linear. Build
the promotion from `main`, retain the canonical tip as a merge parent, and
verify tree equality before publishing it.

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
git fetch "$core_remote" --tags
core_commit=$(git rev-parse "$core_tag^{commit}")
git show --no-patch --decorate "$core_commit"
git switch --create roots/30.3 "$core_commit"
git cherry-pick -x <semantic-roots-commit>
```

Repeat the cherry-pick for each semantic commit after deciding whether that
change is still needed. `-x` records the source commit when the new commit is a
true cherry-pick. When a conflict requires a material rewrite, explain the old
commit and the deviation in the new commit message rather than retaining a
misleading cherry-pick identity.

Compare the old and new generations as patch series:

```sh
git range-diff v29.4..roots/29.4 v30.3..roots/30.3
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
canonical_ref=origin/roots/30.3
git switch --create promote/30.3 origin/main
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

## Release handoff

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

For an explicitly requested optional rehearsal:

```sh
release_tag=v30.3-roots.1
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
release_tag=v30.3-roots.1
git check-ref-format "refs/tags/$release_tag"
git fetch origin roots/30.3
git fetch https://github.com/bitcoin/bitcoin.git \
    refs/tags/v30.3:refs/tags/v30.3
release_commit=$(git rev-parse origin/roots/30.3^{commit})
git tag --annotate "$release_tag" "$release_commit" -m "Bitcoin Roots $release_tag"
git rev-parse "$release_tag^{commit}"
test "$(git rev-parse "$release_tag^{commit}")" = \
    "$(git rev-parse origin/roots/30.3^{commit})"
ci/release/validate-release-source.sh "$release_tag"
```

Run the retained CI/release metadata tests before pushing (this includes signing,
portable-patch replay, archive/COPYING and signed-draft gates):

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
