# AGENTS.md

This file is the operational guide for AI agents working in this repository. It
applies to the whole tree unless a more specific `AGENTS.md` is added below it.

## Project identity and non-negotiable invariants

This repository is **Bitcoin Roots**, not an unmodified Bitcoin Core or Bitcoin
Knots checkout. It is a C++20 full-node, wallet, GUI, and utility suite based on
Bitcoin Core with selected policy features inherited from Bitcoin Knots
`29.3.knots20260507`.

Read `README.md` before changing behavior. In particular:

- Bitcoin Roots stays compatible with Bitcoin Core consensus.
- Conservative transaction relay and mempool policy is intentional and
  configurable. A transaction rejected by local policy can still be valid by
  consensus; blocks containing it must remain acceptable.
- This revision deliberately does **not** enforce RDTS/BIP110 consensus rules.
  Do not add, imply, or silently enable those rules, and do not confuse the
  startup warning/consent code with consensus enforcement.
- Consensus, validation, serialization, wallet, networking, and cryptographic
  changes are security-critical. Make the smallest justified change and add
  regression coverage. Never “clean up” consensus-adjacent code incidentally.

Much of `doc/`, comments, scripts, and test output still uses the inherited
names “Bitcoin Core” or “Bitcoin Knots.” Do not mass-rebrand this text. Determine
whether a name is a historical/upstream reference, a protocol compatibility
identifier, or user-facing Roots branding before changing it. The current
`doc/release-notes.md` is inherited and contains RDTS migration language that is
not an authoritative statement of Roots policy. For fork identity and policy,
prefer, in order: current source behavior and tests, root `README.md`, current
build configuration, then inherited documentation.

## Repository map

- `src/`: production C++ and internal libraries.
  - `bitcoind.cpp`, `bitcoin-cli.cpp`, `bitcoin-tx.cpp`, `bitcoin-util.cpp`, and
    `bitcoin-wallet.cpp` are command-line entry points.
  - `qt/` contains the `bitcoin-qt` GUI and GUI tests.
  - `node/` owns P2P/RPC server orchestration and node-specific behavior.
  - `kernel/` is the validation/consensus engine being separated from the node.
  - `consensus/`, `script/`, `primitives/`, and much of `validation.cpp` are
    consensus-sensitive.
  - `policy/` and mempool code define local admission, relay, and mining policy;
    policy must not leak into block-validity decisions.
  - `wallet/` contains wallet databases, coin selection, signing, and wallet RPC.
  - `rpc/` contains non-wallet RPC implementations; `interfaces/` contains
    abstract boundaries between node, wallet, and GUI.
  - `common/` is higher-level shared application code; `util/` is lower-level,
    broadly reusable platform/support code; `crypto/` is standalone primitives.
  - `index/`, `zmq/`, `ipc/`, `init/`, and `logging/` are feature-focused areas.
  - `test/`, `wallet/test/`, and `qt/test/` contain C++ unit tests.
- `test/functional/`: Python integration tests that launch isolated regtest
  nodes and drive RPC/P2P behavior. `test_framework/` is their shared harness.
- `test/lint/`: repository lint checks; its runner is a Rust crate.
- `test/fuzz/` and `src/test/fuzz/`: fuzz runner and C++ fuzz targets.
- `cmake/`, root `CMakeLists.txt`, and `src/**/CMakeLists.txt`: the primary build
  system. This tree no longer uses Autotools for the main build.
- `depends/`: reproducible pinned third-party dependency builder, used for
  release and cross builds.
- `ci/` and `.github/workflows/`: canonical CI configurations. CI intentionally
  exercises different feature/compiler/platform combinations.
- `contrib/`: developer, packaging, tracing, deployment, and maintenance tools.
- `doc/`: build, runtime, interface, design, and contributor documentation.
- `share/`: example configuration, desktop integration, icons, and RPC auth tool.

Embedded `src/secp256k1`, `src/leveldb`, `src/crc32c`, `src/crypto/ctaes`, and
`src/minisketch` are maintained as upstream subtrees. Avoid direct cosmetic or
cross-cutting edits there. Fix upstream when possible and use
`test/lint/git-subtree-check.sh` when intentionally updating a subtree.

### Library boundaries

Follow `doc/design/libraries.md`. The important dependency direction is:

- `crypto` has no internal dependencies.
- `consensus` depends only on `crypto`.
- `util` depends only on `crypto` and must remain suitable for kernel users.
- `common` depends only on `util`, `consensus`, and `crypto`.
- `kernel` depends only on `util`, `consensus`, and `crypto`; internally only
  `node` should depend on `kernel`.
- Node, wallet, and GUI implementations must not link directly to one another.
  Communicate through abstract types in `src/interfaces/`.

When adding production C++ files, add them to the appropriate library target in
`src/CMakeLists.txt` or the nearest feature `CMakeLists.txt`. When adding a unit
test file, register it in `src/test/CMakeLists.txt`,
`src/wallet/test/CMakeLists.txt`, or `src/qt/test/CMakeLists.txt`.

## Toolchain and dependencies

The minimums documented by this checkout are CMake 3.22, Python 3.10, GCC 11.1
or Clang 16, Boost 1.73, and libevent 2.1.8. The code is compiled as C++20.
The default build is `RelWithDebInfo` (`-O2 -g` on typical Unix compilers).

Minimal Debian/Ubuntu setup for a headless descriptor-wallet build:

```bash
sudo apt-get install build-essential cmake pkgconf python3 \
  libevent-dev libboost-dev libsqlite3-dev
```

Common optional packages are `python3-zmq`, `libzmq3-dev`,
`libminiupnpc-dev`, `systemtap-sdt-dev`, `libqrencode-dev`, and Qt development
packages. See `doc/build-unix.md` and `doc/dependencies.md` for the current full
list and the macOS/BSD/Windows-specific documents under `doc/` for those hosts.

Descriptor wallets use SQLite and are enabled by default. Legacy wallets need
Berkeley DB and `-DWITH_BDB=ON`; release-compatible legacy wallets require BDB
4.8, not the newer BDB commonly packaged by Linux distributions. A node-only
build can avoid both databases with `-DENABLE_WALLET=OFF`.

Do not install dependencies or run package-manager commands without user
authorization. Prefer reporting the exact missing dependency. The `depends/`
system is the source of pinned release dependencies and toolchain files; read
`depends/README.md` before using it.

## Configure and build

Always build out of source. The conventional directory is `build/`:

```bash
cmake -B build
cmake --build build -j 4
```

Use a conservative fixed `-j` value on memory-limited hosts; compilation needs
roughly 1.5 GiB or more and highly parallel C++ builds can exhaust RAM. Inspect
available configuration switches with `cmake -B build -LH`.

Useful configurations:

```bash
# Debug symbols, assertions, lock-order and lock-contention instrumentation
cmake -B build-debug -DCMAKE_BUILD_TYPE=Debug

# Headless node without wallet or GUI dependencies
cmake -B build-node -DENABLE_WALLET=OFF -DBUILD_GUI=OFF

# GUI (Qt 5 by default; use -DWITH_QT_VERSION=6 for Qt 6)
cmake -B build-gui -DBUILD_GUI=ON

# Treat warnings as errors for CI-like compilation
cmake -B build -DWERROR=ON

# Generate compile_commands.json for tooling
cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

The `dev-mode` preset enables nearly every optional target and dependency and
uses `build_dev_mode/`; it is useful only when those dependencies are installed:

```bash
cmake --preset dev-mode
cmake --build build_dev_mode -j 4
```

Important current-build nuance: root `CMakeLists.txt` unconditionally sets
`WITH_MULTIPROCESS` to `OFF`, even though the inherited `dev-mode` preset asks
for it to be on. Treat the generated CMake summary as truth; do not assume the
preset succeeded in enabling multiprocess support.

Build only the target needed during iteration, for example:

```bash
cmake --build build --target bitcoind -j 4
cmake --build build --target test_bitcoin -j 4
cmake --build build --target bitcoin-qt -j 4
```

Binaries are emitted under `build/bin/`. Installation is optional:
`cmake --install build`. Never install system-wide unless explicitly requested.

## Running safely

Use regtest and a disposable, explicit data directory for development. Never
point tests or experimental builds at a user's real mainnet wallet/datadir.

```bash
build/bin/bitcoind -regtest -datadir=/absolute/path/to/disposable-dir \
  -daemon -fallbackfee=0.0002
build/bin/bitcoin-cli -regtest -datadir=/absolute/path/to/disposable-dir \
  getblockchaininfo
build/bin/bitcoin-cli -regtest -datadir=/absolute/path/to/disposable-dir stop
```

By default the data directory is `~/.bitcoin` on Linux,
`~/Library/Application Support/Bitcoin` on macOS, and `%LOCALAPPDATA%\Bitcoin`
on Windows. Chain state is below `regtest/`, `signet/`, `testnet3/`, or
`testnet4/`; `bitcoin.conf` is normally at the datadir root. Command-line values
override configuration values. Do not edit `settings.json` while the program is
running; changes may be ignored or overwritten.

`bitcoind -help` is the authority for options in the built revision. RPC help is
available through `bitcoin-cli help [command]`. Prefer graceful shutdown via
the `stop` RPC and wait for the process to exit before reusing a datadir.

## Testing and validation

Choose the narrowest test that proves the changed behavior, then expand based
on risk. Do not claim success without running the relevant command on the final
working tree.

### C++ unit and utility tests

```bash
ctest --test-dir build --output-on-failure
build/bin/test_bitcoin --list_content
build/bin/test_bitcoin -l all -t getarg_tests
build/bin/test_bitcoin -l all -t getarg_tests/doubledash
```

`ctest` also runs tests from embedded subprojects and utility tests. Use
`ctest --test-dir build -N` to list registered tests and `-R <regex>` to select
one. Failure details are saved in `build/Testing/Temporary/LastTest.log`.

### Functional tests

Configure and build first. Run from the repository root so paths and globs are
unambiguous:

```bash
build/test/functional/test_runner.py feature_rbf.py
build/test/functional/test_runner.py --jobs=4
build/test/functional/test_runner.py --extended
```

An individual generated test script can also be invoked directly, for example
`build/test/functional/feature_rbf.py`. The default suite is already large;
`--extended` is larger still. Run the focused test while iterating, then the
appropriate suite before handoff.

Functional tests launch isolated regtest nodes. Their first output line gives
the temporary directory. On failure inspect:

- `<tmpdir>/test_framework.log`
- `<tmpdir>/nodeN/regtest/debug.log`
- merged output from
  `build/test/functional/combine_logs.py -c <tmpdir> | less -r`

Useful flags are `--nocleanup`, `--tracerpc`, `--jobs=N`, `--tmpdir=...`, and
the per-test `-l debug`. Run the runner with `-h` for the current set. Do not
kill all host `bitcoind` processes as a routine cleanup step; that can destroy
an unrelated user's running node. Identify and terminate only test-owned PIDs.

When changing RPC/P2P behavior, add or update a functional test. Reuse helpers
from `test/functional/test_framework/`, use deterministic assertions, and avoid
wall-clock sleeps. If the framework already exposes a synchronization helper,
use it.

### Lint and formatting

The full lint runner requires Rust plus the tools listed in
`test/lint/README.md` and `ci/lint/04_install.sh`:

```bash
(cd test/lint/test_runner && cargo fmt && cargo clippy && \
  RUST_BACKTRACE=1 cargo run)
```

Run selected lints with, for example:

```bash
(cd test/lint/test_runner && \
  RUST_BACKTRACE=1 cargo run -- --lint=doc --lint=trailing_whitespace)
```

Format only changed C++ lines, not whole legacy files:

```bash
git diff -U0 --no-color | contrib/devtools/clang-format-diff.py -p1 -i
```

Python formatting/lint behavior is defined by `.style.yapf` and the lint runner
(which invokes pinned tools such as Ruff). Check `git diff --check` for
whitespace errors.

### Fuzzing, sanitizers, and benchmarks

```bash
cmake --preset=libfuzzer
cmake --build build_fuzz -j 4
FUZZ=process_message build_fuzz/bin/fuzz /path/to/corpus

cmake -B build-asan -DSANITIZERS=address,undefined
cmake --build build-asan -j 4

cmake -B build-bench -DBUILD_BENCH=ON
cmake --build build-bench --target bench_bitcoin -j 4
build-bench/bin/bench_bitcoin
```

The fuzz seed corpora live in the separate `bitcoin-core/qa-assets` repository.
Do not add arbitrary corpora or crash artifacts to this repository. Address and
thread sanitizers are mutually incompatible. Use the suppressions in
`test/sanitizer_suppressions/` as documented in `doc/developer-notes.md`.
Performance claims should include a focused `bench_bitcoin` comparison and, for
user-visible paths such as IBD, an end-to-end measurement where practical.

## Debugging workflow

1. Reproduce with the smallest deterministic unit or functional test.
2. Record the exact configure flags, executable, arguments, and datadir.
3. Read the relevant `debug.log`; enable categories with `-debug=<category>`
   and severity with `-loglevel=<level>`. Runtime categories can be toggled via
   the `logging` RPC. Qt `qDebug()` output uses the `qt` category.
4. Use a Debug or sanitizer build when the symptom warrants it.
5. Form a concrete root-cause hypothesis before editing and add a regression
   test that fails for the original bug.

Common commands:

```bash
gdb --args build/bin/bitcoind -regtest -datadir=/path/to/test-datadir
gdb build/bin/test_bitcoin
build/bin/test_bitcoin --catch_system_errors=no -t suite/test
```

A Debug build enables `DEBUG_LOCKORDER` and `DEBUG_LOCKCONTENTION`. Use
`-debug=lock` or the runtime `logging` RPC for contention records. For a unit
test that needs a persistent temporary datadir, pass application arguments
after `--`, for example:

```bash
build/bin/test_bitcoin -t getarg_tests/doubledash -- \
  -testdatadir=/absolute/path/to/test-output -printtoconsole=1
```

Do not “fix” flaky functional tests by increasing sleeps. Check stale test
processes, port conflicts, the generated cache under `build/test/cache`, and
the combined logs. Only remove generated test cache after confirming its exact
path and that no test is using it.

## Coding rules that matter most

The complete rules are in `doc/developer-notes.md`; functional-test rules are
in `test/functional/README.md`. Preserve these essentials:

- Four spaces, no tabs (except Makefile recipes), LF endings, final newline,
  and no trailing whitespace. CMake/YAML commonly use two spaces as specified
  by `.editorconfig`.
- New C++ uses `snake_case` variables/namespaces, `m_` members, `g_` globals,
  `UPPER_CASE` constants, and `PascalCase` classes/functions/methods. Do not
  rename broad legacy surfaces merely to match modern style.
- Prefer RAII, value returns, `std::optional` for genuine optional values,
  `nullptr`, list initialization, explicit namespace qualification, named
  casts, and `++i`.
- Keep includes minimal and in the order enforced by project lint. Include the
  corresponding header first in implementation files where the project does so.
- Use project synchronization annotations/macros and established lock order.
  Do not call unknown/external code while holding a lock without analyzing
  reentrancy and order. Run a Debug build for synchronization changes.
- Use `Assert`, `Assume`, `CHECK_NONFATAL`, or explicit error handling according
  to the distinctions documented under “Assertions and Checks”; never rely on
  assertions for untrusted input validation.
- Monetary RPC values must use `AmountFromValue`/`ValueFromAmount`, not floating
  point or hand parsing. RPC methods are lowercase; new argument/result fields
  use established names or `snake_case`. Treat omitted and `null` arguments
  consistently.
- Keep GUI work off the GUI thread when it can block. Preserve node/wallet/GUI
  separation through interfaces.
- Python functional tests use `snake_case`, named constants instead of magic
  values, precise `assert_equal`-style helpers, and framework synchronization.
- Add comments for why and for invariants, not line-by-line narration. Keep
  patches reviewable; avoid unrelated refactors and formatting churn.

For new user-facing strings, follow the translation helpers already used by
the surrounding component. Do not manually edit translated
`src/qt/locale/bitcoin_*.ts` files. `bitcoin_en.ts` is generated with the
`translate` target as described in `doc/translation_process.md`.

## Change-specific checklist

- **Consensus/validation/script/serialization:** obtain explicit intent; verify
  compatibility with historical data and peers; add unit, functional, and/or
  fuzz coverage; test reindex/load paths when relevant. Preserve the Roots
  consensus-neutral invariant.
- **Policy/mempool/mining:** prove the rule affects admission/relay/template
  policy only unless a consensus change was explicitly requested. Test both
  rejection and block acceptance where that boundary is at risk.
- **RPC:** update implementation, help text, argument/result schemas, and
  functional tests together. Run `bitcoin-cli help <rpc>` and relevant doc lint.
- **Wallet/database:** test descriptor-wallet behavior by default and legacy
  BDB behavior only with a compatible BDB build. Cover upgrade, unload/reload,
  backup, and watch-only/signing implications as applicable.
- **P2P/networking:** test malformed and adversarial input, disconnect/ban
  behavior, permissions, and resource bounds. Prefer P2P test-framework helpers
  and add fuzz coverage for parsers.
- **GUI:** build with `-DBUILD_GUI=ON`, run `test_bitcoin-qt` when enabled, and
  manually check the affected interaction without a real wallet/datadir.
- **Build/CI/platform:** configure at least one affected minimal combination;
  remember options interact. Consult `ci/test/00_setup_env_*.sh` rather than
  assuming the default build represents CI.
- **User-visible feature/config/API:** add `doc/release-notes-<PR>.md` when a PR
  number is known. Do not invent a number; report the need in the handoff.

## Generated files, docs, and repository hygiene

- Never edit files in a build directory as source; reconfigure/regenerate them.
- Manpages in `doc/man/` and `share/examples/bitcoin.conf` are generated from
  built help output using `contrib/devtools/gen-manpages.py` and
  `contrib/devtools/gen-bitcoin-conf.sh`. Regenerate them only when their source
  help/config interface changes and verify the diff.
- Do not commit build trees, functional-test caches/temp dirs, fuzz artifacts,
  core dumps, coverage output, editor metadata, or local `.gestalt/` data.
- Preserve unrelated working-tree changes. Check `git status --short` before
  and after work. Do not reset, clean, or delete files you did not create.
- Do not update lockfiles, vendored subtrees, generated translations, snapshots,
  or release artifacts as collateral effects.
- Documentation links and commands may be inherited from upstream. When a
  statement is fork-sensitive, verify it against this checkout before repeating
  it. Fix only the stale material relevant to the current change.

## Before handing off

Summarize what changed and why, list exact verification commands and outcomes,
and disclose anything not run or any dependency that prevented it. At minimum:

```bash
git diff --check
git status --short
git diff --stat
```

Then run the focused test(s), an appropriate `ctest`/functional subset, and
relevant lint. Review the final diff for accidental consensus changes, policy
boundary violations, generated-file churn, branding regressions, secrets,
absolute local paths, and test-only debug code. A successful compile alone is
not sufficient evidence for behavioral changes.

## Git-native Roots patch and release workflow

Git history is the source of truth for Roots patches. Keep each supported Core
version as a linear semantic series on `roots/<core-version>`, starting at the
exact matching Bitcoin Core tag. Use reviewable `topic/<core-version>/<area>`
branches for development, disposable `integration/<core-version>` branches for
combined CI, `promote/<core-version>` branches to merge a canonical tree into
`main`, and `archive/*` only to preserve superseded public history. Release
tags point to canonical `roots/*` tips, not promotion commits on `main`.

For a new Core generation, create `roots/<new-version>` directly from the new
official Core tag. Port the prior Roots commits semantically in logical order,
usually with `git cherry-pick -x`, dropping behavior Core adopted and explaining
material rewrites. Do not merge the previous Roots branch into the new Core
branch. Compare generations with `git range-diff`, keep policy rejection out of
block validity, and rerun the tests owned by every ported feature.

Do not check generated patch files into Git. Release automation exports the
canonical range with `git format-patch`, excludes `.github/**` from the portable
product patch, and verifies replay onto the matching Core base. Because the
series contains inherited CRLF files, apply the released mbox with
`git am -3 --keep-cr <bitcoin-roots-*.patch>`.

Before assigning a permanent release tag:

1. Merge the reviewed promotion PR and verify its tree equals the canonical tip.
2. Dispatch `Release artifacts` from `main` with the future Roots tag and the
   full 40-hex `origin/roots/<core-version>` commit.
3. Require all five platform builds and independently inspect the packages and
   patch. Manual rehearsal must create neither a remote tag nor a release.
4. Re-fetch refs, create one annotated tag at the unchanged canonical tip, run
   the release-source validator, and push only that tag. Never move it.
5. Verify the tag-triggered draft's packages, patch, `SHA512SUMS`, optional
   signature, tag target, and source tree before publishing the unchanged draft.
6. Repeat tag, asset, checksum, and signature-if-present verification through an
   unauthenticated client after publication.

The detailed commands and branch maintenance rules are in
`contrib/roots/README.md`. The local Bitcoin Core v29.4 tag is signed, but the
signer public key is absent; do not claim local cryptographic verification.
