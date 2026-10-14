# Bitcoin Roots contributor and agent guide

The released v31.1-roots.1 product is based directly on authenticated Bitcoin Core
v31.1 (`9be056a8a72b624dae9623b2f7bded92c2a21c91`). Selected Bitcoin Knots
`29.3.knots20260507` behavior is historical lineage, not another upstream or a
consensus specification. Read `README.md`, `doc/roots-features.md`,
`doc/wallet-privacy.md` and `doc/roots-v31-port-review.md` before changing behavior.

## Invariants and scope

- Roots remains compatible with Bitcoin Core consensus. Local mempool, relay
  and template rejection must not invalidate consensus-valid blocks.
- Do not add or imply RDTS/BIP110 enforcement. Dormant statistics, historical
  notes and unused support helpers do not establish active features.
- Preserve wallet authorization, transient-secret handling, descriptor/SQLite
  boundaries and the separation of wallet, node and GUI through interfaces.
- Keep consensus, serialization, validation, networking, wallet and cryptographic
  changes minimal, with observable regressions. Avoid incidental refactoring.
- Keep historical/upstream attribution and protocol identifiers. Do not mass
  rebrand comments, translated text, old release notes or embedded subtrees.
- Use disposable explicit regtest datadirs, including for daemon/wallet/GUI help
  generation. Early option setup can access wallet directories before printing
  help; do not point development commands at a real wallet/datadir.
- Preserve unrelated work and public refs. No dependency installation, system
  install, release dispatch, tagging, signing, publication or main promotion
  is authorized merely by an implementation task.

`src/` owns production C++; `test/functional/` owns isolated RPC/P2P integration;
`src/qt/test/` owns GUI tests; `ci/test/` owns maintenance fixtures and cadence;
`ci/release/` owns Git-native release validation/export; `depends/` owns pinned
release dependencies. Follow `doc/design/libraries.md` and
`doc/developer-notes.md`. Do not cosmetically edit vendored subtrees or manually
edit generated translations. Register added files in the relevant CMake target.

## Builds and evidence

Use out-of-source CMake and conservative fixed parallelism. Minimums include
CMake 3.22, Python 3.10, GCC 12.1 or Clang 17, Boost 1.74 and libevent 2.1.8;
see `doc/dependencies.md` for actual supported profiles. Wallet/tests default on,
GUI defaults off, embedded libraries default on. Qt5/Qt6 selection is supported.
Optional IPC/USDT/ZeroMQ/system-library/platform gaps are not passing evidence.
Windows GUI builds that request MiniUPnPc also request its optional vcpkg feature.

```sh
cmake -S . -B build -DENABLE_IPC=OFF
cmake --build build --target bitcoind test_bitcoin -j 2
python3 ci/test/roots-regressions.py plan --cadence fast \
  --files-json '["src/policy/policy.cpp"]'
python3 -m unittest discover -s ci/test
```

Use affected regressions and short time-bounded smoke checks while editing;
retain one full applicable suite at acceptance. Reuse evidence only when tested
code, binaries and fixtures match, recording provenance. Broad optional-platform,
extended and longer sanitizer/fuzz coverage belongs to scheduled/manual assurance;
see `ci/README.md` for exact budgets and commands. Input counts are results, not
campaign quotas. Preserve failure logs and state executed/skipped counts; a
compile or local YAML check does not establish a hosted or cross-platform pass.
Never relink binaries while dependent tests use the same build directory.

Manpages and `share/examples/bitcoin.conf` come only from current candidate help
through `contrib/devtools/gen-manpages.py` and `gen-bitcoin-conf.sh`. Use isolated
help wrappers when needed. Inspect generated diffs for profile-specific omissions
and reproducible output. Do not substitute older help or hand-edit the manuals.

## Maintenance and release boundaries

Git history is the source of Roots patches. Start `roots/<version>` at its exact
authenticated Core tag and port semantic commits; never merge an older Roots
line into a new base or rewrite published history. Review topic branches and
use exact canonical source/CI parity for promotions. See `contrib/roots/README.md`.

Portable patches exclude `.github/**` and replay with `git am -3 --keep-cr`;
product paths must match the selected tip while Core hosting files stay unchanged.
Retain author/contributor credit, including Jaromil and devjoinedthechat.
Signing and new-draft creation stay confined to the permanent tag workflow's
release environment; manual rehearsal remains read-only and cannot publish.
Existing draft/public releases block replacement. Manifest signing, Git-tag
signing and platform signing are distinct; expanded notice coverage is deferred.

Before handoff, run focused tests, relevant lint, `git diff --check`, and inspect
sorted intended scope/status. Disclose missing tools, dependencies and platform
coverage. Do not commit build trees, caches, fuzz crash artifacts, generated
patch exports or workspace-local coordination/evidence data. Org plans are never
Git deliverables; inspect staged names and bytes before an accepted commit.
