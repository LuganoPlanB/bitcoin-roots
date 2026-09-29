Contributing to Bitcoin Roots
=============================

Bitcoin Roots welcomes code, review, testing, documentation, and release
engineering contributions. This guide explains how a contribution fits the
Roots maintenance model. General coding, build, and test requirements are in
the [developer notes](doc/developer-notes.md); the exact maintainer commands and
release controls are in the
[Roots maintainer workflow](contrib/roots/README.md).

Project boundaries
------------------

Bitcoin Roots is a small, reviewable series of semantic changes carried on top
of Bitcoin Core. Bitcoin Knots records the lineage of selected Roots features;
it is not the Git base, a build dependency, or a source of consensus rules.

Every contribution must preserve these boundaries:

* Bitcoin Roots remains compatible with Bitcoin Core consensus.
* Conservative transaction relay, mempool, and mining-template behavior is
  local policy. A policy-rejected transaction may still be consensus-valid,
  and a valid block containing it must remain acceptable.
* Roots does not enforce RDTS/BIP110 consensus rules.
* Git commits are the source of truth for the Roots patch. Exported patch files,
  release archives, reports, and integration branches are derived artifacts.
* Consensus, validation, serialization, wallet, networking, and cryptographic
  changes require the smallest justified patch and regression coverage.

The two contribution journeys
-----------------------------

Most product work belongs to one of two journeys. Name the journey in the pull
request so reviewers know which history and evidence to compare.

| Journey | Question | Starting point | Result |
| --- | --- | --- | --- |
| [1. Change the Roots patch](#journey-1-change-the-roots-patch) | What should Roots add, remove, or change on a supported Core base? | The oldest affected `roots/<core-version>` branch | Reviewed semantic commits carried forward to newer supported Roots lines |
| [2. Port Roots to a new Core release](#journey-2-port-roots-to-a-new-core-release) | How should the existing Roots behavior apply to a newer Core base? | The exact official Bitcoin Core tag for the new version | A new `roots/<new-core-version>` branch containing the still-relevant Roots series |

Do not mix the journeys casually. Adding a feature and porting the entire Roots
series create different review questions. If both are needed, normally finish
and review the feature on the oldest affected supported line first, then carry
that reviewed behavior into the new Core line.

Branch and release model
------------------------

The branch name communicates whether a ref is durable product history or
temporary review state:

* `roots/<core-version>` is the canonical release line. It begins at the exact
  matching Bitcoin Core tag and contains the semantic Roots commit series for
  that Core version.
* `topic/<core-version>/<area>` is a focused feature, fix, or porting branch.
  Product changes normally enter a canonical Roots line through such a branch.
* `integration/<core-version>` is disposable combined-CI state. Do not base
  durable work on it.
* `promote/<name>` is temporary review state used to connect a reviewed
  canonical line to `main`.
* `main` is the integrated public project history and the default branch. It
  records explicit release promotions and repository-level work such as the
  project website. A commit being on `main` does not by itself make it part of
  a release patch.
* `archive/*` preserves retired candidates or maintenance systems as evidence.
  Archive branches are not release inputs.

A release branch can advance as fixes accumulate. A release itself is the
immutable annotated tag `v<core-version>-roots.<number>` and the verified
artifacts built from that tagged canonical commit. Release candidates use
`v<core-version>rc<number>-roots.<number>`.

Choose the pull-request base from the intended ownership:

* Product behavior, source tests, build support, and documentation that must
  ship in the portable Roots patch target the relevant
  `roots/<core-version>` line.
* A new-Core port targets its new `roots/<core-version>` line after a maintainer
  establishes that branch at the official Core tag.
* Website, repository-hosting, and other explicitly main-only changes target
  `main`.
* A release promotion targets `main`, retains the canonical tip as a merge
  parent, and follows the promotion checks in `contrib/roots/README.md`.

Getting started
---------------

Before editing, read the root [README](README.md), the relevant source and test
documentation, and the current [Roots feature catalog](doc/roots-features.md).
Build out of source and use only disposable regtest data directories.

The ordinary GitHub workflow is:

1. Fork `LuganoPlanB/bitcoin-roots` if you do not have a writable remote.
2. Fetch the current target branch and its exact upstream Core tag.
3. Create one focused topic branch from the correct base.
4. Make atomic commits with the implementation, tests, operator help, and
   documentation needed to explain the same behavior.
5. Run the narrowest relevant tests, then broader tests proportional to risk.
6. Push the topic branch and open a pull request against the correct Roots
   branch.

Use scoped, imperative commit subjects where practical, for example:

```
policy: make package limits configurable
wallet: preserve backup metadata
docs: explain Roots patch maintenance
```

Explain why the change exists and which invariant it preserves. Do not combine
behavior changes with unrelated formatting, code moves, generated files, or
vendored-subtree cleanup. Do not put `@` mentions in commit messages.

Journey 1: Change the Roots patch
---------------------------------

Use this journey to add or remove a Roots feature, change its configuration or
tests, fix a Roots-specific bug, or change what the patch carries across
current and future Core releases.

### 1. Define the semantic change

Describe the operator-visible behavior independently of the current file names
or APIs. Identify:

* the feature owner and its focused tests;
* whether the change affects policy, consensus, wallet, P2P, GUI, build, or
  release behavior;
* the oldest supported Roots line that needs it;
* which newer supported lines must also receive it; and
* whether `doc/roots-features.md`, option help, or release notes must change.

For imported behavior, record provenance but review and maintain the result as
Roots code. Do not make Bitcoin Knots an upstream dependency.

### 2. Start from the oldest affected line

Fix the oldest supported canonical line that needs the change. For example:

```sh
git fetch origin roots/29.4
git switch --create topic/29.4/<area> origin/roots/29.4
git merge-base --is-ancestor v29.4 HEAD
```

Do not begin product work on `main` and later guess which parts belong in the
release patch. Starting from the canonical line keeps the patch boundary and
release evidence reviewable.

### 3. Preserve the policy/consensus boundary

For any rule that can reject a consensus-valid transaction locally, test both
sides explicitly:

1. the configured node rejects it from admission, relay, or block-template
   selection; and
2. a consensus-valid block containing it is still accepted.

Treat changes under `src/consensus/`, `src/script/`, serialization, and block
validation as consensus-sensitive even when the intended change is policy-only.
Do not perform incidental cleanup there.

### 4. Make the semantic commit self-contained

A commit should carry enough evidence to survive a future Core port:

* implementation and stable configuration/help text;
* focused unit, functional, Qt, or fuzz coverage;
* comments explaining invariants rather than current mechanics;
* feature-catalog and release-note updates where user-visible; and
* provenance in the commit message when behavior was adapted from elsewhere.

This is the unit future maintainers will keep, drop, adapt, or split. Avoid a
single cross-feature commit whose meaning cannot be reviewed independently.

### 5. Review and carry the change forward

Open the PR against the canonical branch, not an integration branch. After the
change is accepted, carry it into every newer affected Roots line with
provenance:

```sh
git switch roots/<newer-core-version>
git cherry-pick -x <accepted-commit>
```

Use `-x` only when the new commit is a faithful cherry-pick. If newer Core APIs
require a material rewrite, explain the source commit and the deviation in the
new commit message instead of preserving a misleading cherry-pick identity.
Use `git range-diff` to show reviewers what changed between generations.

Never rewrite published release commits or tags. A maintenance release advances
its canonical branch from one tagged tip to another; it does not merge a newer
Core branch into an older Roots line.

Journey 2: Port Roots to a new Core release
-------------------------------------------

Use this journey when Bitcoin Roots moves from one Core base to another, for
example from Core 29.4 to Core 30.0. The goal is not to replay every old diff
blindly. The goal is to preserve the intended Roots behavior on the new Core
architecture with a reviewable semantic series.

### 1. Establish the new base

Fetch the tag from the official Bitcoin Core repository, inspect its peeled
commit, and create the canonical branch directly at that commit:

```sh
core_remote=<configured-bitcoin-core-remote>
core_tag=v<new-core-version>
git remote get-url "$core_remote"
git fetch "$core_remote" --tags
core_commit=$(git rev-parse "$core_tag^{commit}")
git show --no-patch --decorate "$core_commit"
git switch --create roots/<new-core-version> "$core_commit"
```

Do not merge the complete old `roots/<core-version>` branch. That would mix two
Core histories and make it difficult to distinguish upstream changes from the
Roots patch.

### 2. Inventory the old semantic series

List every Roots commit between the old Core tag and canonical tip. For each
commit, make one explicit decision:

| Decision | Meaning |
| --- | --- |
| Keep | The behavior is still needed and applies cleanly. |
| Drop | New Core already provides it, removed the need, or made it unsafe. |
| Adapt | The operator-visible behavior remains, but new Core APIs require a rewrite. |
| Split or combine | Core reorganized ownership, so a different commit boundary is clearer. |

Record the reason for every dropped or materially changed commit. Compare the
old and proposed series with:

```sh
git range-diff \
    v<old-core-version>..roots/<old-core-version> \
    v<new-core-version>..roots/<new-core-version>
```

### 3. Port commits semantically

Cherry-pick a commit with `-x` when it remains faithful. Resolve conflicts by
understanding the new Core ownership and invariants, not by bulk-selecting the
old or new side:

```sh
git cherry-pick -x <semantic-roots-commit>
# Inspect and resolve an identified conflict.
git diff --check
git cherry-pick --continue
```

When a change is no longer a true cherry-pick, create an adapted commit that
names the old commit and explains why the implementation changed. Abort a port
whose premise is wrong rather than hiding the conflict with an overwrite.

### 4. Re-prove the behavior on new Core

Passing compilation is not sufficient. For each retained feature:

* run its focused regression tests;
* verify option help, defaults, RPC/GUI exposure, and release documentation;
* repeat policy-rejection and block-acceptance tests where applicable;
* exercise database, upgrade, reindex, wallet reload, or P2P paths affected by
  the upstream change; and
* run the appropriate broad unit, functional, sanitizer, fuzz, and platform
  matrix before promotion.

The port PR must include the old and new Core commits, the old and new Roots
tips, the range-diff, the keep/drop/adapt inventory, and exact test results.

### 5. Review, promote, and release

Review the new canonical series independently of `main`. Integration branches
may combine work for CI, but they remain disposable. Once the canonical tip is
accepted, maintainers create an explicit promotion into `main` using the
procedure in `contrib/roots/README.md`.

Before assigning a permanent version, maintainers dispatch the release workflow
from `main` with the future tag name and the full canonical commit. After the
five platform packages and portable patch have been inspected, the annotated
Roots tag is created at exactly that canonical commit. Pushing the tag builds a
draft GitHub release for independent verification and publication.

Pull-request requirements
-------------------------

Every PR should state:

* Journey 1, Journey 2, or main-only repository work;
* target branch and upstream Core base;
* affected supported Roots lines;
* behavior changed and why;
* consensus and policy impact, including an explicit statement when there is
  no consensus change;
* provenance for imported or carried-forward behavior;
* commits kept, dropped, adapted, or split when porting;
* exact verification commands and results; and
* documentation and release-note impact.

PR titles should use a clear component prefix such as `policy:`, `wallet:`,
`net:`, `qt:`, `rpc:`, `build:`, `ci:`, `test:`, or `docs:`. Mark
incomplete work as a draft pull request rather than presenting it as
merge-ready.

Keep patch sets focused. Refactors, formatting changes, feature work, and Core
ports should be separate unless the dependency is unavoidable and explained.
Reviewers may ask for fixup commits to be squashed before final review. After a
rebase or material rewrite, provide a `git range-diff` so previous review can
be mapped to the new series.

Review and decision making
--------------------------

Anyone may review a pull request. Useful review describes both the commit
reviewed and the evidence gathered. For example, distinguish code inspection
from focused tests, broad tests, and manual operator testing.

Maintainers consider whether a change:

* has a clear operator or maintenance benefit;
* respects the project boundaries above;
* has appropriate unit, functional, Qt, fuzz, or platform coverage;
* is small enough to audit and carry to future Core releases;
* documents user-visible behavior and configuration; and
* has received review proportional to its risk.

Consensus-sensitive changes require substantially more discussion and review.
Changes to Bitcoin consensus are outside the ordinary Roots feature process and
must not be introduced as policy maintenance or as a routine Core port.

When reviewing a port, concentrate on semantic equivalence rather than textual
similarity. A clean cherry-pick can still be wrong if Core changed the
surrounding invariant; a rewritten commit can be correct when the range-diff,
tests, and explanation make the adaptation clear.

Release maintenance
-------------------

Apply a security or correctness fix first to the oldest supported Roots line
that needs it, then carry it forward with recorded provenance. Do not rewrite a
published tag or force-push a canonical branch other contributors may use.

The release workflow validates that a Roots tag targets the matching remote
`roots/<core-version>` tip and that the branch starts from the official Core
tag. It generates the portable patch directly from that Git range and verifies
that replay reproduces the tagged product tree outside `.github/**`. The patch
is an artifact, never a second source tree to edit or commit.

The exact rehearsal, tagging, patch-generation, signing, and draft-publication
commands are maintained in [`contrib/roots/README.md`](contrib/roots/README.md).
Do not copy those commands into a feature PR or bypass their validation with a
hand-built archive.

Communication and upstream work
-------------------------------

Discuss Roots-specific behavior and repository work in this repository's
issues and pull requests. For complicated or controversial Bitcoin consensus
or P2P protocol proposals, use the broader Bitcoin development forums before
writing an implementation.

If a change is generally suitable for Bitcoin Core, consider contributing it
upstream first. Follow Bitcoin Core's own contribution process for that work.
Once upstream accepts equivalent behavior, the next Roots port should normally
drop the redundant Roots commit and record that decision in the port inventory.

Translation changes
-------------------

Do not manually edit generated `src/qt/locale/bitcoin_*.ts` files. Follow the
[translation process](doc/translation_process.md) and regenerate translations
only through the documented tooling.

Copyright
---------

By contributing to this repository, you agree to license your work under the
MIT license unless specified otherwise in `contrib/debian/copyright` or at the
top of the file itself. Work for which you are not the original author must
retain its license, attribution, and source.
