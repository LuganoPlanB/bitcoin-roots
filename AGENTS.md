# Bitcoin Roots contributor and agent guide

This unpublished port starts directly from Bitcoin Core v32.1
(`f0de87f040621468b956f01f526a43a010310501`). Read
`doc/roots-v32-port-review.md` for pinned source, preservation contracts and
acceptance owners. A planned disposition does not establish an active feature.

Keep local admission, relay and template policy separate from consensus: valid
blocks containing locally rejected transactions must remain acceptable. Do not
add RDTS/BIP110 enforcement or activate dormant statistics. Preserve descriptor
wallet/SQLite and wallet/node/GUI interface boundaries. Keep changes minimal,
preserve unrelated edits and upstream/Jaromil/devjoinedthechat attribution,
and avoid cosmetic changes to embedded subtrees or generated translations.
Follow `doc/design/libraries.md` and register new sources in their native targets.

Use out-of-source CMake builds with `-j 2`. See `doc/dependencies.md` and
`doc/roots-build-portability.md`: CMake >=3.22, GCC >=12.1 or Clang >=17,
Boost >=1.74, Python >=3.10 for tests, SQLite for wallets and Qt >=6.2 for GUI.
Qt6 is the supported GUI; v32 native HTTP needs no libevent. Wallet/tests and
embedded libraries default on; GUI defaults off. Do not silently disable a
feature to claim a full profile pass. Record optional dependency/platform gaps.
Use disposable explicit regtest datadirs, including for daemon/wallet/GUI help.
Never relink a binary while its tests run. Run focused checks while editing and
one full applicable suite at acceptance; preserve logs and executed/skipped counts.
Generate manuals/example configuration only from current candidate help using
the existing generators and isolated datadirs. Do not substitute old help.

Sign every new Bitcoin Roots commit with Denis Roio's OpenPGP key, fingerprint
`5EADD53F2CD1F0B7AEEE920D25FC5C29CD528E32` (jaromil@dyne.org), across all
worktrees, including merges, cherry-picks and patch imports. Set repository-local
`user.signingkey` to that fingerprint, `commit.gpgsign=true`, `gpg.format=openpgp`;
use explicit `-S`/`--gpg-sign` where needed and inspect the resulting signature.
Preserve original authorship/credits. If signing fails, leave the commit pending
and report it; never disable signing, use an unsigned fallback or rewrite history
merely to add signatures. Commit signing does not authorize release signing.

Git history is canonical. Fold still-needed fixes into thematic changes on a new
exact Core base; never merge old Roots history into it or rewrite published
releases. Do not introduce replay frameworks, checked-in exports/manifests or
generated evidence. Keep builds, caches, logs and coordination data outside Git.
Inspect sorted status, staged names/bytes and whitespace before commits.
Dependency installation, system changes, canonical public-ref updates, main
promotion, workflow dispatch, permanent tags, release signing, release/draft
creation and publication require their own authorization.
