// Optional real-browser acceptance. Reuse an existing Playwright installation;
// this script never installs dependencies, contacts GitHub, or changes releases.
import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { createServer } from "node:http";
import { createRequire } from "node:module";
import { mkdir, mkdtemp, readFile, readdir, rm, stat, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { extname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { renderPatchSite } from "./render-patches.mjs";

const require = createRequire(import.meta.url);
const { chromium } = require(process.env.PLAYWRIGHT_MODULE || "playwright");
const siteDir = fileURLToPath(new URL("../.vitepress/dist/", import.meta.url));
const inputDir = fileURLToPath(new URL("../.vitepress/release-inputs/", import.meta.url));
const catalogue = JSON.parse(await readFile(join(siteDir, "patches/catalogue.json"), "utf8"));
const inputs = JSON.parse(await readFile(join(inputDir, "catalogue.json"), "utf8"));
const evidence = process.env.PATCH_BROWSER_EVIDENCE || await mkdtemp(join(tmpdir(), "roots-browser-evidence-"));
await mkdir(evidence, { recursive: true });
const fixtureRoot = await mkdtemp(join(tmpdir(), "roots-browser-fixture-"));
const base = catalogue.base;
const local = (path) => `${base}${path}`;
const siteStyles = (await readdir(join(siteDir, "assets"))).filter((name) => /^style\.[A-Za-z0-9_-]+\.css$/.test(name)).sort().map((name) => `assets/${name}`);

// Small synthetic inventory exercises future prerelease and empty states.
const tag = "v30.3rc1-roots.1";
const bytes = Buffer.from(`From ${"a".repeat(40)} Mon Sep 17 00:00:00 2001\nFrom: Fixture author\nDate: 2026-10-01\nSubject: [PATCH] Browser fixture\n\n---\ndiff --git a/source.txt b/source.txt\n--- /dev/null\n+++ b/source.txt\n@@ -0,0 +1 @@\n+fixture source\ndiff --git a/binary.bin b/binary.bin\nGIT binary patch\nliteral 0\nHcmV?d00001\ndiff --git a/empty.txt b/empty.txt\nnew file mode 100644\nindex 0000000..e69de29\ndiff --cc unsupported\n@@@ -1 -1 +1 @@@\n++unknown\ndiff --git a/long.txt b/long.txt\n--- /dev/null\n+++ b/long.txt\n@@ -0,0 +1 @@\n+${"x".repeat(4097)}\n-- \n2.43.0\n`);
const sha512 = createHash("sha512").update(bytes).digest("hex");
const cacheFile = `${"b".repeat(64)}-${sha512}.bin`;
const fixtureInputs = join(fixtureRoot, "inputs");
await mkdir(join(fixtureInputs, "assets"), { recursive: true });
await writeFile(join(fixtureInputs, "assets", cacheFile), bytes);
const fixtureRelease = { tag, publishedAt: "2026-10-01T00:00:00Z", prerelease: true, patchState: "available",
    url: `https://github.com/LuganoPlanB/bitcoin-roots/releases/tag/${tag}`,
    patch: { name: `bitcoin-roots-${tag.slice(1)}.patch`, size: bytes.length,
        url: `https://github.com/LuganoPlanB/bitcoin-roots/releases/download/${tag}/bitcoin-roots-${tag.slice(1)}.patch` },
    integrity: { sha512, cacheFile, checksum: "missing", platformDigest: "missing", signature: "not-verified" } };
for (const [name, releases] of [["prerelease", [fixtureRelease]], ["empty", []]]) {
    await writeFile(join(fixtureInputs, "catalogue.json"), JSON.stringify({ schemaVersion: 1, repository: "LuganoPlanB/bitcoin-roots", releases }));
    await renderPatchSite({ inputDir: fixtureInputs, outputDir: join(fixtureRoot, name, "patches"), base: `/_fixture-${name}/`, siteStyles });
}

const server = createServer(async (request, response) => {
    try {
        const pathname = decodeURIComponent(new URL(request.url, "http://localhost").pathname);
        const fixture = ["prerelease", "empty"].find((name) => pathname.startsWith(`/_fixture-${name}/`));
        const prefix = fixture ? `/_fixture-${fixture}/` : base;
        const sharedAsset = fixture && pathname.slice(prefix.length).startsWith("assets/");
        const root = resolve(fixture && !sharedAsset ? join(fixtureRoot, fixture) : siteDir);
        if (!pathname.startsWith(prefix)) return response.writeHead(404).end();
        let path = resolve(root, pathname.slice(prefix.length));
        if (path !== root && !path.startsWith(`${root}/`)) return response.writeHead(404).end();
        try { if ((await stat(path)).isDirectory()) path = join(path, "index.html"); }
        catch { path += ".html"; }
        const body = await readFile(path);
        response.writeHead(200, { "Content-Type": { ".html": "text/html", ".css": "text/css", ".js": "text/javascript", ".json": "application/json", ".svg": "image/svg+xml" }[extname(path)] ?? "application/octet-stream" }).end(body);
    } catch { response.writeHead(404).end(); }
});
await new Promise((done) => server.listen(0, "127.0.0.1", done));
const origin = `http://127.0.0.1:${server.address().port}`;
let browser;
const results = [];
try {
    browser = await chromium.launch({ headless: true, ...(process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE ? { executablePath: process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE } : {}) });
    const newest = catalogue.releases.find((release) => release.state === "available");
    assert.ok(newest, "Smoke test needs at least one prepared available patch");
    const published = inputs.releases.find((release) => release.tag === newest.tag);
    const original = await readFile(join(inputDir, "assets", published.integrity.cacheFile));
    assert.equal(createHash("sha512").update(original).digest("hex"), published.integrity.sha512);
    for (const viewport of [{ width: 1440, height: 900 }, { width: 390, height: 844 }]) {
        const context = await browser.newContext({ viewport, acceptDownloads: true });
        const page = await context.newPage();
        const failures = [];
        const requests = [];
        const external = [];
        page.on("pageerror", (error) => failures.push(error.message));
        page.on("response", (response) => { if (response.status() >= 400) failures.push(response.url()); });
        page.on("request", (request) => requests.push(request.url()));
        await page.route("**/*", (route) => {
            const url = route.request().url();
            if (url.startsWith(origin)) return route.continue();
            external.push(url);
            return route.abort();
        });
        const shellResponse = await page.goto(`${origin}${local("getting-started")}`, { waitUntil: "networkidle" });
        assert.equal(shellResponse.status(), 200, "Website shell is unavailable");
        if (viewport.width < 960) await page.locator(".VPNavBarHamburger").click();
        const nav = viewport.width < 960 ? page.locator(".VPNavScreen") : page.locator(".VPNavBar");
        await nav.getByRole("link", { name: "Patches", exact: true }).click();
        await page.waitForLoadState("networkidle");
        assert.equal(await page.locator("body.roots-patches").count(), 1);
        assert.equal(await page.locator("#patch-latest").innerText(), newest.tag);
        assert.equal(await page.locator(".patch-release-list > li").count(), catalogue.releases.length);
        assert.ok(!requests.some((url) => /catalogue\.json|\.patch(?:$|\?)|commit-\d|file-\d/.test(url)), "Initial shell/index loaded historical payload");
        assert.deepEqual(external, []);
        await page.getByRole("searchbox", { name: "Search releases" }).fill("no-such-release");
        assert.equal(await page.locator(".patch-release-list > li:visible").count(), 0);
        assert.equal(await page.locator(".patch-no-match").isVisible(), true);
        await page.getByRole("button", { name: "Reset filters" }).click();
        await page.getByRole("link", { name: "Browse patch series" }).click();
        await page.waitForLoadState("networkidle");
        assert.equal(await page.locator(".patch-commit-list > li").count(), newest.commits.length);
        await page.locator(".patch-commit summary").first().click();
        await page.getByRole("link", { name: "Open commit overview" }).first().click();
        await page.waitForLoadState("networkidle");
        const commit = newest.commits.find((commit) => commit.files.some((file) => file.state === "text"));
        await page.goto(`${origin}${commit.route}`, { waitUntil: "networkidle" });
        const file = commit.files.find((file) => file.state === "text");
        await page.getByRole("link", { name: file.path, exact: true }).click();
        await page.waitForLoadState("networkidle");
        assert.equal(page.url(), `${origin}${file.route}`);
        assert.equal(await page.locator(".d2h-file-wrapper").count(), 1);
        assert.equal(await page.locator(".patch-view-nav [aria-current]").first().innerText(), "Unified");
        await page.getByRole("link", { name: "Side by side", exact: true }).first().click();
        await page.waitForLoadState("networkidle");
        await page.goBack({ waitUntil: "networkidle" });
        assert.equal(page.url(), `${origin}${file.route}`);
        await page.goForward({ waitUntil: "networkidle" });
        assert.ok(page.url().endsWith("/side-by-side/"));
        await page.reload({ waitUntil: "networkidle" });
        assert.equal(await page.locator(".d2h-file-wrapper").count(), 1);
        const overflow = await page.evaluate(() => ({ viewport: innerWidth, document: document.documentElement.scrollWidth }));
        assert.ok(overflow.document <= overflow.viewport);
        const notes = page.getByRole("link", { name: "Release notes", exact: true });
        const download = page.getByRole("link", { name: "Download original patch", exact: true });
        assert.equal(await notes.getAttribute("href"), published.url);
        assert.equal(await download.getAttribute("href"), published.patch.url);
        assert.deepEqual(external, [], "Browsing requires an external runtime request");
        // Only explicit outbound clicks are mocked. Preserve/compare original bytes.
        await page.route(published.patch.url, (route) => route.fulfill({ status: 200, headers: { "Content-Type": "application/octet-stream", "Content-Disposition": `attachment; filename="${published.patch.name}"` }, body: original }));
        const [artifact] = await Promise.all([page.waitForEvent("download"), download.click()]);
        assert.equal(createHash("sha512").update(await readFile(await artifact.path())).digest("hex"), published.integrity.sha512);
        await page.route(published.url, (route) => route.fulfill({ status: 200, contentType: "text/html", body: "<!doctype html><title>Outbound release notes destination</title>" }));
        await notes.click();
        await page.waitForURL(published.url);
        assert.equal(page.url(), published.url);
        for (const missing of catalogue.releases.filter((release) => release.state === "unavailable")) {
            await page.goto(`${origin}${local(`patches/${missing.tag}/`)}`, { waitUntil: "networkidle" });
            assert.equal(await page.getByRole("heading", { name: "Patch unavailable", exact: true }).count(), 1);
            assert.equal(await page.getByRole("link", { name: "Download original patch", exact: true }).count(), 0);
        }
        await page.goto(`${origin}/_fixture-prerelease/patches/`, { waitUntil: "networkidle" });
        assert.equal(await page.locator(".patch-release-list .patch-status").innerText(), "Prerelease");
        await page.getByRole("link", { name: "Browse patch series" }).click();
        await page.waitForLoadState("networkidle");
        assert.equal(await page.locator("h1").innerText(), tag);
        const fixtureIndex = JSON.parse(await readFile(join(fixtureRoot, "prerelease/patches/catalogue.json"), "utf8"));
        for (const fallback of fixtureIndex.releases[0].commits[0].files.filter((file) => file.state !== "text")) {
            await page.goto(`${origin}${fallback.route}`, { waitUntil: "networkidle" });
            assert.equal(await page.locator(".patch-fallback").count(), 1);
            assert.equal(await page.getByRole("link", { name: "Download original patch", exact: true }).count(), 1);
            assert.equal(await page.locator(".d2h-file-wrapper").count(), 0);
        }
        await page.goto(`${origin}/_fixture-empty/patches/`, { waitUntil: "networkidle" });
        assert.equal(await page.locator(".patch-empty").count(), 1);
        assert.equal(await page.locator(".patch-featured").count(), 0);
        assert.equal(await page.locator(".patch-filters").count(), 0);
        await page.screenshot({ path: join(evidence, `empty-${viewport.width}.png`) });
        assert.deepEqual(failures, []);
        results.push({ viewport, completeJourney: true, downloadBytesVerified: true, notesDestinationVerified: true, missingPrereleaseEmpty: true, explicitFallbacks: true, overflow, failures });
        await context.close();
        const noJs = await browser.newContext({ viewport, javaScriptEnabled: false });
        const staticPage = await noJs.newPage();
        await staticPage.route("**/*", (route) => route.request().url().startsWith(origin) ? route.continue() : route.abort());
        await staticPage.goto(`${origin}${local("patches/")}`, { waitUntil: "networkidle" });
        await staticPage.getByRole("link", { name: "Browse patch series" }).click();
        await staticPage.locator(".patch-commit summary").nth(commit.ordinal - 1).click();
        await staticPage.getByRole("link", { name: "Open commit overview" }).first().click();
        await staticPage.waitForLoadState("networkidle");
        await staticPage.getByRole("link", { name: file.path, exact: true }).click();
        await staticPage.waitForLoadState("networkidle");
        assert.equal(staticPage.url(), `${origin}${file.route}`);
        assert.equal(await staticPage.locator(".d2h-file-wrapper").count(), 1);
        await staticPage.getByRole("link", { name: "Side by side", exact: true }).first().click();
        await staticPage.waitForLoadState("networkidle");
        assert.equal(await staticPage.locator(".d2h-file-wrapper").count(), 1);
        results.push({ viewport, javaScriptEnabled: false, releaseCommitFileNavigation: true });
        await noJs.close();
    }
    // Keep the reviewed mobile ordinal fix covered in the repeatable smoke.
    for (const theme of ["light", "dark"]) for (const fontScale of [100, 200]) {
        const context = await browser.newContext({ viewport: { width: 390, height: 844 }, colorScheme: theme === "light" ? "dark" : "light" });
        await context.addInitScript(({ theme, fontScale }) => {
            localStorage.setItem("vitepress-theme-appearance", theme);
            addEventListener("DOMContentLoaded", () => document.documentElement.style.fontSize = `${fontScale === 200 ? 32 : 16}px`);
        }, { theme, fontScale });
        const page = await context.newPage();
        await page.route("**/*", (route) => route.request().url().startsWith(origin) ? route.continue() : route.abort());
        await page.goto(`${origin}${local(`patches/${newest.tag}/`)}`, { waitUntil: "networkidle" });
        const ordinals = await page.locator(".patch-ordinal").evaluateAll((nodes) => nodes.map((node) => {
            const range = document.createRange();
            range.selectNodeContents(node);
            return { text: node.textContent, lines: range.getClientRects().length, shrink: getComputedStyle(node).flexShrink };
        }));
        assert.ok(ordinals.every((ordinal) => ordinal.lines === 1 && ordinal.shrink === "0"), "Wrapped mobile commit ordinal");
        assert.equal(await page.getByLabel("Appearance").inputValue(), theme);
        assert.ok(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth));
        await page.locator(".patch-commit summary").first().scrollIntoViewIfNeeded();
        await page.screenshot({ path: join(evidence, `ordinals-${theme}-${fontScale}.png`) });
        results.push({ mobileOrdinalRegression: true, theme, fontScale, ordinals });
        await context.close();
    }
    await writeFile(join(evidence, "results.json"), `${JSON.stringify(results, null, 2)}\n`);
    console.log(`Browser acceptance passed: desktop/mobile journeys, deep links/history, preserved downloads and notes destinations, missing/prerelease/empty/fallback states, JS-disabled browsing and mobile ordinals in both themes/font sizes. Evidence: ${evidence}`);
} finally {
    await browser?.close();
    await new Promise((done) => server.close(done));
    await rm(fixtureRoot, { recursive: true, force: true });
}
