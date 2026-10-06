import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { copyFile, mkdtemp, mkdir, readFile, readdir, rm, symlink, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import test from "node:test";
import { parsePatchSeries } from "../../scripts/patch-series.mjs";
import { renderFilePages, renderLimits, rendererSettings, toDiffFile } from "../../scripts/patch-renderer.mjs";
import { renderPatchSite, rendererVersion, validateRenderCatalogue, websiteRoot } from "../../scripts/render-patches.mjs";
import { patchCacheKey } from "../../scripts/patch-cache.mjs";
import { patchDocument, validateBase } from "../../scripts/patch-page.mjs";
import { checkPatchHtml } from "../../scripts/check-patch-html.mjs";

const tag = "v29.4-roots.4";
const mail = (diff, subject = "[PATCH] Fixture") => `From ${"a".repeat(40)} Mon Sep 17 00:00:00 2001\nFrom: <script>author</script>\nDate: 2026-10-01\nSubject: ${subject}\n\nMessage.\n---\n\n${diff}-- \n2.43.0\n`;
const diff = (content = "+safe\n", path = "source.txt") => `diff --git a/${path} b/${path}\n--- /dev/null\n+++ b/${path}\n@@ -0,0 +10,${content.split("\n").length - 1} @@\n${content}`;
const parseFile = (text) => parsePatchSeries(mail(text), tag).commits[0].files[0];

async function inputs(t, patch = mail(diff())) {
    const directory = await mkdtemp(join(tmpdir(), "roots-renderer-"));
    t.after(() => rm(directory, { recursive: true, force: true }));
    const inputDir = join(directory, "inputs");
    await mkdir(join(inputDir, "assets"), { recursive: true });
    const bytes = Buffer.from(patch);
    const sha512 = createHash("sha512").update(bytes).digest("hex");
    const cacheFile = `${"b".repeat(64)}-${sha512}.bin`;
    await writeFile(join(inputDir, "assets", cacheFile), bytes);
    const release = { tag, url: `https://github.com/LuganoPlanB/bitcoin-roots/releases/tag/${tag}`,
        prerelease: true, publishedAt: "2026-10-01T12:00:00Z", patchState: "available",
        patch: { name: `bitcoin-roots-${tag.slice(1)}.patch`, size: bytes.length,
            url: `https://github.com/LuganoPlanB/bitcoin-roots/releases/download/${tag}/bitcoin-roots-${tag.slice(1)}.patch` },
        integrity: { sha512, cacheFile, checksum: "verified", platformDigest: "missing", signature: "not-verified" } };
    const catalogue = { schemaVersion: 1, repository: "LuganoPlanB/bitcoin-roots", releases: [release,
        { tag: "v29.3-roots.1", url: "https://github.com/LuganoPlanB/bitcoin-roots/releases/tag/v29.3-roots.1",
            prerelease: false, publishedAt: "2026-09-01T12:00:00Z", patchState: "unavailable", patch: null }] };
    await writeFile(join(inputDir, "catalogue.json"), JSON.stringify(catalogue));
    return { directory, inputDir, catalogue, outputDir: join(directory, "output") };
}

test("counted JSON adapter preserves source lines, numbers and no-newline marker location", () => {
    const file = parseFile("diff --git a/file b/file\n--- a/file\n+++ b/file\n@@ -20,2 +30,2 @@\n--- old filename-like source\n+++ new filename-like source\n context\\ No newline at end of file inside source\n\\ No newline at end of file\n");
    const data = toDiffFile(file);
    assert.equal(data.blocks[0].lines.length, 4);
    assert.deepEqual(data.blocks[0].lines.map((line) => [line.oldNumber, line.newNumber]), [[20, undefined], [undefined, 30], [21, 31], [undefined, undefined]]);
    const page = renderFilePages(file).pages[0];
    assert.ok(page.unified.includes("old filename-like source"));
    assert.ok(page.unified.includes("inside source"));
    assert.ok(page.unified.includes("No newline at end of file"));
    assert.equal(rendererSettings.matching, "none");
});

test("literal script/style/event markup and Vue expressions remain inert visible text in both views", () => {
    const content = '+</script><script>alert("x")</script>\n+</style><img src=x onerror=alert(1)>\n+{{ globalThis.process.exit() }}\n';
    const file = parseFile(diff(content, '../<img onerror="alert(1)">.txt'));
    const rendered = renderFilePages(file);
    for (const html of [rendered.pages[0].unified, rendered.pages[0].sideBySide]) {
        assert.doesNotMatch(html, /<(?:script|style|img)\b/i);
        assert.doesNotMatch(html, /<[^>]+\bon(?:error|load|click)\s*=/i);
        assert.ok(html.includes("&lt;"));
        assert.ok(html.includes("{{ globalThis.process.exit() }}"));
    }
});

test("ordinary large hunks paginate without dropping rows or renumbering source lines", () => {
    const file = parseFile(diff(Array.from({ length: 1001 }, (_, i) => `+row-${i}`).join("\n") + "\n"));
    const result = renderFilePages(file);
    assert.equal(result.pages.length, 3);
    assert.equal(result.lineCount, 1001);
    assert.deepEqual(result.pages.map((page) => [page.start, page.end]), [[0, 400], [400, 800], [800, 1001]]);
    assert.ok(result.pages[0].unified.includes("row-0"));
    assert.ok(result.pages[2].unified.includes("row-1000"));
    assert.match(result.pages[1].unified, /\b410\b/);
    assert.ok(result.pages[1].unified.includes("(continued)"));
    assert.ok(result.pages.every((page) => Buffer.byteLength(page.unified) < renderLimits.diffHtmlBytes));
});

test("HTML expansion splits pages further; long source lines get explicit complete-download fallback", () => {
    const file = parseFile(diff(Array(20).fill(`+${"&".repeat(100)}`).join("\n") + "\n"));
    const result = renderFilePages(file, { ...renderLimits, diffHtmlBytes: 7000 });
    assert.ok(result.pages.length > 1);
    assert.equal(result.pages.reduce((sum, page) => sum + page.end - page.start, 0), 20);
    assert.ok(result.pages.every((page) => Buffer.byteLength(page.unified) <= 7000 && Buffer.byteLength(page.sideBySide) <= 7000));
    const oversized = renderFilePages(parseFile(diff(`+${"x".repeat(4097)}\n`)));
    assert.equal(oversized.state, "oversized");
    assert.equal(oversized.pages.length, 0);
    assert.match(oversized.reason, /original patch/);
});

test("complete standalone routes provide static navigation and download access at both base paths", async (t) => {
    const fixture = await inputs(t, mail(diff(Array.from({ length: 405 }, (_, i) => `+item-${i}`).join("\n") + "\n")));
    for (const base of ["/", "/bitcoin-roots/"]) {
        const result = await renderPatchSite({ ...fixture, base, siteStyles: ["assets/style.abc.css"] });
        assert.equal(result.releases.length, 2);
        assert.equal(result.releases[0].fileCount, 1);
        const routes = ["", `${tag}/`, `${tag}/commit-1/`, `${tag}/commit-1/file-1/`, `${tag}/commit-1/file-1/page-2/`,
            `${tag}/commit-1/file-1/side-by-side/`, `${tag}/commit-1/file-1/side-by-side/page-2/`, "v29.3-roots.1/"];
        assert.equal(result.pageCount, routes.length);
        const checked = await checkPatchHtml({ outputDir: fixture.outputDir });
        assert.equal(checked.pages, routes.length);
        assert.equal(checked.fileChanges, 1);
        for (const route of routes) {
            const html = await readFile(join(fixture.outputDir, route, "index.html"), "utf8");
            assert.ok(html.includes(`<link rel="stylesheet" href="${base}assets/style.abc.css">`));
            assert.match(html, new RegExp(`<script src="${base}patches/patch-browser.js" defer></script>`));
            assert.doesNotMatch(html, /<script(?! src=)|modulepreload|prefetch|javascript:/i);
            for (const href of html.matchAll(/href="([^"]+)"/g)) {
                if (href[1].startsWith("#") || href[1].startsWith("https:")) continue;
                assert.ok(href[1].startsWith(base), href[1]);
                const path = href[1].slice(`${base}patches/`.length);
                if (href[1].startsWith(`${base}patches/`)) await readFile(join(fixture.outputDir, path, path.endsWith("/") || path === "" ? "index.html" : ""));
            }
        }
        const last = await readFile(join(fixture.outputDir, `${tag}/commit-1/file-1/page-2/index.html`), "utf8");
        assert.ok(last.includes("item-404"));
        assert.ok(last.includes("Download original patch"));
        assert.ok(last.includes("Previous part"));
        const commit = await readFile(join(fixture.outputDir, `${tag}/commit-1/index.html`), "utf8");
        assert.ok(commit.includes("&lt;script&gt;author&lt;/script&gt;"));
        const catalogue = await readFile(join(fixture.outputDir, "catalogue.json"), "utf8");
        assert.doesNotMatch(catalogue, /item-404|diff --git/);
    }
});

test("static acceptance rejects broken deep links, unexplained routes and external runtime scripts", async (t) => {
    const fixture = await inputs(t);
    await renderPatchSite(fixture);
    const path = join(fixture.outputDir, "index.html");
    const html = await readFile(path, "utf8");
    await writeFile(path, html.replace('href="/patches/v29.4-roots.4/"', 'href="/patches/nonexistent/"'));
    await assert.rejects(checkPatchHtml({ outputDir: fixture.outputDir }), /Broken patch link/);
    await writeFile(path, html.replace('src="/patches/patch-browser.js"', 'src="https://cdn.example/browser.js"'));
    await assert.rejects(checkPatchHtml({ outputDir: fixture.outputDir }), /External runtime/);
    await writeFile(path, html);
    await mkdir(join(fixture.outputDir, "unexpected"));
    await writeFile(join(fixture.outputDir, "unexpected/index.html"), html);
    await assert.rejects(checkPatchHtml({ outputDir: fixture.outputDir }), /page count mismatch/);
});

test("binary, metadata-only, unsupported and oversized changes publish explicit source-linked pages", async (t) => {
    const patch = mail("diff --git a/image b/image\nGIT binary patch\nliteral 0\nHcmV?d00001\n"
        + "diff --git a/empty b/empty\nnew file mode 100644\nindex 0000000..e69de29\n"
        + "diff --cc unsupported\n@@@ -1 -1 +1 @@@\n++unknown\n"
        + diff(`+${"x".repeat(4097)}\n`, "too-long"));
    const fixture = await inputs(t, patch);
    const result = await renderPatchSite(fixture);
    assert.deepEqual(result.releases[0].commits[0].files.map((file) => file.state), ["binary", "metadata-only", "unsupported", "oversized"]);
    for (let ordinal = 1; ordinal <= 4; ordinal++) {
        const html = await readFile(join(fixture.outputDir, `${tag}/commit-1/file-${ordinal}/index.html`), "utf8");
        assert.ok(html.includes("patch-fallback"));
        assert.ok(html.includes("Download original patch"));
    }
});

test("malformed URLs/cache paths/tags cannot escape output; corrupted bytes preserve previous output", async (t) => {
    const fixture = await inputs(t);
    for (const mutate of [
        (r) => r.tag = "../../escape",
        (r) => r.patch.url = "javascript:alert(1)",
        (r) => r.integrity.cacheFile = "../../secret",
        (r) => r.url = "https://attacker.test",
        (r) => r.patchState = "invented",
        (r) => r.checksum = { url: "javascript:alert(1)" },
    ]) {
        const catalogue = structuredClone(fixture.catalogue);
        mutate(catalogue.releases[0]);
        assert.throws(() => validateRenderCatalogue(catalogue));
    }
    assert.throws(() => validateBase('//attacker/'));
    assert.throws(() => renderFilePages(parseFile(diff()), { ...renderLimits, linesPerPage: 0 }), /limits/);
    assert.throws(() => patchDocument({ title: "a", body: "b", base: "/", siteStyles: ['" onload="alert(1)'] }));
    await renderPatchSite(fixture);
    const previous = await readFile(join(fixture.outputDir, "index.html"));
    await writeFile(join(fixture.inputDir, "assets", fixture.catalogue.releases[0].integrity.cacheFile), "corrupt");
    await assert.rejects(renderPatchSite(fixture), /integrity/);
    assert.deepEqual(await readFile(join(fixture.outputDir, "index.html")), previous);
    assert.ok(!(await readdir(fixture.directory)).some((file) => file.includes("tmp-")));
});

test("empty prepared inventory renders an honest static index and passes route acceptance", async (t) => {
    const fixture = await inputs(t);
    await writeFile(join(fixture.inputDir, "catalogue.json"), JSON.stringify({ ...fixture.catalogue, releases: [] }));
    const result = await renderPatchSite(fixture);
    assert.equal(result.pageCount, 1);
    const html = await readFile(join(fixture.outputDir, "index.html"), "utf8");
    assert.match(html, /class="patch-empty"/);
    assert.doesNotMatch(html, /class="patch-featured"/);
    assert.doesNotMatch(html, /data-patch-filter=/);
    assert.equal((await checkPatchHtml({ outputDir: fixture.outputDir })).releases, 0);
});

test("warm output cache is byte-identical and binds input digest, renderer/settings and template sources", async (t) => {
    const fixture = await inputs(t);
    const cacheDir = join(fixture.directory, "cache");
    const first = await renderPatchSite({ ...fixture, cacheDir });
    assert.equal(first.cache, "miss");
    const before = await readFile(join(fixture.outputDir, `${tag}/commit-1/file-1/index.html`));
    const second = await renderPatchSite({ ...fixture, cacheDir });
    assert.equal(second.cache, "hit");
    assert.equal(second.cacheKey, first.cacheKey);
    assert.deepEqual(await readFile(join(fixture.outputDir, `${tag}/commit-1/file-1/index.html`)), before);
    const keyInputs = { websiteRoot, catalogue: fixture.catalogue, base: "/", siteStyles: [], limits: renderLimits, rendererVersion, rendererSettings };
    const key = await patchCacheKey(keyInputs);
    assert.equal(key, first.cacheKey);
    for (const changes of [
        { rendererVersion: "next-renderer" },
        { rendererSettings: { ...rendererSettings, matching: "lines" } },
        { limits: { ...renderLimits, linesPerPage: 100 } },
        { base: "/bitcoin-roots/" },
        { siteStyles: ["assets/style.new.css"] },
        { catalogue: { ...fixture.catalogue, releases: fixture.catalogue.releases.map((r) => ({ ...r, prerelease: !r.prerelease })) } },
    ]) assert.notEqual(await patchCacheKey({ ...keyInputs, ...changes }), key);
    const changed = await renderPatchSite({ ...fixture, cacheDir, limits: { ...renderLimits, linesPerPage: 100 } });
    assert.equal(changed.cache, "miss");
    assert.notEqual(changed.cacheKey, key);
    const alternateSources = join(fixture.directory, "sources");
    for (const path of ["scripts/patch-series.mjs", "scripts/patch-renderer.mjs", "scripts/patch-page.mjs", "scripts/render-patches.mjs",
        "scripts/patch-cache.mjs", "scripts/patch-overview.mjs", "scripts/patch-browser.js", "scripts/patch-theme.js", ".vitepress/theme/patches.css", "package-lock.json"]) {
        await mkdir(join(alternateSources, path, ".."), { recursive: true });
        await copyFile(join(websiteRoot, path), join(alternateSources, path));
    }
    assert.equal(await patchCacheKey({ ...keyInputs, websiteRoot: alternateSources }), key);
    await writeFile(join(alternateSources, "scripts/patch-page.mjs"), "changed trusted template");
    assert.notEqual(await patchCacheKey({ ...keyInputs, websiteRoot: alternateSources }), key);
    await writeFile(join(fixture.inputDir, "assets", fixture.catalogue.releases[0].integrity.cacheFile), "bad original bytes");
    await assert.rejects(renderPatchSite({ ...fixture, cacheDir }), /integrity/);
});

test("corrupt, incomplete, path-tampered and symlink cache objects regenerate from verified inputs", async (t) => {
    const fixture = await inputs(t);
    const cacheDir = join(fixture.directory, "cache");
    const first = await renderPatchSite({ ...fixture, cacheDir });
    const entry = join(cacheDir, first.cacheKey);
    const page = join(entry, "pages", `${tag}/commit-1/file-1/index.html`);
    await writeFile(page, '<script>alert("cache corruption")</script>');
    assert.equal((await renderPatchSite({ ...fixture, cacheDir })).cache, "miss");
    assert.doesNotMatch(await readFile(page, "utf8"), /<script(?! src=)/);
    await rm(page);
    assert.equal((await renderPatchSite({ ...fixture, cacheDir })).cache, "miss");
    const manifestPath = join(entry, "manifest.json");
    const manifest = JSON.parse(await readFile(manifestPath));
    manifest.files[0].path = "../escape";
    await writeFile(manifestPath, JSON.stringify(manifest));
    assert.equal((await renderPatchSite({ ...fixture, cacheDir })).cache, "miss");
    await rm(page);
    await symlink(join(fixture.outputDir, `${tag}/commit-1/file-1/index.html`), page);
    assert.equal((await renderPatchSite({ ...fixture, cacheDir })).cache, "miss");
    assert.equal((await renderPatchSite({ ...fixture, cacheDir })).cache, "hit");
});
