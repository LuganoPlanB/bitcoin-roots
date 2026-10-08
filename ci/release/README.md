# Release notice assembly

Platform build artifacts are intermediate packages. A green build proves the
built product, but its package must also pass notice assembly before becoming a
release asset. `release.yml` is a read-only `workflow_dispatch` build/rehearsal;
permanent tag pushes do not start builds or create a draft. Maintainers explicitly
assemble reviewed platform notices, validate the final packet and use the draft
helper below. Unassembled packages are rejected before upload. Automatic discovery
or acquisition of dependency notice inputs is not part of release execution.

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

## Immutable reviewed-assets draft

Complete source review, canonical/main integration and asset review first.
Authorize the exact annotated tag target and push only that tag; no tag-triggered
build or draft job runs. The canonical tip must remain unchanged. Release assets
must be outside the clean tagged source checkout and contain exactly the five
platform archives, `bitcoin-roots-<version>.patch` and `SHA512SUMS`. Record the
manifest's SHA512 digest and curated notes' SHA256 digest in the review; passing
newly calculated digests is not a substitute for acceptance of those bytes.

From the clean checkout at the approved commit, validate the complete handoff:

```sh
python3 ci/release/create-draft.py \
  --tag v30.3-roots.1 --commit <approved-40-hex-commit> \
  --repository LuganoPlanB/bitcoin-roots \
  --assets /absolute/path/to/reviewed-release-assets \
  --notes /absolute/path/to/reviewed-release-notes.md \
  --manifest-sha512 <reviewed-128-hex-digest> \
  --notes-sha256 <reviewed-64-hex-digest>
```

The default validates without a remote write. After explicit draft authority,
repeat the same command with `--create-draft`. The helper copies the exact packet
and notes into a private temporary snapshot, verifies all six checksums, archive
roots/COPYING/platform notice indexes, local annotated tag/source/linear ancestry,
and the exact portable patch regenerated and replayed from that tag. It then
refreshes remote canonical/tag object/peeled identities and checks every page of
the release inventory. Network/API failure or any existing draft/public release
blocks creation. The only write is `gh release create --draft --verify-tag` with
those seven immutable assets and the reviewed notes; it never updates, clobbers
or publishes an existing release. An interrupted upload requires independent
inspection of its partial draft, not an automatic helper retry or replacement.

This initial handoff accepts unsigned manifests only. `SHA512SUMS.asc` or any
unexpected file is rejected. Do not infer signing from the inherited public-key
comment in SHA512SUMS. Optional use of `sign-manifest.sh` remains a separate
signing operation, with signed-packet upload requiring its own reviewed path;
never access signing secrets for unsigned draft creation. State the actual Git
tag, manifest and platform-code signature status in the notes.

Independently download and verify the newly created draft, including notes and
contributor credit, before separately authorized visibility-only publication.
After publication verify all public assets unauthenticated. The helper requires
canonical-tip equality before draft creation; later consumer verification uses
the immutable release tag when the canonical branch has advanced.
