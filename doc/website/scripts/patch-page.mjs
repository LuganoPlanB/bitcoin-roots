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
export function patchDocument({ title, body, base, siteStyles = [] }) {
    validateBase(base);
    if (siteStyles.some((path) => !/^assets\/[A-Za-z0-9_.-]+\.css$/.test(path))) throw new Error("Invalid site stylesheet path");
    return `<!doctype html>\n<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">\n<title>${escapeHtml(title)} · Bitcoin Roots</title>
<meta name="color-scheme" content="light dark">
<script src="${base}patches/patch-theme.js"></script>
${siteStyles.map((path) => `<link rel="stylesheet" href="${base}${path}">`).join("\n")}
<link rel="stylesheet" href="${base}patches/diff2html.css"><link rel="stylesheet" href="${base}patches/patches.css">
<script src="${base}patches/patch-browser.js" defer></script>
</head><body class="roots-patches"><a class="patch-skip" href="#patch-content">Skip to content</a>
<header class="patch-header"><a href="${base}">Bitcoin Roots</a><div class="patch-header-controls"><nav aria-label="Website"><a href="${base}getting-started">Get started</a><a href="${base}documentation">Documentation</a><a href="${base}patches/" aria-current="page">Patches</a></nav><label class="patch-appearance" hidden>Appearance<select data-patch-appearance><option value="auto">System</option><option value="light">Light</option><option value="dark">Dark</option></select></label></div></header>
<main id="patch-content">${body}</main><footer>Bitcoin Roots · Plan ₿ Foundation · MIT License</footer></body></html>\n`;
}

export const link = (href, label) => `<a href="${escapeHtml(href)}">${escapeHtml(label)}</a>`;
export const sourceLink = (release) => link(release.patch.url, "Download original patch");
export const seriesExplanation = "This ordered patch series shows Roots changes relative to the release’s stated base. It is not a delta from the previous Roots release; repeated file changes are counted separately. The download preserves the published export. Browse a release’s file entries to see its included paths; historical exports may differ in scope. Consult the release notes for context.";

export function fileNavigation({ fileRoute, page, pageCount, view }) {
    const route = (number, selectedView) => `${fileRoute}${selectedView === "side-by-side" ? "side-by-side/" : ""}${number > 1 ? `page-${number}/` : ""}`;
    const previous = page > 1 ? link(route(page - 1, view), "Previous part") : "";
    const next = page < pageCount ? link(route(page + 1, view), "Next part") : "";
    const viewLink = (selectedView, label) => `<a href="${escapeHtml(route(page, selectedView))}"${view === selectedView ? ' aria-current="page"' : ""}>${label}</a>`;
    return `<nav class="patch-view-nav" aria-label="Diff view">${viewLink("unified", "Unified")}${viewLink("side-by-side", "Side by side")}</nav>
<nav class="patch-pagination" aria-label="Diff parts">${previous}<span>Part ${page} of ${pageCount}</span>${next}</nav>`;
}
