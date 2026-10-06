import { createHash } from "node:crypto";
import { createRequire } from "node:module";
import { copyFile, mkdir, readFile, readdir, rename, rm, writeFile } from "node:fs/promises";
import { join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { expectedPatchName, validateReleaseUrl, validateTag } from "./release-catalogue.mjs";
import { defaultInputDir } from "./prepare-releases.mjs";
import { parsePatchSeries } from "./patch-series.mjs";
import { renderFilePages, renderLimits, rendererSettings } from "./patch-renderer.mjs";
import { patchCacheKey, restorePatchCache, storePatchCache } from "./patch-cache.mjs";
import { escapeHtml as e, fileNavigation, link, patchDocument, sourceLink, validateBase } from "./patch-page.mjs";
import { orderedReleases, releaseIndex, releaseOverview } from "./patch-overview.mjs";

const require = createRequire(import.meta.url);
export const rendererVersion = require("diff2html/package.json").version;
export const websiteRoot = fileURLToPath(new URL("../", import.meta.url));

export function validateRenderCatalogue(catalogue) {
    if (catalogue?.schemaVersion !== 1 || catalogue.repository !== "LuganoPlanB/bitcoin-roots" || !Array.isArray(catalogue.releases)) throw new Error("Invalid rendering catalogue");
    const tags = new Set();
    for (const release of catalogue.releases) {
        validateTag(release.tag);
        validateReleaseUrl(release.url, release.tag);
        if (tags.has(release.tag)) throw new Error("Duplicate rendering release tag");
        tags.add(release.tag);
        if (typeof release.prerelease !== "boolean" || !/^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z$/.test(release.publishedAt)) throw new Error("Invalid rendering release metadata");
        if (release.checksum) validateReleaseUrl(release.checksum.url, release.tag, "SHA512SUMS");
        if (release.patchState === "unavailable" && release.patch === null) continue;
        if (release.patchState !== "available" || release.patch?.name !== expectedPatchName(release.tag)) throw new Error("Invalid rendering patch state");
        if (!Number.isSafeInteger(release.patch.size) || release.patch.size < 0 || release.patch.size > 20 * 1024 * 1024) throw new Error("Invalid rendering patch byte size");
        validateReleaseUrl(release.patch.url, release.tag, release.patch.name);
        if (!/^[a-f0-9]{128}$/.test(release.integrity?.sha512)
            || !/^[a-f0-9]{64}-[a-f0-9]{128}\.bin$/.test(release.integrity?.cacheFile)
            || !["verified", "missing"].includes(release.integrity?.checksum)
            || !["verified", "missing"].includes(release.integrity?.platformDigest)
            || release.integrity?.signature !== "not-verified") throw new Error("Invalid verified rendering input");
    }
    return catalogue;
}

export async function renderPatchSite({ inputDir = defaultInputDir, outputDir, cacheDir, base = "/", siteStyles = [], limits = renderLimits } = {}) {
    validateBase(base);
    if (typeof outputDir !== "string" || !outputDir || ![limits.documentBytes, limits.overviewBytes].every((value) => Number.isSafeInteger(value) && value > 0)) throw new Error("Invalid patch output settings");
    const catalogue = validateRenderCatalogue(JSON.parse(await readFile(join(inputDir, "catalogue.json"), "utf8")));
    // A warm output cache never substitutes for verifying the original inputs.
    for (const release of catalogue.releases.filter((release) => release.patch)) {
        const bytes = await readFile(join(inputDir, "assets", release.integrity.cacheFile));
        if (bytes.length !== release.patch.size || createHash("sha512").update(bytes).digest("hex") !== release.integrity.sha512) throw new Error(`Rendering input integrity mismatch for ${release.tag}`);
    }
    const key = await patchCacheKey({ websiteRoot, catalogue, base, siteStyles, limits, rendererVersion, rendererSettings });
    const temporary = `${outputDir}.tmp-${process.pid}`;
    await mkdir(temporary, { recursive: true });
    const summary = { rendererVersion, base, pageCount: 0, htmlBytes: 0, largestPageBytes: 0, releases: [] };
    const local = (route) => `${base}patches/${route}`;
    const page = async (route, title, body, overview = false) => {
        if (route && !/^(?:[a-zA-Z0-9_.-]+\/)+$/.test(route)) throw new Error("Unsafe generated patch route");
        const document = patchDocument({ title, body, base, siteStyles });
        const bytes = Buffer.byteLength(document);
        if (bytes >= (overview ? limits.overviewBytes : limits.documentBytes)) throw new Error(`Generated patch page exceeds HTML budget (${route || "index"})`);
        const directory = join(temporary, route);
        await mkdir(directory, { recursive: true });
        await writeFile(join(directory, "index.html"), document);
        summary.pageCount++;
        summary.htmlBytes += bytes;
        summary.largestPageBytes = Math.max(summary.largestPageBytes, bytes);
    };
    try {
        const cached = await restorePatchCache({ cacheDir, key, temporary });
        if (cached) {
            await rm(outputDir, { recursive: true, force: true });
            await rename(temporary, outputDir);
            return { ...cached, cache: "hit", cacheKey: key };
        }
        await copyFile(require.resolve("diff2html/bundles/css/diff2html.min.css"), join(temporary, "diff2html.css"));
        await copyFile(join(websiteRoot, ".vitepress/theme/patches.css"), join(temporary, "patches.css"));
        await copyFile(join(websiteRoot, "scripts/patch-browser.js"), join(temporary, "patch-browser.js"));
        await copyFile(join(websiteRoot, "scripts/patch-theme.js"), join(temporary, "patch-theme.js"));
        await page("", "Release patches", releaseIndex(catalogue.releases, local), true);
        for (const release of orderedReleases(catalogue.releases)) {
            const releaseRoute = `${release.tag}/`;
            const releaseTitle = `${release.tag} patch series`;
            if (release.patchState === "unavailable") {
                await page(releaseRoute, releaseTitle, releaseOverview(release, null, local), true);
                summary.releases.push({ tag: release.tag, state: "unavailable", commits: [], fileCount: 0 });
                continue;
            }
            const bytes = await readFile(join(inputDir, "assets", release.integrity.cacheFile));
            if (bytes.length !== release.patch.size || createHash("sha512").update(bytes).digest("hex") !== release.integrity.sha512) throw new Error(`Rendering input integrity mismatch for ${release.tag}`);
            const series = parsePatchSeries(bytes, release.tag);
            const entry = { tag: release.tag, state: "available", sha512: release.integrity.sha512, fileCount: series.fileCount, commits: [] };
            const releaseLinks = `${sourceLink(release)} · ${link(release.url, "Release notes")}`;
            await page(releaseRoute, releaseTitle, releaseOverview(release, series, local), true);
            for (const commit of series.commits) {
                const commitRoute = `${releaseRoute}commit-${commit.ordinal}/`;
                const commitEntry = { id: commit.id, ordinal: commit.ordinal, originalCommit: commit.originalCommit,
                    subject: commit.subject, author: commit.author, date: commit.date, route: local(commitRoute), files: [] };
                const commitHeader = `<p>${link(local(releaseRoute), release.tag)} · Commit ${commit.ordinal} of ${series.commits.length}</p>
<h1>${e(commit.subject)}</h1><p>${e(commit.author ?? "Author not supplied")}</p><p>${e(commit.date ?? "Date not supplied")}</p><p>Original commit: <code>${commit.originalCommit}</code></p>`;
                await page(commitRoute, commit.subject, `${commitHeader}<p>${releaseLinks}</p><p>${commit.files.length} file changes${commit.files.length ? "" : " · No diff section in this mail; consult the original patch."}</p>
<ol>${commit.files.map((file) => `<li id="${file.id}">${link(local(`${commitRoute}file-${file.ordinal}/`), file.path ?? "Unsupported path")} · ${e(file.state)}</li>`).join("")}</ol>`, true);
                for (const file of commit.files) {
                    const fileRoute = `${commitRoute}file-${file.ordinal}/`;
                    const rendered = renderFilePages(file, limits);
                    const fileEntry = { id: file.id, ordinal: file.ordinal, oldPath: file.oldPath, newPath: file.newPath,
                        path: file.path, state: rendered.state, reason: rendered.reason ?? null,
                        route: local(fileRoute), pageCount: rendered.pages.length || 1 };
                    commitEntry.files.push(fileEntry);
                    const fileHeader = `<p>${link(local(releaseRoute), release.tag)} · ${link(local(commitRoute), `Commit ${commit.ordinal}: ${commit.subject}`)}</p>
<h1 class="patch-file-heading" id="${file.id}">${e(file.path ?? "Unsupported file path")}</h1>
<p>Before: <code>${e(file.oldPath ?? "/dev/null")}</code> · After: <code>${e(file.newPath ?? "/dev/null")}</code></p>
${file.metadata.length ? `<details><summary>File metadata</summary><pre>${e(file.metadata.join("\n"))}</pre></details>` : ""}<p>${releaseLinks}</p>`;
                    if (!rendered.pages.length) {
                        const reasons = { binary: "Binary change. Text diff display is unavailable; the original patch includes the binary data.",
                            "metadata-only": "Metadata-only change. No text hunk is present; file modes, rename/copy information or empty-file metadata are shown above." };
                        await page(fileRoute, file.path ?? "Unsupported file", `${fileHeader}<p class="patch-fallback">${e(rendered.reason ?? reasons[rendered.state] ?? "Unsupported change; consult the original patch.")}</p>`);
                        continue;
                    }
                    for (let index = 0; index < rendered.pages.length; index++) {
                        const part = rendered.pages[index];
                        for (const view of ["unified", "side-by-side"]) {
                            const route = `${fileRoute}${view === "side-by-side" ? "side-by-side/" : ""}${index ? `page-${index + 1}/` : ""}`;
                            const navigation = fileNavigation({ fileRoute: local(fileRoute), page: index + 1, pageCount: rendered.pages.length, view });
                            await page(route, file.path, `${fileHeader}${navigation}<p>Diff rows ${part.start + 1}–${part.end} of ${rendered.lineCount}. Line numbers refer to the original before/after files.</p>
<p class="patch-diff-legend"><span class="patch-added">+ Added</span><span class="patch-removed">− Removed</span><span>Unmarked lines are context.</span></p>
<p class="patch-scroll-hint" id="patch-scroll-hint">Scroll the code area horizontally to read long lines. ${view === "side-by-side" ? "Before is on the left; after is on the right. Unified view fits narrow screens more easily." : "The two line-number columns refer to before and after."}</p>
<section class="patch-diff" tabindex="0" aria-describedby="patch-scroll-hint" aria-label="${view === "unified" ? "Unified" : "Side by side"} code diff">${view === "side-by-side" ? '<div class="patch-side-headings" aria-hidden="true"><span>Before</span><span>After</span></div>' : ""}${view === "unified" ? part.unified : part.sideBySide}</section>${navigation}`);
                        }
                    }
                }
                entry.commits.push(commitEntry);
            }
            summary.releases.push(entry);
        }
        // Metadata only: never expose patch bodies in a global data/search bundle.
        await writeFile(join(temporary, "catalogue.json"), `${JSON.stringify(summary, null, 2)}\n`);
        await storePatchCache({ cacheDir, key, directory: temporary });
        await rm(outputDir, { recursive: true, force: true });
        await rename(temporary, outputDir);
        return { ...summary, cache: "miss", cacheKey: key };
    } catch (error) {
        await rm(temporary, { recursive: true, force: true });
        throw error;
    }
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
    try {
        const dist = join(websiteRoot, ".vitepress/dist");
        const assets = await readdir(join(dist, "assets"));
        const siteStyles = assets.filter((file) => /^style\.[A-Za-z0-9_-]+\.css$/.test(file)).sort().map((file) => `assets/${file}`);
        const result = await renderPatchSite({ outputDir: join(dist, "patches"), cacheDir: join(websiteRoot, ".vitepress/patch-cache"), base: process.env.DOCS_BASE || "/", siteStyles });
        console.log(`Rendered ${result.releases.length} releases, ${result.pageCount} static pages; largest HTML ${result.largestPageBytes} bytes; cache ${result.cache}.`);
    } catch (error) {
        console.error(`Patch rendering failed: ${error.message}`);
        process.exitCode = 1;
    }
}
