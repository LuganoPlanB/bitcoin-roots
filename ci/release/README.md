# Release assembly and optional notice tooling

`release.yml` runs the nine-package pipeline on a permanent Roots tag push;
optional manual dispatch rehearses an absent future tag with read-only
permissions. The normal release builds once on the tag push, signs/creates a
draft and independently verifies it before separate publication approval.
Manual rehearsal is not required, cannot run against an existing remote tag,
and does not establish binary reproducibility. Source corrections require
renewed review/CI and affected build evidence.
Both paths preserve existing project license files and stage canonical `COPYING`
before packaging. The user deferred expanded dependency copyright/REUSE
coverage to future releases; the default release does not require dependency
notice collection or its index schema. No complete coverage claim is made.
Only the tag path enters the `release` environment and requires a signed
manifest before creating a new draft. Public publication needs separate approval.
Unsafe or unassembled packages fail before upload or final preparation. No
prior binary is substituted for a fresh build.

The current candidate targets Core v32.1 and the future
`v32.1-roots.3` name, appending to the published Roots.2 history.
Source/tag checks derive `roots/32.1` and `v32.1`
from that name; an absent future name may be checked only offline or in an
explicitly requested disposable rehearsal. Tag presence does not establish
publication. Local fixtures are not hosted cross-platform build evidence.

## Static headless musl packages

Alongside the existing five Linux/macOS/Windows packages, the release requires:

- `bitcoin-roots-musl-x86_64.tar.gz`
- `bitcoin-roots-musl-arm_64.tar.gz`
- `bitcoin-roots-musl-arm_hf.tar.gz`
- `bitcoin-roots-musl-riscv_64.tar.gz`

The shared `reusable-musl-release.yml` matrix uses [dyne/musl-action](https://github.com/dyne/musl-action)
at commit `fe1d1871b096824ddb7aa158f68dd63dabdc9586` and SDK `v2.6.0`, with
per-target SHA256 pins checked against that release's manifest. Keep the SDK
version and all four digest pins together when updating. The x86_64-hosted
toolchains target `x86_64-linux-musl`, `aarch64-linux-musl`,
`arm-linux-musleabihf` and `riscv64-linux-musl`, respectively. The action requires
an x86-64-v2 runner; target execution requirements follow the pinned SDK.

`build-musl.sh` consumes the action's `CC`/`CXX` outputs, locates the relocated
SDK from those paths and uses its target-only CMake/pkg-config configuration.
It builds with fixed parallelism 2 and installs into a new private directory.
Qt, IPC, USDT, tests and experimental kernel tools are disabled. SQLite wallets,
ZeroMQ, embedded dependencies, the `bitcoin` launcher, `bitcoind`, `bitcoin-cli`,
`bitcoin-tx`, `bitcoin-util` and `bitcoin-wallet` are included. No Cap'n Proto or
Qt dependency is required. This is a headless release profile; existing GUI
packages retain their profiles.

`validate-musl.py` requires exactly those six executable files, checks ELF
class/machine and ARM hard-float ABI, and rejects a program interpreter,
`DT_NEEDED`, shared libraries or symlinks. This gate runs after installation
and again before packaging. Native x86_64 CI also exercises help/version output
and offline SQLite wallet creation in an isolated regtest datadir. Foreign
targets receive compile/link/ELF checks without claiming runtime execution.
All archives use the versioned root and canonical `COPYING` checks. The final
job waits for the whole musl matrix and requires nine packages plus the patch;
its signed draft contains twelve assets including both manifest files.

Local metadata/fixture checks do not establish that these SDK builds pass.
Fresh hosted build evidence for every target is required before release.

`musl.yml` runs that same matrix on pull requests, including drafts, when build
inputs change. It checks out the exact PR head with a read-only token, compiles
the candidate version and retains the four checked archives as run artifacts.
PR builds skip canonical release-source validation and create no tags, signed
manifests or drafts. The release caller keeps canonical validation enabled by
default and still waits for all four builds before signing/assembly. PR results
establish build/profile evidence only; they do not establish release readiness.

The portable exporter uses exact Core ancestry, excludes `.github/**`, replays
with `git am -3 --keep-cr`, and compares all product blobs, modes and symlinks
with the candidate while retaining Core's hosting tree. Local verification
commits must use the contributor's required signing key. Supply inherited Git
`commit.gpgsign=true`, `user.signingkey=5EADD53F2CD1F0B7AEEE920D25FC5C29CD528E32`
and `gpg.format=openpgp`, including in the disposable clone, and verify each
new commit's exact `VALIDSIG`. A parent repository's local config alone is not
inherited by clones. Git commit signing is separate from release-manifest
signing. Keep optional read-only hosted rehearsals free of release secrets.

## Optional dependency notice tooling

The following interface remains available for future coverage work. It is not
a prerequisite for this release's CI pipeline or draft validation.

The supported assembly interface uses explicitly reviewed inputs:

1. Prepare a descriptor for each platform from retained build logs, binary
   version metadata, dependency recipes or package metadata. Preserve source
   notice bytes and identify the exact version, primary origin, source hash and
   evidence. The descriptor is a JSON object keyed by component. Each entry has
   `version`, `source`, `source_sha256`, `provenance_type`, `evidence`, `files`,
   and optionally `role`. Paths in `evidence` and `files` are absolute or relative
   to the descriptor. `build-receipt` entries can instead supply `receipt`;
   their source hash defaults to the receipt hash. Independent provenance can
   use `pinned-source-archive` or `versioned-primary-notice` without an old runner
   receipt. `source-version-stability` keeps `version` as `unknown` and records
   `notice_stable_versions` only when authoritative notice bytes are identical
   across the reviewed range. A source hash means the source archive hash for
   archive provenance, or the primary notice-source record digest for notice
   provenance; retained evidence identifies which and includes notice hashes.
2. Collect the reviewed inputs into a new directory:

   ```sh
   python3 ci/release/notices.py collect linux-x86_64 descriptor.json notice-inputs
   ```

   The collector adds tracked component licenses and embedded font attribution,
   retains provenance and builds a hashed index. Conservative source-availability
   notices should be labeled with `role`; they do not establish actual linkage.
3. Add notices to a new package without modifying existing members:

   ```sh
   python3 ci/release/repack.py intermediate.tar.gz assembled.tar.gz \
     bitcoin-roots-32.1-roots.3 notice-inputs/index.json linux-x86_64
   ```

   The repacker adds canonical `COPYING` and `notices/` at the archive root,
   outside signed applications. It preserves every existing member's bytes,
   modes, timestamps and symlink contents. Existing destinations, including
   dangling symlinks, are refused. An existing regular COPYING must match
   canonical bytes and is preserved as an original member; COPYING is added
   only when missing. Altered/nonregular COPYING or an existing notices tree
   is refused. Use the package's original release filename
   when placing the result in the final download directory.
4. Validate all nine assembled packages with their matching portable patch:

   ```sh
   ci/release/prepare-release.sh assembled-downloads release-assets \
     contrib/release/bitcoin-roots-release-key.asc v32.1-roots.3 9
   ```

   This final gate derives each expected platform from its exact asset filename,
   requires canonical `COPYING` and archive safety, and validates every package
   before copying assets or creating `SHA512SUMS`. Dependency indexes are not
   required by this gate. `notices.py validate ARCHIVE ROOT PLATFORM` and
   `notices.py stage DIRECTORY INDEX PLATFORM` expose the same caller-pinned
   validation for explicit assembly of the original five platforms. The optional
   notice collector has no musl dependency profile yet; do not apply a GUI
   platform's dependency index to a headless musl package.

Retain the original build run/source identity when repacking. A packaging-only
source correction requires a reviewed source/portable-patch reconciliation;
it does not turn an old run into a build of the new commit. Signing, permanent
tagging and publication remain separate reviewed actions. These checks verify
notice contents and provenance, not general legal compliance.

## Optional runner collection

`runner-notices.py` can feed the descriptor/collection format above for future
coverage work. It is not invoked by the current release workflow:

- Linux runs inside the build container after the new build, while installed
  `depends` toolchains/recipes and verified source archives remain available.
  The notice tree persists into the mounted build directory. Both architectures
  independently capture selected packages and Qt's additional source archives.
- macOS captures matching installed Homebrew formula receipts and verified
  source hashes for required dependencies and any extra Qt modules proven in
  the built bundle. Qt's non-archive umbrella and unrelated CA/tool formulas
  are excluded. Bundle hashes/`otool` records prove system SQLite linkage.
- Windows captures the actual `build/vcpkg_installed` status and static-triplet
  copyrights. Boost split ports form one logical Boost notice with every exact
  installed version/copyright; the pinned manifest baseline remains evidence.

## Signed CI draft

The platform archive and canonical COPYING are validated before upload.
The tag-only final job downloads only its own run's nine archives and patch,
runs `prepare-release.sh`, and requires signing through `sign-manifest.sh` with
`REQUIRE_RELEASE_SIGNATURE=1`. The real secret is passed only to that step;
never copy it to local tooling or build jobs. A missing/mismatched key fails.

`create-ci-draft.py` requires exactly twelve regular nonempty assets. It reuses
the packet/source/archive/COPYING/patch replay guards, independently imports only the
checked-in public key and verifies `SHA512SUMS.asc` with the expected fingerprint.
It snapshots the checked bytes and curated notes, refreshes remote tag/canonical
identities and paginated release inventory, then calls only
`gh release create --draft --verify-tag`. Any existing draft/public release or
network ambiguity fails closed; no clobber/update/deletion/replacement exists.
The notes add the actual fresh build run/source and distinguish manifest, Git
and platform signatures. `contrib/release/release-notes.md` is the reviewed
curated template, including #24/#25 contributor credit.

The older `create-draft.py` remains a validation/unsigned-packet utility and
shares its fail-closed guards with the signed CI path. It is not the normal
release publication route. Never use it to bypass required CI signing.

Download all twelve draft assets independently and check the manifest signature,
ten asset checksums, archive safety/COPYING, immutable source/tag, patch replay and
notes before visibility-only publication under separate authority. Repeat the
consumer checks unauthenticated after publication. Interrupted creation needs
operator inspection of the partial draft; reruns cannot replace it.
