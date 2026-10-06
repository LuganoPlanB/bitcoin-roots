# Website release inputs

From `doc/website`, with the website dependencies and the pinned sibling
`vite-theme` checkout available, prepare the complete public history:

```sh
npm run releases:prepare
npm test
npm run build
```

Use the same preparation command for the first build, retries, and later
refreshes. It explicitly contacts the public GitHub releases API, following
numbered pages through the final empty page. Unit tests and the ordinary website
build do not contact GitHub. Rendering these inputs into patch pages is a
separate website stage; preparation currently writes inputs for that stage.

The ignored `.vitepress/release-inputs/catalogue.json` contains the stable,
chronologically sorted public inventory, including prereleases and releases
without a patch. `.vitepress/release-inputs/assets/` holds the original patch
and checksum bytes. No release is rebuilt, no patch is regenerated, and no tag,
branch, or release asset is modified. Do not add these generated inputs to Git.
Small synthetic fixtures in the unit tests keep regular tests offline.

Cache identities bind asset ID, name, destination, size, update timestamp and
GitHub digest; objects also include their SHA512 content digest. Every reuse
checks the bytes again. Metadata changes select a fresh cache entry; old entries
can remain until the cache is cleared. An integrity error stops preparation and
preserves the previous catalogue. If local cache corruption is diagnosed,
remove this disposable input directory and rerun preparation. For an upstream
checksum mismatch, investigate the published assets before retrying; never edit
cached bytes or checksums to make validation pass. Network failures remain
errors rather than turning available patches into unavailable entries.

`integrity.checksum: verified` means the exact patch filename matched one
published SHA512SUMS entry. Comment-prefixed signing-key metadata is ignored.
`platformDigest: verified` means bytes matched GitHub's supplied SHA256 digest.
Either value is `missing` when its source is absent. `signature: not-verified`
is always explicit: neither matching checksums nor the public key embedded in a
manifest establishes authenticated signature verification. A release lacking
the expected patch is retained with `patchState: unavailable` and no integrity
claim.

Downloads allow at most three attempts, four validated HTTPS redirects, 30
seconds per attempt, 20 MiB per patch and 2 MiB per checksum manifest. Discovery
is bounded at 1,000 pages with 8 MiB and 30 seconds per page. Bounds fail
explicitly; increases require measured review rather than silently truncating
data. The asset cache reuses verified bytes on subsequent preparations while
discovery always refreshes the full inventory.

## Observed history and baseline

Unauthenticated inventory on 2026-10-06 contained five releases across API pages
of 5 and 0 entries. Four patches matched both their published SHA512 checksums
and GitHub digests; `v29.3-roots.1` had no patch. No signature was verified.

| Tag | Patch bytes | Mail commits | File changes | Binary | Metadata only |
| --- | ---: | ---: | ---: | ---: | ---: |
| v29.4-roots.4 | 3,288,042 | 37 | 477 | 5 | 3 |
| v29.4-roots.3 | 3,259,998 | 30 | 466 | 5 | 3 |
| v29.4-roots.2 | 3,040,381 | 21 | 381 | 5 | 0 |
| v29.4-roots.1 | 3,002,930 | 13 | 365 | 5 | 0 |
| v29.3-roots.1 | unavailable | — | — | — | — |

These are ordered mail-series commit and file-change counts, including repeated
paths, rather than a net release diff. Mail boundaries use the original 40-hex
commit IDs. Independent `git apply --numstat --allow-empty` inspection confirmed
file/binary/metadata-only counts. Each patch contained 58 CRLF line endings and
a final newline; cached bytes preserve both. Renderers must account for binary
and metadata-only sections explicitly.

On this workstation (Node 24), the first successful preparation took 4.17 s
and 107,688 KiB peak RSS; warm preparation took 0.23 s and 94,532 KiB.
The persistent inputs totaled 12,608,759 bytes. Two consecutive live
preparations produced byte-identical catalogue output (SHA256
`4e8576a6434ce972d7e816bda665c4d9fcd7279361d0d8e6a459eefdaeee80b9`).
These are importer measurements, not browser or renderer performance budgets.
