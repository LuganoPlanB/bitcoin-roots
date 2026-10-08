# Release notice assembly

`release.yml` runs the five-platform pipeline on a permanent Roots tag push;
optional manual dispatch rehearses an absent future tag with read-only
permissions. The normal release builds once on the tag push, signs/creates a
draft and independently verifies it before separate publication approval.
Manual rehearsal is not required, cannot run against an existing remote tag,
and does not establish binary reproducibility. Source corrections require
renewed review/CI and affected build evidence.
Both paths capture notices from each new build runner before packaging. Only
the tag path enters the `release` environment and requires a signed manifest
before creating a new draft. Public publication remains a separate approval.
Unassembled packages fail before upload or final preparation. No old local
notice bundle or prior binary is substituted for a fresh build.

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
     bitcoin-roots-30.3-roots.1 notice-inputs/index.json linux-x86_64
   ```

   The repacker adds canonical `COPYING` and `notices/` at the archive root,
   outside signed applications. It preserves every existing member's bytes,
   modes, timestamps and symlink contents. Existing destinations, including
   dangling symlinks, are refused. An existing regular COPYING must match
   canonical bytes and is preserved as an original member; COPYING is added
   only when missing. Altered/nonregular COPYING or an existing notices tree
   is refused. Use the package's original release filename
   when placing the result in the final download directory.
4. Validate all five assembled packages with their matching portable patch:

   ```sh
   ci/release/prepare-release.sh assembled-downloads release-assets \
     contrib/release/bitcoin-roots-release-key.asc v30.3-roots.1 5
   ```

   This final gate derives each expected platform from its exact asset filename,
   requires canonical `COPYING`, checks notice index/platform/content hashes,
   and validates every package before copying assets or creating `SHA512SUMS`.
   No flag bypasses notices. `notices.py validate ARCHIVE ROOT PLATFORM` and
   `notices.py stage DIRECTORY INDEX PLATFORM` expose the same caller-pinned
   validation for explicit assembly.

Retain the original build run/source identity when repacking. A packaging-only
source correction requires a reviewed source/portable-patch reconciliation;
it does not turn an old run into a build of the new commit. Signing, permanent
tagging and publication remain separate reviewed actions. These checks verify
notice contents and provenance, not general legal compliance.

## Runner collection and signed CI draft

`runner-notices.py` feeds the existing descriptor/collection format above:

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

The platform archive is staged and validated in the same job before upload.
The tag-only final job downloads only its own run's five archives and patch,
runs `prepare-release.sh`, and requires signing through `sign-manifest.sh` with
`REQUIRE_RELEASE_SIGNATURE=1`. The real secret is passed only to that step;
never copy it to local tooling or build jobs. A missing/mismatched key fails.

`create-ci-draft.py` requires exactly eight regular nonempty assets. It reuses
the packet/source/notice/patch replay guards, independently imports only the
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

Download all eight draft assets independently and check the manifest signature,
six asset checksums, package notices, immutable source/tag, patch replay and
notes before visibility-only publication under separate authority. Repeat the
consumer checks unauthenticated after publication. Interrupted creation needs
operator inspection of the partial draft; reruns cannot replace it.
