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

For some sanitizer builds, the kernel's address-space layout randomization
(ASLR) entropy can cause sanitizer shadow memory mappings to fail. When running
the CI locally you may need to reduce that entropy by running:

```
sudo sysctl -w vm.mmap_rnd_bits=28
```

To run a test that requires emulating a CPU architecture different from the
host, we may rely on the container environment recognizing foreign executables
and automatically running them using `qemu`. The following sets us up to do so
(also works for `podman`):

```
docker run --rm --privileged docker.io/multiarch/qemu-user-static --reset -p yes
```

It is recommended to run the CI system in a clean environment. The `env -i`
command below ensures that *only* specified environment variables are propagated
into the local CI.
To run the test stage with a specific configuration:

```
env -i HOME="$HOME" PATH="$PATH" USER="$USER" FILE_ENV="./ci/test/00_setup_env_arm.sh" ./ci/test_run_all.sh
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
env -i HOME="$HOME" PATH="$PATH" USER="$USER" MAKEJOBS="-j1" FILE_ENV="./ci/test/00_setup_env_arm.sh" ./ci/test_run_all.sh
```

The files starting with `0n` (`n` greater than 0) are the scripts that are run
in order.

## Cache

In order to avoid rebuilding all dependencies for each build, the binaries are
cached and reused when possible. Changes in the dependency-generator will
trigger cache-invalidation and rebuilds as necessary.

## Roots GitHub Actions policy

The v32.1 candidate keeps path-selected PR coverage and a separate expensive
assurance cadence. `.github/workflows/ci.yml` runs on pull requests with a
stable `required result` gate. Classifier, maintenance fixtures and lint always
run; malformed, incomplete, empty or unknown path input requests broad fast
coverage. Selected failed, cancelled or skipped jobs fail the result gate.
Exact successful canonical PR CI may be reused only after the promotion
verifier checks current `roots/<core-version>` ancestry and product parity;
`promote/32.1-*` is covered by its local fixtures. Explicit `ci:*` labels
request fresh coverage. A fixture pass is not successful hosted CI.

| Change | Automatic fast coverage beyond lint |
| --- | --- |
| Documentation | No compiled job. |
| Branding or GUI | Qt6/resources build and GUI unit tests. |
| Wallet, C/C++, critical, build or unknown | ASan/UBSan native suite, affected Roots functional regressions and bounded libFuzzer smoke. |

`ci/test/roots-regressions.py` owns the additive functional/fuzz selection.
It retains roots.6 address-key-export coverage and adds current graph/RBF,
compact-reconstruction cache, native HTTP and IPC-mining regressions. Unknown
input selects all 28 functional scripts and 9 fuzz targets. Changes to the
selected test scripts select their owners too. Fast functional execution has
a 900-second process-group limit. Each fast libFuzzer campaign runs for 5
seconds, with a 5-second per-input limit, seed 1 and 15-second outer limit.
Input counts are measured results, not a quota. Fast fuzz uses current native
libFuzzer binaries and does not download the external full corpus.

```sh
python3 ci/test/roots-regressions.py plan --cadence fast \
  --files-json '["src/ipc/capnp/mining.capnp"]'
python3 ci/test/roots-regressions.py functional --cadence fast \
  --files-json '["src/wallet/spend.cpp"]' --build-dir build \
  --artifacts /tmp/roots-functional -- -j2 --quiet
python3 ci/test/roots-regressions.py fuzz --cadence fast \
  --files-json '["src/policy/policy.cpp"]' --build-dir build_fuzz \
  --artifacts /tmp/roots-fuzz
python3 -m unittest discover -s ci/test
```

Fast/native GUI profiles require current Qt6, SQLite and Cap'n Proto/pycapnp
where appropriate; the fast headless profile enables IPC so its mining test
can execute. v32 uses native HTTP and has no libevent dependency. Keep native
v32 compiler/dependency profiles, Unicode/space build paths, Nix Windows cross
support and per-profile container networks. Windows smoke requests optional
MiniUPnPc explicitly; Windows fuzz disables ZMQ to match its wallet manifest.

### Shared test inputs

PR and nightly workflows acquire `bitcoin-core/qa-assets` once through
`actions/checkout`, at the revision pinned in `test/download_utils.py`. The
preparation job checks the unit-vector SHA256, records the source revision and
file hashes, retains qa-assets' license, and shares separate unit/fuzz inputs
through Actions artifacts.
Nested nightly calls reuse their caller's prepared inputs; full fuzz data is
requested only for suites that consume it. Fast fuzz smoke stays self-contained.

Every consumer verifies its prepared files before use. Linux containers receive
a read-only input mount; native macOS and Windows jobs use the same handoff.
Fuzz runners receive a writable per-job copy so they can create directories for
new targets and retain newly discovered inputs without changing the shared data.
Preparation cleans interrupted copies and reuses unchanged completed copies;
choose a fresh working directory to preserve a corpus changed by a prior run.
`ROOTS_QA_ASSETS_DIR` makes missing or corrupt prepared inputs fatal instead of
triggering another external download. Standalone shell and Windows helpers keep
their fallback acquisition through `test/download_utils.py`; existing valid unit
vectors are reused. Update the pinned revision and unit-vector SHA256 together
when deliberately updating inputs. Sharing inputs reduces duplicate requests;
initial acquisition still depends on GitHub being available.

### Diagnostics and assurance cadence

Hosted container jobs set an absolute `ROOTS_ARTIFACTS_DIR` and bind-mount the
same path on host and guest. The CI-script wrapper records complete output,
exact argv, timeout and exit in `ci.log`/`ci-results.json`. Failed commands
print the exact command, target, log path and at most 12 tail lines of 240
characters from the last 4 KiB; earlier diagnostics remain in the full log.
Functional/fuzz result JSON and per-target logs/crash files use this directory.
The failure collector copies CMake/CTest and node/framework diagnostics from
the actual configured build/scratch paths, including Unicode/space paths.
It excludes configs, wallets, keys and database files, including numeric
LevelDB `.log` journals. Uploads use these diagnostic outputs, not raw datadirs.
Native Windows/macOS jobs retain separate configure/build/test logs and results.
Local runs without the variable keep the native console/scratch fallback.

Container wrappers stop five minutes before their declared job budget where
configured, leaving time for diagnostics and uploads; manual jobs default to
a 355-minute script bound. Native platform wrappers have explicit command
bounds. A hosted runner cancellation can still prevent upload: current local
fixtures prove command/timeout/selection and mocked bind transport only, not
GitHub service behavior or platform execution.

`nightly.yml` keeps cron `23 2 * * *` UTC, manual suite selection and reusable
label requests. Scheduled work skips only after a validated immediate completed
successful scheduled run at the same SHA; uncertain history retries. Long
coverage includes ASan/USDT/Qt6, TSan, MSan, tidy/dependency boundaries, i686
no-IPC, ARM32, native kernel/chainstate, previous-release extended tests,
extended functional tests, explicit system LevelDB/secp256k1, Windows and both
macOS architectures, plus native Linux/macOS/Windows full fuzz corpus replay.
System-library discovery is required in its profile: missing libraries/modules
fail configure rather than silently using embedded copies. Nightly Linux adds
selected 60-second libFuzzer campaigns (75-second outer limits) after full
corpus replay. Direct nightly functional selection has a 7200-second bound.

| Jobs | Budget |
| --- | --- |
| PR classifier/result; lint | 10; 20 minutes |
| PR ASan/fuzz; GUI | 60; 120 minutes |
| Nightly ASan | 180 minutes |
| TSan/MSan/i686/system libraries/Linux full corpus | 240 minutes |
| Previous-release extended tests | 300 minutes |
| Tidy/extended functional/native platform fuzz | 360 minutes |

Labels `ci:sanitizers`, `ci:fuzz`, `ci:platforms` and `ci:full` request heavy
reusable assurance; `ci:compat` requests fresh fast compatibility regressions.
The main-only differential compatibility harness was guarded for Core 30.3.
Its v32 source/binary qualification and workflow import remain owned by the
whole-candidate compatibility gate; ordinary previous-release tests and local
maintenance fixtures do not establish that qualification. No exact-SHA hosted,
Windows/macOS, system-library or long sanitizer/fuzz success is implied here.
Release/source/export tooling is qualified separately before release use.

These policies Fold roots.6 CI/diagnostic repairs (notably
`9aaf62146fae1a89d65121468633d5c1701699e9`,
`7cd11e298ec99e9bfc497f0fe3b9a18ab48a61a7` and
`b8c98a129f141ac5ba077c33b473120f0cd2ece4`) into current Core v32.1 interfaces.
The pinned main selector omitted the later address-export regression, so the
released source retains ownership. Preserve upstream and Plan-B credits.

## Configuring upstream-compatible cache providers

### Primary repository

To configure the primary repository, follow these steps:

1. Register with [WarpBuild](https://www.warpbuild.com/) and purchase runners.
2. Install the WarpBuild GitHub app against the GitHub organization.
3. Enable organisation-level runners to be used in public repositories:
   1. `Org settings -> Actions -> Runner Groups -> Default -> Allow public repos`
4. Permit the following actions to run:
   1. actions/cache/restore@\*
   1. actions/cache/save@\*
   1. actions/github-script@\*
   1. docker/setup-buildx-action@\*
   1. warpbuilds/cache/restore@\*
   1. warpbuilds/cache/save@\*

### Forked repositories

When used in a fork the CI will run on GitHub's free hosted runners by default.
In this case, GitHub's cache size limitations may cause caches to be frequently evicted and missed, but the workflows will run (slowly).

It is also possible to use your own WarpBuild Runners in your own fork by replacing the references to `bitcoin/bitcoin` in `../.github/workflows/ci.yml` with the name of your fork (e.g. `your-org/bitcoin`).
NB that WarpBuild Runners only work at an organisation level, therefore in order to use your own WarpBuild Runners, *the fork must be within your own organisation*.

Copyright (C) 2026 Plan-B Foundation
