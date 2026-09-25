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

### Pull-request selection

`ci/change-classifier.py` reads the complete PR file list using
`ci/change-classifier-policy.json`. Mixed changes receive the union of their
coverage. Incomplete, truncated, empty, ambiguous, and unknown inputs fail open
to broad coverage.

| Change | Automatic coverage in addition to lint |
| --- | --- |
| Documentation only | No build job. |
| Branding or GUI | GUI/resource build and Qt tests. |
| Wallet | Compatibility, sanitizers, and platform smoke tests. |
| General C/C++ | Sanitizers, ARM32, Windows, and both native macOS architectures. |
| Critical, build, CI, or unknown | Broad coverage, including GUI and previous-release compatibility. |

The stable `required result` job is the branch-protection check to require. It
fails if classification, lint, or any selected job fails.

Maintainers can add coverage with `ci:sanitizers`, `ci:fuzz`,
`ci:compat`, `ci:platforms`, or `ci:full` PR labels. Labels add tests;
they never suppress automatic coverage.

### Nightly assurance and caches

Nightly assurance retains TSan, MSan, clang-tidy/dependency checks, i686 Debug,
extended functional tests, previous-release compatibility, and native Linux,
macOS, and Windows fuzz corpora. A scheduled run skips expensive jobs only
after the same commit has already completed successfully; failures retry and
unavailable history fails open.

GitHub-hosted compiler and dependency caches are read by PRs and updated only
from the repository's default branch. Cache misses make jobs slower but do not
change which tests run.
