# Website release inputs

Use Node.js 24 (the workflow version; the package minimum is 20), a clean Roots
checkout or snapshot, and a sibling `vite-theme` checkout pinned to
`b969923b3407fb85a46cf45a8e18385d239cf79e`. The layout is
`<workspace>/bitcoin-roots/doc/website` and `<workspace>/vite-theme`; the theme
is a local file dependency. Confirm the theme revision from the Roots root,
then install the website dependencies once:

```sh
git -C ../vite-theme rev-parse HEAD
cd doc/website
npm ci
```

Prepare, verify and build every public historical release with one command:

```sh
DOCS_BASE=/bitcoin-roots/ npm run releases:build
```

Use the same command for a local refresh or retry. It downloads published bytes,
then builds VitePress, renders bounded patch views and checks static links and
coverage. Run `npm test` for the offline suite. For a root-base local preview,
use `DOCS_BASE=/ npm run releases:build` followed by `npm run preview`.
Production uses `/bitcoin-roots/`; use the same base for every build stage.

The preparation stage remains separately available when only inputs are needed:

```sh
npm run releases:prepare
```

Preparation explicitly contacts the public GitHub releases API, following
numbered pages through the final empty page. Unit tests and the ordinary website
build do not contact GitHub. Rendering these inputs into patch pages is a
separate offline stage included in `npm run build`. A build requires a prepared
input catalogue.

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

## Publication refresh and recovery

After publishing and independently verifying a release, a maintainer can request
an immediate website refresh from trusted `main`:

```sh
gh workflow run deploy-docs.yml --repo LuganoPlanB/bitcoin-roots --ref main
gh run list --repo LuganoPlanB/bitcoin-roots --workflow deploy-docs.yml --branch main --limit 5
gh run watch <refresh-run-id> --repo LuganoPlanB/bitcoin-roots --exit-status
```

Select the new run ID from the list. These commands require repository workflow
dispatch authority. This is the documentation workflow, separate from
`release.yml` and its immutable binary-release handoff. A website retry never
rebuilds binaries, moves tags, uploads or replaces release assets. Do not rerun
a published tag's release workflow to refresh the site.

`deploy-docs.yml` also checks the complete public inventory at minute 17 every
six hours in UTC, and on main pushes. Expected automatic catch-up is within six
hours **plus GitHub scheduling delay**, not a delivery guarantee: schedules can
be delayed or dropped, and public repositories' schedules can be disabled after
60 days without activity. Manual dispatch recovers a missed update. See
[GitHub's scheduled-event rules](https://docs.github.com/en/actions/reference/workflows-and-actions/events-that-trigger-workflows#schedule).
Release events use a release tag ref, whose website files can be old or absent;
publication performed with `GITHUB_TOKEN` ordinarily suppresses subsequent
release-triggered workflows. The explicit dispatch and scheduled inventory do
not depend on that event chain or introduce a PAT. GitHub documents the
[tag-ref release behavior](https://docs.github.com/en/actions/reference/workflows-and-actions/events-that-trigger-workflows#release)
and [token-trigger exceptions](https://docs.github.com/en/actions/how-tos/write-workflows/choose-when-workflows-run/trigger-a-workflow#triggering-a-workflow-from-a-workflow).

Each production run acquires the existing whole-workflow `pages` concurrency
group, resolves current `main`, then discovers all current public releases.
It never builds a queued event's old source SHA or released tag. Concurrency
ordering is not guaranteed, so input discovery happens after acquisition.
Non-main dispatches and fork runs cannot deploy; PR checks and local previews
have no Pages publication authority. Only the deployment job receives Pages
write and OIDC permissions. The pinned shared theme remains unchanged.

The public [deployment receipt](https://plan-b.foundation/bitcoin-roots/deployment.json)
is emitted into the checked Pages artifact after the build succeeds. It binds
the complete inventory fingerprint, main revision, pinned theme revision and
base, plus release/available-patch counts. The fingerprint includes release
identity, publication/prerelease state and edits, and patch/checksum/signature
asset IDs, names, URLs, sizes, update timestamps and platform digests. Added,
deleted, newly unavailable or replaced inputs cause a fresh build; an asset's
previously verified bytes are not assumed unchanged after its metadata changes.
The receipt is deployment evidence only when served by the public site.
Matching public evidence skips dependency installation, full build and deploy.
A missing or malformed receipt forces a build; cache presence cannot suppress
one. Transient receipt/API errors fail the refresh and preserve the public site.

The workflow first saves its checked inventory in ignored
`.vitepress/release-inputs/publication.json`, then fetches/verifies exactly that
snapshot. It adds a receipt only when the verified input inventory and rendered
catalogue agree. Actions caches contain only verified asset objects and renderer
output, under `website-patches-v1-…`; the receipt and checked snapshot are never
restored as deployed evidence. All restored bytes and generated pages are
revalidated. Download, checksum, parse, output-budget or link-check failures
prevent upload/deployment, leaving the last successful public site intact.

Diagnose the failing step in the refresh run before retrying:

| Failure | Check and recovery |
| --- | --- |
| Public inventory / receipt | Check GitHub API rate limits, connectivity, Pages URL and run errors. Retry the documentation dispatch after the transient issue clears. An absent receipt on first deployment is expected. |
| Fetch / integrity | Compare the exact selected asset identity, filename, size, GitHub digest and SHA512SUMS entry. A checksum mismatch is a published-input problem until investigated; matching checksums alone do not authenticate signatures. |
| Verified input cache | Locally remove `.vitepress/release-inputs/assets/` and repeat `releases:build`. In Actions, remove the affected website cache through the repository's Actions caches UI, then dispatch again. Never edit cached bytes to make verification pass. |
| Render / static checks | Inspect the release, commit/file state and reported bound. Clear disposable `.vitepress/patch-cache/` for a cold retry. Corrupt output caches regenerate automatically; malformed input or exceeded limits require a reviewed fix. |

For a successful run, open the public `/patches/` index and every release
overview, including unavailable history. Check representative commit/file deep
links, original download/checksum destinations and public receipt counts against
the run's captured inventory. Release overviews retain series totals and explicit
verification states; the receipt records publication identity, not signature
authentication. Browser screenshots and production checks are separate from
the local smoke test below.

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

## Mail-series parsing

`scripts/patch-series.mjs` reads the downloaded UTF-8 Git format-patch mail
series. It retains original commit IDs, ordered decoded mail metadata and
distinct file-change entries, including repeated paths. Navigation IDs contain
only the validated release tag and commit/file ordinals; filenames are display
data. Each file entry retains its original diff text, including CRLF endings
and no-newline markers. Original asset bytes remain in the verified input cache.

The parser counts old/new hunk lines before recognizing subsequent mail or file
boundaries. Header-like source lines cannot become extra commits or files.
Binary changes, metadata-only changes (including modes, empty files and pure
renames), unsupported formats/ambiguous paths, and mails without a diff each
have explicit states. Invalid UTF-8, malformed mail headers and incomplete
hunks fail generation rather than publishing incomplete coverage. Encoded mail
headers with unsupported character sets retain their original spelling.
This is a series of file changes; totals do not describe a net diff.

Focused offline coverage runs from `doc/website` with:

```sh
node --test .vitepress/tests/patch-series.test.mjs
```

On the 2026-10-06 input snapshot, parser commit/file/binary/metadata-only counts
match the table above with zero unsupported sections. Independent
`git -C /tmp apply --numstat --allow-empty < /absolute/path/to/cached.patch`
confirms the file counts without applying changes. Run this inspection outside
the repository: Git filters paths to the current subdirectory when run from
`doc/website`. Each asset's 58 CRLF endings remain in the extracted sections.

## Static patch rendering

`npm run build` builds VitePress, then `npm run patches:render` emits standalone
HTML below `.vitepress/dist/patches/`. To regenerate only patch views after an
existing VitePress build, use `npm run patches:render`. `DOCS_BASE=/bitcoin-roots/`
selects the production prefix; leaving it unset selects `/` for previews. Use the
same base for the VitePress build and renderer so shared CSS font URLs agree.
The renderer revalidates catalogue routes, download URLs, cache filenames,
byte size and SHA512 digest before reading a series into pages. Failure stops
the build and does not publish a partial patch directory.

diff2html is pinned to `3.4.56` in the package and lockfile. The adapter supplies
counted JSON hunk rows to its HTML API with `matching: none`, no inline word
highlighting, and no browser renderer/parser bundle. The library's string parser
is bypassed because it removes no-newline markers and normalizes source bytes.
Displayed no-newline markers retain their positions with blank line-number
cells; source lines that merely contain marker-like text stay intact.

Routes use `patches/<tag>/commit-<ordinal>/file-<ordinal>/`. File views default to
unified HTML; `side-by-side/` selects the alternative. `page-<n>/` links to later
parts. All overviews, navigation and diff text exist in static HTML and work
without JavaScript. Each page loads local shared site CSS, local diff2html CSS
and section-only `patches.css`; neither a CDN nor runtime GitHub access is needed.
These pages bypass Vue/Markdown compilation and global VitePress search. They
contain no prefetch hints, patch body data bundles or client-side parser.
`patches/catalogue.json` contains navigation metadata only and is not loaded
merely to visit the release index.

Each text part starts with at most 400 diff rows, preserving original before/
after line numbers. Parts split further when HTML expansion exceeds 1,800,000
bytes in either view. Source lines/hunk headers over 4,096 characters receive an
explicit oversized state with the complete original patch download. Binary,
metadata-only and unsupported changes also retain dedicated source-linked
pages. Every overview must stay below 500,000 bytes and every HTML document
below 2,000,000 bytes; exceeding these bounds fails generation explicitly.
The measured real-asset output below meets these bounds.

Focused static-output, pagination and hostile-input tests run with:

```sh
node --test .vitepress/tests/patch-renderer.test.mjs
```

## Output cache and measured budgets

`.vitepress/patch-cache/` holds ignored complete-output snapshots. Their SHA256
keys bind the full verified catalogue (including asset SHA512 digests), renderer
version/settings, limits, base, shared CSS identities, and hashes of the parser,
adapter, page template, section CSS, cache/generator code and dependency lockfile.
Changing a release, renderer, template, style or setting selects a new snapshot.
Old snapshots may be removed to recover space; deleting this disposable cache
forces cold generation without refetching the verified release inputs.

Reuse first rechecks every original asset's size and SHA512 digest, then verifies
the complete cached file list and each output's size/SHA256. Missing, modified,
path-tampered or symlinked objects cause regeneration from verified inputs.
Input integrity errors stop the build. HTML is staged before publication into
the local dist directory; deployment still requires the website workflow to
finish successfully. No cache presence is evidence that Pages deployed.

On this workstation with Node 24, the 2026-10-06 five-release snapshot and
production base produced the following uncompressed output:

| Tag | Static HTML pages | HTML bytes |
| --- | ---: | ---: |
| v29.4-roots.4 | 998 | 30,585,165 |
| v29.4-roots.3 | 969 | 30,056,201 |
| v29.4-roots.2 | 793 | 25,353,447 |
| v29.4-roots.1 | 753 | 24,593,820 |
| v29.3-roots.1 | 1 | 1,196 |

Including the release index, there are 3,515 HTML pages totaling 110,591,447
bytes. The complete output with metadata/CSS is 111,385,956 bytes. Total history
size is a build/deployment cost; a browser opens one bounded page at a time.
The largest HTML document is 291,734 bytes; the largest overview is 12,636
bytes, and the release index is 1,618 bytes. All 1,689 file changes have a
dedicated entry; current assets need no unsupported/oversized fallback.

Cold generation, including cache population and output writes, took 3.10 s
with 328,696 KiB peak RSS. Verified warm reuse took 1.32 s with 117,164 KiB
peak RSS. A second independent cold generation took 3.08 s with 333,072 KiB.
The sorted output manifests were byte-identical across all three runs (SHA256
`44dba1d75ffde78bedb37239439feafd8fccc529f3014318998ee293349bad56`).
Parsing the largest 3,288,042-byte release alone took 44.4 ms; rendering its
952 unified/side-by-side text parts took 195.3 ms with 123,932 KiB peak RSS.
These workstation measurements describe the initial renderer before the styled
browsing UI; they are baselines, not promised build times.

Chromium checks at 1440×900 and 390×844, 100% font scale, with JavaScript
disabled opened the index, largest release, commit and file, switched views,
and used Back. The index loaded its 1,618-byte HTML plus 368,237 bytes of local
shared/section CSS and fonts, with no scripts, patch bodies, catalogue fetch or
history prefetch. The largest release overview loaded 7,866 bytes of HTML.
Both viewports had no document-wide overflow or failed requests; switching
views removed the old document and retained exactly one diff wrapper. Generated
patch bodies and renderer code are absent from the VitePress JavaScript/search
assets.

## Browsing UI and acceptance checks

The website's Patches navigation uses full-document links to the standalone
pages. The index stays chronological and highlights the newest available patch;
missing releases and public prereleases remain explicit. Core-line labels come
from the release tag, rather than implying independently verified ancestry.
Release overviews contain ordered commits and all file-change paths, including
repeated paths in different commits. These are metadata lists, not source-diff
payloads. Optional release/path-group searches operate on the rendered page;
reset and no-match states remain recoverable. Search parameters survive reload
and normal history navigation.

`patch-theme.js` shares VitePress's `vitepress-theme-appearance` preference and
applies explicit light/dark or system appearance before stylesheet paint.
`patch-browser.js` adds filtering. Both scripts are small same-origin assets,
are part of the renderer cache key, and load neither a parser nor GitHub data.
With JavaScript disabled, release, commit, file, view and pagination links plus
native expandable commit lists still work. Appearance and filtering controls
are shown only when their enhancement is available. Unified views are the
default; side-by-side views keep their horizontal scrolling inside the code
area. File line numbers refer to before/after source files; visible signs and
an explicit legend accompany the addition/removal colors.

Every `npm run build` now runs `npm run patches:check`. This offline acceptance
check verifies the complete static route set against the rendered catalogue,
all file-change counts, local links/resources, document budgets, headings, and
the restricted same-origin runtime assets. A failure stops the build. Renderer
input failures also stop generation and preserve the previous patch output;
they do not relabel a published asset as unavailable. Unit tests exercise empty
inventories, broken links, hostile runtime URLs and explicit binary,
metadata-only, unsupported and oversized states.

For a real-browser smoke check after a prepared build, reuse an existing
Playwright installation and Chromium. No browser dependency is installed by
this command:

```sh
PLAYWRIGHT_MODULE=/path/to/playwright \
PLAYWRIGHT_CHROMIUM_EXECUTABLE=/path/to/chromium \
PATCH_BROWSER_EVIDENCE=/tmp/roots-browser-evidence \
    npm run patches:smoke
```

The two Playwright variables can be omitted when the package and browser are
already resolvable by the standard Playwright runtime. The smoke check runs
desktop/mobile navigation from the website shell through release, commit and
file, reload/back/forward, missing/prerelease/empty/fallback states and
JavaScript-disabled browsing. It blocks external runtime requests. Explicit
outbound download and notes clicks use local mocks to check the exact published
destinations; the mocked download preserves and verifies the original cached
patch bytes. This proves navigation without contacting GitHub and does not
claim a live production check. Future-release examples are local synthetic
fixtures, never public releases. Results and screenshots are written to the
specified evidence directory, or a temporary directory printed on completion.

On the five-release backfill, the styled index is about 5 KB, the largest release
overview about 128 KB and the largest file document about 293 KB uncompressed.
All 1,689 file changes have dedicated routes across 3,515 HTML pages. Browser
acceptance covers light/dark at 1440×900 and 390×844, 100% and 200% root font
size, explicit modes against the opposite OS preference, keyboard focus and
scrolling, selectable source, and homepage/documentation style regressions.
The measured text-contrast samples exceed 4.5:1; code scrolling does not expand
the document width. Mobile commit ordinals remain single unbroken units at
both font sizes.
