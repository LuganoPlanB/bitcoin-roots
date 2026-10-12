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

Bitcoin Roots uses a smaller, risk-selected pull-request matrix rather than
running every Bitcoin Core CI configuration on every change:

| Workflow | Events | Purpose |
| --- | --- | --- |
| `.github/workflows/ci.yml` | pull request | Required lint plus path-selected build and test coverage. |
| `.github/workflows/nightly.yml` | schedule, manual dispatch, reusable call | Expensive sanitizer, fuzz, compatibility, and platform assurance. |
| `.github/workflows/release.yml` | Roots release tags, manual dispatch | Validate a canonical release tag and produce the five release packages and portable patch. |

These repository workflows are kept in every canonical Roots branch and release
tag so a PR tests the code it proposes. They are hosting automation, however,
so `ci/release/create-patch-series.sh` excludes `.github/**` from the
downloadable patch applied on top of Bitcoin Core. Source tests and CI support
under `test/**`, `src/**/test/**`, and `ci/**` remain in that patch.

### Fast PR and development coverage

`ci/change-classifier.py` retains additive categories and labels. Complete file
lists select affected groups; malformed, truncated, empty and unknown input
selects broad **fast** coverage. Lint/classifier checks always run. The stable
`required result` check rejects failure, cancellation or skipping of a selected
job. Verified pure promotions may reuse exact successful canonical CI; explicit
`ci:*` labels still request fresh coverage.

| Change | Automatic fast coverage beyond lint |
| --- | --- |
| Documentation only | No compiled job. |
| Branding or GUI | Actual Qt/resource build and GUI unit tests. |
| Wallet, general C/C++, critical/build/unknown | Headless ASan/UBSan unit tests, affected Roots functional regressions, bounded libFuzzer smoke. |

Fast functional selection and fuzz budgets are implemented by
`ci/test/roots-regressions.py`, invoked through the existing
`ci/test_run_all.sh` / `ci/test/03_test_script.sh` entry points. Known wallet,
network, runtime, policy and mining paths select their acceptance groups;
mixed paths take the union. Unknown input selects all 16 Roots functional
regressions and 9 fuzz targets. Fast functional execution has a 900-second
process-group timeout. Each selected libFuzzer target has a 5-second campaign,
5-second per-input limit and 15-second outer timeout; input counts are reported
results, never a campaign quota. Fixed seed 1 makes smoke commands reproducible.
Fast fuzz does not download or replay the full external corpus.

Developer commands using an already configured/built profile:

```sh
python3 ci/test/roots-regressions.py plan --cadence fast \
  --files-json '["src/wallet/spend.cpp"]'
python3 ci/test/roots-regressions.py functional --cadence fast \
  --files-json '["src/wallet/spend.cpp"]' --build-dir build \
  --artifacts /tmp/roots-fast-functional -- -j2 --quiet
python3 ci/test/roots-regressions.py fuzz --cadence fast \
  --files-json '["src/policy/policy.cpp"]' --build-dir build_fuzz \
  --artifacts /tmp/roots-fast-fuzz
python3 -m unittest discover -s ci/test
```

Fuzz execution requires the native libFuzzer build (`--preset libfuzzer` or
`00_setup_env_native_fuzz.sh`), not a plain standalone corpus-replay binary.
Selection/result JSON and per-command logs are temporary failure artifacts,
not a second source of configuration or a release-state ledger. Timed-out owned
process groups are terminated; unowned node processes are untouched.

Hosted runners set `ROOTS_ARTIFACTS_DIR` to an absolute host directory and
bind-mount it at the same path inside the test container. The regression
runner writes per-target logs, result JSON and crash artifacts there; workflow
uploads use that exact directory. Local runs without the setting retain the
scratch-directory fallback. Failed commands also print the exit status, full
log path and at most 12 tail lines of 240 characters from the last 4 KiB.
The artifact keeps complete output; the bounded console tail may omit earlier
diagnostics. Ordinary mock-runner tests cover transport and retention without
executing a fuzz campaign.

### Nightly/manual assurance and caches

`nightly.yml` keeps cron `23 2 * * *` (UTC), `workflow_dispatch` and explicit
label-triggered reusable calls. Ordinary PRs do not run the broad platform
matrix. Nightly/manual coverage includes full ASan/USDT/Qt, TSan, MSan,
clang-tidy/dependency boundaries, i686 no-IPC, ARM32, native kernel/chainstate,
previous-release extended compatibility, extended functional tests, Windows and
both macOS architecture smokes, and native Linux/macOS/Windows full fuzz corpora.
Selected empty matrices are skipped before strategy expansion. The schedule
skips only after the same SHA's preceding completed scheduled run succeeded;
failed or unavailable/malformed history retries conservatively.

| Execution | Explicit budget |
| --- | --- |
| PR classifier/result and lint | 10 / 20 minutes. |
| PR native ASan and fuzz builds plus bounded regressions | 60 minutes each. |
| PR GUI build/unit checks | 120 minutes. |
| Nightly ASan | 180 minutes; full applicable native suite. |
| Nightly TSan/MSan/i686/Linux full corpus | 240 minutes each. |
| Previous-release extended compatibility | 300 minutes. |
| Extended functional/tidy/native platform fuzz | 360 minutes. |
| Nightly selected Roots fuzz campaigns | 60 seconds/target, 75-second outer timeout, after full Linux corpus replay. |

Direct `--cadence nightly` functional execution has a 7200-second outer budget;
full/extended hosted suites use their existing native runners and job budgets.
Labels `ci:sanitizers`, `ci:fuzz`, `ci:platforms` and `ci:full` explicitly request
reusable heavy coverage; `ci:compat` requests fresh fast compatibility
regressions. Manual suite selection can request previous-release/extended
coverage independently. Failure artifacts retain native test logs, CTest
output and fuzz crashes/results. Job timeouts are bounds, not measured cold
build predictions. Local selector/fixture validation does not establish a
hosted job pass or Windows/macOS/32-bit execution.

Compiler/dependency caches remain read-only to PR code; trusted default-branch
nightly/manual compiler cache saves retain the source behavior. Native v31
cache-action paths, compiler profiles, no-IPC i686 profile and VS2026 static
preset replace obsolete v30 names. Windows GUI workflows request the optional
vcpkg `miniupnpc` feature when enabling `WITH_MINIUPNPC`; fuzz builds explicitly
disable ZMQ to match their wallet-only dependency manifest. Release signing
secrets never enter these test containers.
