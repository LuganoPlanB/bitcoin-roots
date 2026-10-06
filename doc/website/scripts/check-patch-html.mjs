import assert from "node:assert/strict";
import { readFile, readdir, stat } from "node:fs/promises";
import { join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { renderLimits } from "./patch-renderer.mjs";
import { validateBase } from "./patch-page.mjs";
import { expectedPatchName } from "./release-catalogue.mjs";

async function htmlFiles(directory, prefix = "") {
    const result = [];
    for (const entry of await readdir(join(directory, prefix), { withFileTypes: true })) {
        const path = `${prefix}${entry.name}`;
        if (entry.isDirectory()) result.push(...await htmlFiles(directory, `${path}/`));
        else if (entry.isFile() && path.endsWith(".html")) result.push(path);
    }
    return result.sort();
}

export async function checkPatchHtml({ outputDir, siteDir } = {}) {
    const catalogue = JSON.parse(await readFile(join(outputDir, "catalogue.json"), "utf8"));
    const base = validateBase(catalogue.base);
    const prefix = `${base}patches/`;
    const actual = await htmlFiles(outputDir);
    const expected = new Set(["index.html"]);
    let fileChanges = 0;
    for (const release of catalogue.releases) {
        expected.add(`${release.tag}/index.html`);
        assert.equal(release.fileCount, release.commits.reduce((sum, commit) => sum + commit.files.length, 0), `File coverage mismatch: ${release.tag}`);
        for (const commit of release.commits) {
            assert.ok(commit.route.startsWith(prefix), "Commit base path mismatch");
            expected.add(`${commit.route.slice(prefix.length)}index.html`);
            for (const file of commit.files) {
                fileChanges++;
                assert.ok(file.route.startsWith(prefix), "File base path mismatch");
                const route = file.route.slice(prefix.length);
                for (let part = 1; part <= file.pageCount; part++) {
                    const page = part === 1 ? "" : `page-${part}/`;
                    expected.add(`${route}${page}index.html`);
                    if (file.state === "text") expected.add(`${route}side-by-side/${page}index.html`);
                }
            }
        }
    }
    assert.equal(actual.length, catalogue.pageCount, "Published page count mismatch");
    assert.deepEqual(actual, [...expected].sort(), "Missing or unexplained generated routes");
    const available = new Set(actual);
    let localLinks = 0;
    for (const path of actual) {
        const html = await readFile(join(outputDir, path), "utf8");
        assert.ok(html.startsWith("<!doctype html>"), `Not a standalone document: ${path}`);
        assert.equal((html.match(/<h1\b/g) ?? []).length, 1, `Heading hierarchy: ${path}`);
        const release = catalogue.releases.find((release) => path.startsWith(`${release.tag}/`));
        if (release) {
            const resource = `https://github.com/LuganoPlanB/bitcoin-roots/releases`;
            assert.ok(html.includes(`href="${resource}/tag/${release.tag}"`), `Missing release notes: ${path}`);
            const download = `href="${resource}/download/${release.tag}/${expectedPatchName(release.tag)}"`;
            assert.equal(html.includes(download), release.state === "available", `Incorrect original-download access: ${path}`);
        }
        assert.ok(Buffer.byteLength(html) < (path.includes("/file-") ? renderLimits.documentBytes : renderLimits.overviewBytes), `HTML budget: ${path}`);
        assert.doesNotMatch(html, /<(?:script|link)\b[^>]*(?:src|href)="https?:|modulepreload|prefetch/i, `External runtime or prefetch: ${path}`);
        const scripts = [...html.matchAll(/<script\b[^>]*>[\s\S]*?<\/script>/g)].map((match) => match[0]);
        assert.deepEqual(scripts, [`<script src="${prefix}patch-theme.js"></script>`, `<script src="${prefix}patch-browser.js" defer></script>`], `Unexpected executable script: ${path}`);
        for (const match of html.matchAll(/\b(?:href|src)="([^"]+)"/g)) {
            const href = match[1];
            if (href.startsWith("#")) {
                assert.ok(html.includes(`id="${href.slice(1)}"`), `Missing anchor: ${path} ${href}`);
                continue;
            }
            if (/^https:\/\/github\.com\/LuganoPlanB\/bitcoin-roots\/releases(?:\/(?:tag|download)\/[A-Za-z0-9_.-]+(?:\/[A-Za-z0-9_.-]+)?)?$/.test(href)) continue;
            assert.ok(href.startsWith(base) && !href.startsWith("//") && !href.includes(".."), `Invalid local destination: ${path} ${href}`);
            if (href.startsWith(prefix)) {
                const destination = href.slice(prefix.length);
                if (destination.endsWith("/")) assert.ok(available.has(`${destination}index.html`), `Broken patch link: ${path} ${href}`);
                else if (!destination) assert.ok(available.has("index.html"));
                else assert.ok((await stat(join(outputDir, destination))).isFile(), `Missing patch resource: ${href}`);
                localLinks++;
            } else if (siteDir) {
                const destination = href.slice(base.length);
                const sitePath = join(siteDir, destination || "index.html");
                const candidates = [sitePath, `${sitePath}.html`, join(sitePath, "index.html")];
                let exists = false;
                for (const candidate of candidates) {
                    try { if ((await stat(candidate)).isFile()) { exists = true; break; } }
                    catch (error) { if (error.code !== "ENOENT" && error.code !== "ENOTDIR") throw error; }
                }
                assert.ok(exists, `Broken website link/resource: ${path} ${href}`);
            }
        }
    }
    return { releases: catalogue.releases.length, pages: actual.length, fileChanges, localLinks, base };
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
    const siteDir = fileURLToPath(new URL("../.vitepress/dist/", import.meta.url));
    try {
        const result = await checkPatchHtml({ outputDir: join(siteDir, "patches"), siteDir });
        console.log(`Verified ${result.releases} releases, ${result.pages} static pages, ${result.fileChanges} file changes and ${result.localLinks} local patch links/resources at ${result.base}.`);
    } catch (error) {
        console.error(`Static patch acceptance failed: ${error.message}`);
        process.exitCode = 1;
    }
}
