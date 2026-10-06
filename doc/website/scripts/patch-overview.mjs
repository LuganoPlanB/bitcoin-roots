import { escapeHtml as e, link, seriesExplanation, sourceLink } from "./patch-page.mjs";

export const coreLine = (release) => release.tag.match(/^v(.+)-roots\./)[1];
const date = (release) => `<time datetime="${e(release.publishedAt)}">${e(release.publishedAt.slice(0, 10))}</time>`;
const status = (release) => `${release.prerelease ? '<span class="patch-status">Prerelease</span>' : ""}${release.patchState === "unavailable" ? '<span class="patch-status">Patch unavailable</span>' : ""}`;
const size = (release) => `${(release.patch.size / 1024 / 1024).toFixed(2)} MiB · ${release.patch.size.toLocaleString("en-US")} bytes`;
export const orderedReleases = (releases) => [...releases].sort((a, b) => b.publishedAt.localeCompare(a.publishedAt) || a.tag.localeCompare(b.tag));

function filterControls({ kind, label, placeholder, options = [], optionLabel = "" }) {
    return `<form class="patch-filters" data-patch-filter="${kind}" hidden role="search"><label>${label}<input name="q" type="search" placeholder="${placeholder}" autocomplete="off"></label>
${options.length ? `<label>${optionLabel}<select name="group"><option value="">All ${kind === "releases" ? "Core lines" : "paths"}</option>${options.map((value) => `<option value="${e(value)}">${e(value)}</option>`).join("")}</select></label>` : ""}
<button type="reset">Reset filters</button><p class="patch-result-count" role="status" aria-live="polite"></p><p class="patch-no-match" hidden>No matches. Clear the search or reset filters to see all entries.</p></form>`;
}

export function releaseIndex(releases, local) {
    const ordered = orderedReleases(releases);
    const newest = ordered.find((release) => release.patchState === "available");
    return `<div class="patch-intro"><p class="patch-eyebrow">Published source · Bitcoin Roots</p><h1>Release patches</h1><p class="patch-lede">Follow the changes. Read the source.</p><p>Browse the patch series published with each release, from its first commit to individual file changes.</p></div>
${newest ? `<section class="patch-featured" aria-labelledby="patch-latest"><div><p class="patch-eyebrow">Newest available patch</p><h2 id="patch-latest">${e(newest.tag)}</h2><p>${date(newest)} · Core line ${e(coreLine(newest))} · ${size(newest)}</p>${status(newest)}</div><a class="patch-action" href="${local(`${newest.tag}/`)}">Browse patch series <span aria-hidden="true">→</span></a></section>` : ""}
<section aria-labelledby="patch-releases"><h2 id="patch-releases">All releases</h2><p>Newest publication first, across all Core lines. Missing historical patches remain listed.</p>
${ordered.length ? filterControls({ kind: "releases", label: "Search releases", placeholder: "Release tag or Core line", options: [...new Set(ordered.map(coreLine))], optionLabel: "Core line (release tag)" }) : ""}
${ordered.length ? `<ol class="patch-release-list">${ordered.map((release) => `<li data-filter-item data-search="${e(`${release.tag} ${coreLine(release)}`)}" data-group="${e(coreLine(release))}"><a href="${local(`${release.tag}/`)}"><strong>${e(release.tag)}</strong><span class="patch-release-meta">${date(release)} · Core line ${e(coreLine(release))}${release.patch ? ` · ${size(release)}` : ""}</span>${status(release)}<span class="patch-row-arrow" aria-hidden="true">→</span></a></li>`).join("")}</ol>` : '<p class="patch-empty">No public releases are listed yet. Check the <a href="https://github.com/LuganoPlanB/bitcoin-roots/releases">release page</a> for publication updates.</p>'}</section>
<aside class="patch-explanation"><h2>What am I looking at?</h2><p>${e(seriesExplanation)}</p></aside>`;
}

export function releaseOverview(release, series, local) {
    const links = [link(release.url, "Release notes")];
    if (release.patch) links.unshift(sourceLink(release));
    if (release.checksum) links.push(link(release.checksum.url, "SHA512SUMS"));
    const header = `<nav class="patch-breadcrumb" aria-label="Breadcrumb">${link(local(""), "All releases")}<span aria-current="page">${e(release.tag)}</span></nav>
<div class="patch-intro"><p class="patch-eyebrow">Published patch series</p><h1>${e(release.tag)}</h1>${status(release)}<p>${date(release)} · Core line <strong>${e(coreLine(release))}</strong> (from release tag)</p></div>
<nav class="patch-resource-links" aria-label="Release resources">${links.join("")}</nav>`;
    if (!series) return `${header}<section class="patch-explanation"><h2>Patch unavailable</h2><p>No patch asset was published for this release. Its release notes remain available; a patch has not been reconstructed from later source history.</p></section>`;
    const paths = series.commits.flatMap((commit) => commit.files.map((file) => file.path ?? "Unsupported path"));
    const subsystems = [...new Set(paths.map((path) => path.includes("/") ? path.split("/")[0] : "(root files)"))].sort();
    return `${header}<dl class="patch-stats"><div><dt>Ordered commits</dt><dd>${series.commits.length}</dd></div><div><dt>File changes, including repeats</dt><dd>${series.fileCount}</dd></div><div><dt>Published patch</dt><dd>${size(release)}</dd></div></dl>
<aside class="patch-explanation"><h2>A series, in commit order</h2><p>${e(seriesExplanation)}</p><details><summary>Download verification</summary><p>SHA512 checksum: ${e(release.integrity.checksum)} · GitHub SHA256 digest: ${e(release.integrity.platformDigest)} · Signature: not verified.</p><p>Matching checksums establish byte integrity, not authenticated signature verification.</p></details></aside>
<section aria-labelledby="patch-changes"><h2 id="patch-changes">Explore the changes</h2><p>Open a commit to see its files, or search a path across the entire series. Path groups follow the published filenames.</p>
${filterControls({ kind: "files", label: "Search file paths", placeholder: "For example: src/policy", options: subsystems, optionLabel: "Path group" })}
<ol class="patch-commit-list">${series.commits.map((commit) => `<li data-commit-item id="${commit.id}"><details class="patch-commit"><summary><span class="patch-ordinal">${commit.ordinal.toString().padStart(2, "0")}</span><span><strong>${e(commit.subject)}</strong><span class="patch-release-meta">${commit.files.length} file changes · ${e(commit.author ?? "Author not supplied")}</span></span></summary><div class="patch-commit-content"><p>${link(local(`${release.tag}/commit-${commit.ordinal}/`), "Open commit overview")}</p>${commit.files.length ? `<ul class="patch-file-list">${commit.files.map((file) => { const path = file.path ?? "Unsupported path"; return `<li data-filter-item data-search="${e(path)}" data-group="${e(path.includes("/") ? path.split("/")[0] : "(root files)")}">${link(local(`${release.tag}/commit-${commit.ordinal}/file-${file.ordinal}/`), path)}<span class="patch-file-state">${e(file.state)}</span></li>`; }).join("")}</ul>` : '<p>No diff section in this mail. Consult the original patch.</p>'}</div></details></li>`).join("")}</ol>${series.commits.length ? "" : '<p class="patch-empty">This published series contains no commits.</p>'}</section>`;
}
