# Release assembly and optional notice tooling

`release.yml` runs the five-platform pipeline on a permanent Roots tag push;
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
`v32.1-roots.1` name. Source/tag checks derive `roots/32.1` and `v32.1`
from that name; an absent future name may be checked only offline or in an
explicitly requested disposable rehearsal. Tag presence does not establish
publication. Local fixtures are not five-platform hosted build evidence.

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
     bitcoin-roots-32.1-roots.1 notice-inputs/index.json linux-x86_64
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
     contrib/release/bitcoin-roots-release-key.asc v32.1-roots.1 5
   ```

   This final gate derives each expected platform from its exact asset filename,
   requires canonical `COPYING` and archive safety, and validates every package
   before copying assets or creating `SHA512SUMS`. Dependency indexes are not
   required by this gate. `notices.py validate ARCHIVE ROOT PLATFORM` and
   `notices.py stage DIRECTORY INDEX PLATFORM` expose the same caller-pinned
   validation for explicit assembly.

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
The tag-only final job downloads only its own run's five archives and patch,
runs `prepare-release.sh`, and requires signing through `sign-manifest.sh` with
`REQUIRE_RELEASE_SIGNATURE=1`. The real secret is passed only to that step;
never copy it to local tooling or build jobs. A missing/mismatched key fails.

`create-ci-draft.py` requires exactly eight regular nonempty assets. It reuses
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

Download all eight draft assets independently and check the manifest signature,
six asset checksums, archive safety/COPYING, immutable source/tag, patch replay and
notes before visibility-only publication under separate authority. Repeat the
consumer checks unauthenticated after publication. Interrupted creation needs
operator inspection of the partial draft; reruns cannot replace it.

## Complete the public release and Pages handoff

The release source/tag, signed packet and website are separate verifiable
outputs. Main integration must preserve the accepted product while carrying the
current source documentation and website tooling; it does not change which
commit produced the release packages.

Under the appropriate separate authorizations, complete these steps in order:

1. Validate the permanent Roots tag against its exact canonical source, build the
   fresh five-platform packet, sign the manifest and independently check all eight
   draft assets. Preserve reviewed notes and credits. Existing drafts or public
   releases require explicit reconciliation; never bypass the creator's guard.
2. Publish only after publication approval. Independently download the public
   bytes without authentication and repeat manifest-signature/checksum,
   archive/COPYING and source/tag/patch checks.
3. Send `repository_dispatch` type `release-published` to this repository, or run
   `deploy-docs.yml` explicitly from trusted main. Publisher automation using
   `GITHUB_TOKEN` must make this explicit handoff too. These requests are website
   refreshes; never rerun the binary-release workflow to update Pages.
4. Wait for the Pages run, then inspect the public deployment receipt and the
   release/commit/file navigation. Confirm the accepted main revision, complete
   inventory and original patch/checksum destinations. A failed or skipped run
   is not proof that the new release is browsable.

The commands, authority boundaries and recovery checks are maintained in
[Website release inputs](../../doc/website/RELEASE-INPUTS.md#publication-refresh-and-recovery).
The scheduled inventory check is a recovery fallback, not the completion gate.
Pages can describe an already-public temporary-tag release truthfully without
creating a permanent tag or claiming missing signatures are verified. That
compatibility does not waive the normal release procedure above.
