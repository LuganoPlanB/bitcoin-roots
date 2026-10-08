# Release notice assembly

Platform build artifacts are intermediate packages. A green build proves the
built product, but its package must also pass notice assembly before becoming a
release asset. The workflow's draft job runs `prepare-release.sh` before signing
or uploading anything. It fails closed if the downloaded packages lack valid
platform-specific notices. Automatic discovery or acquisition of dependency
notice inputs is not part of release execution.

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
