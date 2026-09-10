# CI Scripts

This directory contains scripts for each build step in each build stage.

## Running a Stage Locally

Be aware that the tests will be built and run in-place, so please run at your own risk.
If the repository is not a fresh git clone, you might have to clean files from previous builds or test runs first.

The ci needs to perform various sysadmin tasks such as installing packages or writing to the user's home directory.
While it should be fine to run
the ci system locally on your development box, the ci scripts can generally be assumed to have received less review and
testing compared to other parts of the codebase. If you want to keep the work tree clean, you might want to run the ci
system in a virtual machine with a Linux operating system of your choice.

To allow for a wide range of tested environments, but also ensure reproducibility to some extent, the test stage
requires `bash`, `docker`, and `python3` to be installed. To run on different architectures than the host `qemu` is also required. To install all requirements on Ubuntu, run

```
sudo apt install bash docker.io python3 qemu-user-static
```

It is recommended to run the ci system in a clean env. To run the test stage
with a specific configuration,

```
env -i HOME="$HOME" PATH="$PATH" USER="$USER" bash -c 'FILE_ENV="./ci/test/00_setup_env_arm.sh" ./ci/test_run_all.sh'
```

## Configurations

The test files (`FILE_ENV`) are constructed to test a wide range of
configurations, rather than a single pass/fail. This helps to catch build
failures and logic errors that present on platforms other than the ones the
author has tested.

Some builders use the dependency-generator in `./depends`, rather than using
the system package manager to install build dependencies. This guarantees that
the tester is using the same versions as the release builds, which also use
`./depends`.

It is also possible to force a specific configuration without modifying the
file. For example,

```
env -i HOME="$HOME" PATH="$PATH" USER="$USER" bash -c 'MAKEJOBS="-j1" FILE_ENV="./ci/test/00_setup_env_arm.sh" ./ci/test_run_all.sh'
```

The files starting with `0n` (`n` greater than 0) are the scripts that are run
in order.

## GitHub Actions policy

GitHub Actions has four deliberately separate workflows:

| Workflow | Events | Purpose |
| --- | --- | --- |
| `.github/workflows/ci.yml` | pull request | Fast, stable required gate plus path-selected PR assurance. |
| `.github/workflows/nightly.yml` | scheduled, manual dispatch, reusable call | Expensive assurance that does not need to block every ordinary PR. |
| `.github/workflows/release.yml` | push to `main`, `v*` tags, manual dispatch | Produce CI artifacts; tagged runs also create signed draft GitHub Releases. |
| `.github/workflows/create-release.yml` | manual dispatch | Create an annotated version tag from `main` and start its release build. |

The classifier and build/test jobs check out immutable PR-head SHAs. The lint
job intentionally checks out GitHub's synthetic PR merge with full history so
commit-range linting examines the merge result. The workflow never uses
`pull_request_target`; it has read-only `actions`, `contents`, and
`pull-requests` permissions and does not expose repository secrets to
pull-request code.
`required result` is the branch-protection check to require: it always runs and
fails when `classify`, `lint`, or any selected job fails or is cancelled.
Intentionally unselected jobs are not failures.

### Pull-request path selection

The repository-owned `ci/change-classifier.py` reads the complete PR file list
and the policy in `ci/change-classifier-policy.json`. A mixed PR receives the
union of selected coverage. An incomplete, truncated, ambiguous, empty, or
unknown file list fails open to broad coverage; do not replace this with
workflow-level `paths` filters, because a filtered required check can remain
pending forever.

| Changed path category | Examples | Automatic PR coverage in addition to lint |
| --- | --- | --- |
| Documentation only | `README.md`, ordinary Markdown, `doc/**` Markdown | Always-on lint only; no build or test job. |
| Branding | `src/qt/res/**`, `share/pixmaps/**` images | GUI/resource build and Qt tests. |
| GUI | `src/qt/**`, desktop/appdata files | GUI/resource build and Qt tests. |
| Wallet | `src/wallet/**` | Wallet compatibility plus sanitizers, fuzz, and platform smoke coverage. |
| General C/C++ | other C/C++ source or header files | ASan/LSan/UBSan/integer, native Linux fuzz corpus, ARM32 tests, and Windows x86_64 plus macOS x86_64/arm64 smoke builds. |
| Critical | consensus, validation, script, primitives, serialization, networking, mempool, RPC, or shared test infrastructure | Broad coverage. |
| Build or unknown | CMake, `depends`, `ci`, `.github`, or an unclassified path | Broad coverage. |

Broad coverage selects the GUI/resource build, sanitizer job, native Linux
fuzz corpus, previous-release compatibility, ARM32 tests, and native Windows
and macOS smoke builds. This is intentionally conservative for CI and build
changes as well as consensus-adjacent changes.

### Maintainer opt-ins

Apply these PR labels when more assurance is useful before merge. Labels are
additive: none can disable automatic coverage, they persist across
`synchronize` events, and adding or removing a label reruns classification.

| Label | Adds PR coverage |
| --- | --- |
| `ci:sanitizers` | Reusable nightly TSan and MSan group, in addition to PR sanitizer coverage. |
| `ci:fuzz` | Reusable nightly native macOS and Windows full fuzz-corpus group. |
| `ci:compat` | Previous-release compatibility job. |
| `ci:platforms` | Reusable nightly i686 Debug job, plus routine PR platform selection. |
| `ci:full` | Every PR-addressable group, including the reusable nightly assurance matrix. |

The labels request coverage; they are not a substitute for reviewing which
automatic jobs were selected. The `classify changes` log records the machine
readable decision and is the first place to inspect when the selected set is
unexpected.

### PR jobs and expected bounds

| Job | Runner | Timeout | Purpose |
| --- | --- | ---: | --- |
| `classify changes` / `required result` | Ubuntu 24.04 | 10 min each | Fail-open selection and stable branch-protection result. |
| `lint` | Ubuntu 24.04 | 20 min | Repository, documentation, file, and link checks for every PR. |
| `GUI and resources` | Ubuntu 24.04 | 120 min | Qt/resource build and tests for GUI or branding changes. |
| `ASan, LSan, UBSan, integer, wallet, GUI, and USDT` | Ubuntu 24.04 | 120 min | Native sanitizer, wallet, GUI, and tracing-probe coverage. |
| `native Linux libFuzzer corpus` | Ubuntu 24.04 | 240 min | Linux fuzz corpus coverage. |
| `previous releases compatibility` | Ubuntu 24.04 | 120 min | Compatibility with prior releases. |
| `32-bit ARM unit tests` | Ubuntu 24.04 ARM | 120 min | Routine 32-bit architecture signal. |
| Windows x86_64 / macOS x86_64 and arm64 smoke | Windows Server 2022 / macOS 15 native | 180 min | Production-like native build and smoke coverage. |

`USDT` here means *User Statically-Defined Tracing*: it covers optional
tracing probes, not the Tether currency.

### Nightly and manual assurance

The scheduled workflow starts at 02:23 UTC and uses `cancel-in-progress: false`
so an already running assurance run is not discarded. Its lightweight gate
looks up the current and immediately preceding scheduled runs of the same
workflow. It skips expensive jobs only when the preceding scheduled run
completed successfully at the exact same commit.

| Situation | Gate decision |
| --- | --- |
| Successful prior scheduled run at this SHA | Skip expensive jobs (`unchanged-success`). |
| New commit, no history, prior failure/cancellation, malformed/API-unavailable history | Run assurance (fail open). |
| Manual dispatch or reusable PR call | Run assurance; no history-based skip. |

Thus a failed nightly retries at the same SHA, while an unchanged SHA after a
successful nightly consumes only the gate job. `workflow_dispatch` provides
`all`, `sanitizers`, `fuzz`, `platforms`, `tsan`, `msan`, `tidy`, `i686`,
`previous-releases`, `extended-functional`, `macos-fuzz`, and `windows-fuzz`
groups. The reusable workflow is what the PR labels call, always checking out
the requested immutable PR SHA.

The all-nightly matrix retains TSan, MSan, clang-tidy/dependency-boundary
checks, i686 Debug, extended previous-release compatibility, extended
functional tests, and full native macOS and Windows fuzz corpora. Sanitizer
runtimes remain separate.

### Release artifacts are not test artifacts

`release.yml` is isolated from adaptive PR selection. On `main` and on manual
dispatch it builds and uploads unsigned 30-day CI artifacts for Linux x86_64,
Linux aarch64, Windows x86_64, macOS x86_64, and macOS arm64. The macOS jobs
verify the runner architecture, so x86_64 is native rather than cross-built.
CentOS GUI and no-wallet/libbitcoinkernel jobs are configuration coverage, not
additional promoted release artifacts.

A pushed `v*` tag runs the same build matrix and then creates a draft GitHub
Release. The tag must point to a commit contained in `main`. The publishing job
downloads exactly one package from each of the five artifact sets, generates a
single `SHA512SUMS` manifest, signs it with the secret
`BITCOIN_ROOTS_GPG_SK`, verifies the detached signature, and uploads the five
packages plus `SHA512SUMS` and `SHA512SUMS.asc`. Release-candidate tags whose
names contain `-rc` are also marked as prereleases. A maintainer must inspect
and publish the draft release.

Maintainers can create the tag without a local Git checkout from the repository
**Actions** tab. Select **Create release**, click **Run workflow**, leave the
branch set to `main`, enter the complete `v*` tag, select the confirmation
checkbox, and run it. The workflow validates the tag, refuses an existing tag,
creates an annotated tag at the current `main` HEAD, and explicitly dispatches
`release.yml` at that tag. The explicit dispatch is required because events
created with the workflow's `GITHUB_TOKEN` do not recursively start ordinary
push workflows.

The committed release public key is
`contrib/release/bitcoin-roots-release-key.asc`, with fingerprint
`5EAD D53F 2CD1 F0B7 AEEE 920D 25FC 5C29 CD52 8E32`. The public key is also
included as comments in `SHA512SUMS`. After downloading all release assets,
verify them with:

```bash
gpg --import contrib/release/bitcoin-roots-release-key.asc
gpg --verify SHA512SUMS.asc SHA512SUMS
sha512sum -c SHA512SUMS
```

These artifacts are not reproducible Guix builds and have no Guix attestations;
do not represent them as independently reproducible binaries.

The default branch needs three operational confirmations after this change lands:

1. Observe one changed-SHA scheduled or manual nightly and a later
   unchanged-SHA scheduled run, confirming both the assurance and gate-only
   paths.
2. Observe one `main` release run and verify the five artifact targets across
   three OS families: `bitcoin-roots-linux-x86_64`,
   `bitcoin-roots-linux-aarch64`, `bitcoin-roots-windows-x86_64`,
   `bitcoin-roots-macos-x86_64`, and `bitcoin-roots-macos-arm64`, plus the two
   configuration-coverage jobs. PR checks cannot fully exercise default-branch
   schedule semantics or produce authoritative release artifacts.
3. For the first release tag, verify that the tag-only publishing job creates a
   draft containing all five packages, `SHA512SUMS`, and `SHA512SUMS.asc`, then
   verify the signature and every checksum before publishing the draft.

### Evidence checklist

| Evidence | Status at PR review | Where to inspect |
| --- | --- | --- |
| Classifier and selected PR job graph | Required before merge | The current pull request's `classify changes` and `required result`. |
| Selected PR builds, tests, and lint | Required before merge | Checks for the current pull-request head. |
| Changed-SHA scheduled/manual nightly | Post-merge main follow-up | `Nightly assurance` run and gate reason. |
| Unchanged-SHA scheduled gate-only nightly | Post-merge main follow-up | Later scheduled `Nightly assurance` run with `unchanged-success`. |
| Five release artifacts across three OS families | Post-merge main follow-up | `Release Artifacts` run on `main`; verify all five names and retention. |
| Signed draft release | First `v*` tag follow-up | Verify the seven draft assets, GPG signature, and all SHA-512 checksums before publishing. |

Do not report the post-merge rows as verified from a PR: GitHub schedules and
default-branch artifact production cannot be fully exercised from a PR branch.

### Caches, concurrency, and troubleshooting

All jobs use GitHub-hosted runners; no external runner service is required.
The PR workflow cancels superseded runs for the same PR, while nightly runs do
not cancel one another. Cache keys are separated by job/OS, architecture,
compiler/toolset, configuration, and dependency inputs where applicable.
PR-originated and reusable PR calls can restore caches but cannot save them.
Cache saves are restricted to trusted default-branch contexts: push-to-main
release jobs, and, where a nightly cache is saved, scheduled or manual runs on
the default branch. Do not broaden those save conditions or add secrets to PR
jobs.

Compiler-object caching is owned by `hendrikmuhs/ccache-action@v1.2.24`, not the
generic dependency-cache composite actions. Linux container jobs configure it
after the environment has exported `CCACHE_DIR` and before Docker starts; the
directory is then bind-mounted into the build container. Native macOS jobs set
the same directory after installing Homebrew `ccache`. Linux families use the
runner OS/architecture, configured `CONTAINER_NAME`, and depends fingerprint;
this lets a PR and a compatible trusted nightly use the same family. Each Linux
job first tries that exact family, then its same OS/architecture/container
family without the depends fingerprint. Because ccache-action adds the `ccache-`
prefix itself, that second fallback also matches the prior generic ccache
namespace during migration. These broader fallbacks remain compatible because
ccache keys each compiled object by compiler invocation and included content.
Native macOS release-like smoke uses one family in PR and release workflows, while
native macOS fuzz uses a separate shared nightly/reusable family. The action
appends its timestamp separator itself, so its `key` and `restore-keys` inputs
must name the exact family without a trailing dash. PR jobs and reusable calls
from PRs are restore-only.

Linux jobs request ccache-action's checksummed binary installation, avoiding a
host package-manager install before the container starts. Native macOS jobs
set `install: no` because the immediately preceding Homebrew step installs
ccache.

Today, trusted writers seed the previous-release Linux family through nightly,
the macOS release-like smoke families through `main` release runs, and the
macOS fuzz family through scheduled/manual nightly runs. PR GUI, ASan, native
Linux fuzz, and ARM32 families are intentionally restore-only but cold until a
matching trusted build is added; do not claim cache hits for those families.

Windows deliberately retains only its vcpkg tool and binary caches. The
ccache-action upstream documentation marks Windows support as provisional and
recommends sccache, while this repository's Visual Studio/MSBuild generator
does not use CMake's compiler-launcher mechanism (which applies to Makefile
and Ninja generators). Do not add a Windows compiler cache without a dedicated
sccache design and a separately validated generator/toolchain change.

When a PR job is unexpected or fails:

1. Inspect `classify changes` for file-list completeness, categories, labels,
   and selected outputs. An API/list anomaly should intentionally select broad
   coverage.
2. Inspect the named job, then `required result`; the latter identifies any
   selected job that did not finish successfully.
3. For lint failures, use the generated lint report and reproduce only the
   narrow relevant lint locally if needed. Do not weaken the required-result
   gate to hide a failed selected job.
4. For platform failures, keep the runner/toolchain/cache identity from the
   job log when filing an issue. Clear or change only the matching cache key;
   never share Windows, macOS, sanitizer, or release caches indiscriminately.
