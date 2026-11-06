import { releaseVersionTag } from "./release-catalogue.mjs";
import { validateSiteShell } from "./patch-site-shell.mjs";

export function escapeHtml(value) {
    return String(value).replaceAll("&", "&amp;").replaceAll("<", "&lt;").replaceAll(">", "&gt;")
        .replaceAll('"', "&quot;").replaceAll("'", "&#39;");
}

export function validateBase(base) {
    if (typeof base !== "string" || !/^\/(?:[a-zA-Z0-9_-]+\/)*$/.test(base)) throw new Error("Invalid website base path");
    return base;
}

// Only the renderer's HTML and these trusted templates cross the HTML boundary.
// Patch metadata is always escaped text; none of these pages enters a compiler.
export function patchDocument({ title, body, base, siteStyles = [], siteShell }) {
    validateBase(base);
    validateSiteShell(siteShell);
    if (siteShell.base !== base) throw new Error("Shared website shell base mismatch");
    const styles = [...new Set([...siteStyles, ...siteShell.styles])];
    if (styles.some((path) => !/^assets\/[A-Za-z0-9_.-]+\.css$/.test(path))) throw new Error("Invalid site stylesheet path");
    return `<!doctype html>\n<html lang="${siteShell.lang}"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">\n<title>${escapeHtml(title)} · Bitcoin Roots</title>
<meta name="color-scheme" content="light dark">
<script src="${base}patches/patch-theme.js"></script>
${styles.map((path) => `<link rel="stylesheet" href="${base}${path}">`).join("\n")}
<link rel="stylesheet" href="${base}patches/diff2html.css"><link rel="stylesheet" href="${base}patches/patches.css">
<script type="module" src="${base}${siteShell.script}"></script>
<script src="${base}patches/patch-browser.js" defer></script>
</head><body class="roots-patches">${siteShell.layout}${siteShell.skip}
<div id="patch-site-header" data-site-data="${base}${siteShell.data}">${siteShell.header}</div>
<main id="VPContent" class="patch-content" tabindex="-1"><div id="patch-content">${body}</div></main>
${siteShell.footer}</div></body></html>\n`;
}

export const link = (href, label) => `<a href="${escapeHtml(href)}">${escapeHtml(label)}</a>`;
export const sourceLink = (release) => link(release.patch.url, "Download original patch");
export const releaseLabel = (release) => releaseVersionTag(release);
export const seriesExplanation = "This ordered patch series shows Roots changes relative to the release’s stated base. It is not a delta from the previous Roots release; repeated file changes are counted separately. The download preserves the published export. Browse a release’s file entries to see its included paths; historical exports may differ in scope. Consult the release notes for context.";

export const foldingExplanation = "Roots maintenance releases on the same exact Core base append fixes and preserve published history. Folding incorporates those fixes into thematic commits when porting to a new Core base; it is separate from promotion to main. Existing release patches and their URLs remain unchanged.";

export function fileNavigation({ fileRoute, page, pageCount, view }) {
    const route = (number, selectedView) => `${fileRoute}${selectedView === "side-by-side" ? "side-by-side/" : ""}${number > 1 ? `page-${number}/` : ""}`;
    const previous = page > 1 ? link(route(page - 1, view), "Previous part") : "";
    const next = page < pageCount ? link(route(page + 1, view), "Next part") : "";
    const viewLink = (selectedView, label) => `<a href="${escapeHtml(route(page, selectedView))}"${view === selectedView ? ' aria-current="page"' : ""}>${label}</a>`;
    return `<nav class="patch-view-nav" aria-label="Diff view">${viewLink("unified", "Unified")}${viewLink("side-by-side", "Side by side")}</nav>
<nav class="patch-pagination" aria-label="Diff parts">${previous}<span>Part ${page} of ${pageCount}</span>${next}</nav>`;
}
