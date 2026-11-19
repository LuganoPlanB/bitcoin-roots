import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { copyFile, mkdir, mkdtemp, readFile, readdir, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { spawnSync } from "node:child_process";
import test from "node:test";
import { prepareReleases } from "../../scripts/prepare-releases.mjs";

const patch = Buffer.from("From 0123456789012345678901234567890123456789 Mon Sep 17 00:00:00 2001\nSubject: [PATCH] Small offline fixture\n\ndiff --git a/example b/example\nnew file mode 100644\n--- /dev/null\n+++ b/example\n@@ -0,0 +1 @@\n+fixture\n");
const tag = "v30.3-roots.1";
const name = `bitcoin-roots-${tag.slice(1)}.patch`;
const checksum = Buffer.from(`${createHash("sha512").update(patch).digest("hex")}  ${name}\n`);
const date = "2026-10-01T12:00:00Z";
const asset = (name, bytes, id) => ({ id, name, size: bytes.length, updated_at: date,
    digest: `sha256:${createHash("sha256").update(bytes).digest("hex")}`,
    browser_download_url: `https://github.com/LuganoPlanB/bitcoin-roots/releases/download/${tag}/${name}` });
const release = (tag, id, assets = []) => ({ id, tag_name: tag, draft: false, prerelease: false,
    published_at: date, html_url: `https://github.com/LuganoPlanB/bitcoin-roots/releases/tag/${tag}`, assets });
const api = async (page) => page === 1 ? [release(tag, 1, [asset(name, patch, 10), asset("SHA512SUMS", checksum, 11)])]
    : page === 2 ? [release("v29.3-roots.1", 2)] : [];

test("full preparation is deterministic, retains missing history, and reuses verified bytes offline", async (t) => {
    const inputDir = await mkdtemp(join(tmpdir(), "roots-preparation-"));
    t.after(() => rm(inputDir, { recursive: true, force: true }));
    const catalogue = await prepareReleases({ inputDir, api,
        fetchImpl: async (url) => new Response(url.endsWith("SHA512SUMS") ? checksum : patch) });
    assert.equal(catalogue.releases.length, 2);
    assert.equal(catalogue.releases.find((entry) => entry.tag === "v29.3-roots.1").patchState, "unavailable");
    const first = await readFile(join(inputDir, "catalogue.json"));
    const warm = await prepareReleases({ inputDir, api, fetchImpl: () => assert.fail("cached assets must not fetch") });
    assert.deepEqual(warm, catalogue);
    assert.deepEqual(await readFile(join(inputDir, "catalogue.json")), first);
    const available = warm.releases.find((entry) => entry.patch);
    assert.deepEqual(await readFile(join(inputDir, "assets", available.integrity.cacheFile)), patch);
    await assert.rejects(prepareReleases({ inputDir, api: async () => { throw new Error("discovery failed"); } }));
    assert.deepEqual(await readFile(join(inputDir, "catalogue.json")), first);
    const replacementApi = async (page) => page === 1 ? [release(tag, 1, [asset(name, patch, 20)])] : [];
    await assert.rejects(prepareReleases({ inputDir, api: replacementApi,
        fetchImpl: async () => new Response(null, { status: 404 }) }), /HTTP 404/);
    assert.deepEqual(await readFile(join(inputDir, "catalogue.json")), first);
});

test("preparation confines explicit token to discovery and keeps asset/CDN requests and cache public", async (t) => {
    const inputDir = await mkdtemp(join(tmpdir(), "roots-auth-preparation-"));
    t.after(() => rm(inputDir, { recursive: true, force: true }));
    const token = "ghs_prepare_mock_marker";
    const pages = [];
    t.mock.method(globalThis, "fetch", async (url, options) => {
        assert.equal(options.headers.Authorization, `Bearer ${token}`);
        assert.equal(options.redirect, "error");
        const page = Number(new URL(url).searchParams.get("page"));
        pages.push(page);
        return Response.json(page === 1 ? [{ draft: true, id: -1, assets: token, body: token }, ...await api(1)] : await api(page));
    });
    const destinations = [];
    const result = await prepareReleases({ inputDir, token, fetchImpl: async (url, options) => {
        destinations.push(url);
        assert.equal(options.headers.Authorization, undefined);
        assert.equal(options.redirect, "manual");
        assert.ok(!url.includes(token));
        if (url.startsWith("https://github.com/")) {
            const suffix = url.endsWith("SHA512SUMS") ? "aaaa" : "bbbb";
            return new Response(null, { status: 302, headers: { location: `https://release-assets.githubusercontent.com/github-production-release-asset/1/${suffix}` } });
        }
        return new Response(url.endsWith("aaaa") ? checksum : patch);
    } });
    assert.deepEqual(pages, [1, 2, 3]);
    assert.equal(destinations.length, 4);
    assert.equal(result.releases.length, 2);
    assert.doesNotMatch(await readFile(join(inputDir, "catalogue.json"), "utf8"), new RegExp(token));
    for (const filename of await readdir(join(inputDir, "assets"))) {
        assert.ok(!filename.includes(token));
        assert.ok(!(await readFile(join(inputDir, "assets", filename))).includes(Buffer.from(token)));
    }
});

async function cliFixture(t) {
    const root = await mkdtemp(join(tmpdir(), "roots-auth-cli-"));
    t.after(() => rm(root, { recursive: true, force: true }));
    const scripts = join(root, "scripts");
    await mkdir(scripts);
    for (const name of ["prepare-releases.mjs", "release-catalogue.mjs", "release-assets.mjs"]) {
        await copyFile(new URL(`../../scripts/${name}`, import.meta.url), join(scripts, name));
    }
    const mock = join(root, "mock-fetch.mjs");
    await writeFile(mock, `
import assert from "node:assert/strict";
import { appendFileSync } from "node:fs";
const releases = ${JSON.stringify([release(tag, 1, [asset(name, patch, 10), asset("SHA512SUMS", checksum, 11)])])};
const patch = Buffer.from(${JSON.stringify(patch.toString("base64"))}, "base64");
const checksum = Buffer.from(${JSON.stringify(checksum.toString("base64"))}, "base64");
globalThis.fetch = async (url, options) => {
    const target = new URL(url);
    assert.ok(!url.includes(process.env.GITHUB_TOKEN || "absent-token-marker"));
    if (target.hostname === "api.github.com") {
        assert.equal(target.pathname, "/repos/LuganoPlanB/bitcoin-roots/releases");
        assert.equal(options.headers.Authorization, process.env.GITHUB_TOKEN ? "Bearer " + process.env.GITHUB_TOKEN : undefined);
        assert.equal(options.redirect, "error");
        const page = Number(target.searchParams.get("page"));
        appendFileSync(${JSON.stringify(join(root, "pages.log"))}, page + "\\n");
        if (process.env.MOCK_FAILURE === "401") return new Response("private " + process.env.GITHUB_TOKEN, { status: 401 });
        if (process.env.MOCK_FAILURE === "network") throw new Error("private Authorization " + process.env.GITHUB_TOKEN);
        if (process.env.MOCK_FAILURE === "JSON") return new Response("private " + process.env.GITHUB_TOKEN);
        if (process.env.MOCK_FAILURE === "later-page" && page === 2) return new Response("private " + process.env.GITHUB_TOKEN, { status: 403 });
        return Response.json(page === 1 ? [{ draft: true, assets: process.env.GITHUB_TOKEN }, ...releases] : []);
    }
    assert.equal(target.hostname, "github.com");
    assert.equal(options.headers.Authorization, undefined);
    return new Response(url.endsWith("SHA512SUMS") ? checksum : patch);
};
`);
    return { root, mock, catalogue: join(root, ".vitepress/release-inputs/catalogue.json"),
        run: (token = "", failure = "") => spawnSync(process.execPath,
            ["--import", mock, join(scripts, "prepare-releases.mjs")],
            { cwd: root, encoding: "utf8", env: { ...process.env, GITHUB_TOKEN: token, MOCK_FAILURE: failure } }) };
}

test("real preparation CLI wires optional environment token through discovery without serializing it", async (t) => {
    for (const token of ["", "ghs_cli_mock_marker"]) {
        const fixture = await cliFixture(t);
        const result = fixture.run(token);
        assert.equal(result.status, 0, result.stderr);
        assert.match(result.stdout, /Prepared 1 public releases: 1 patches, 0 unavailable/);
        assert.equal(result.stderr, "");
        assert.equal(await readFile(join(fixture.root, "pages.log"), "utf8"), "1\n2\n");
        const catalogue = await readFile(fixture.catalogue, "utf8");
        if (token) assert.ok(!`${result.stdout}${result.stderr}${catalogue}`.includes(token));
    }
});

test("CLI authentication, network, JSON and later-page failures preserve the catalogue without fallback or disclosure", async (t) => {
    const token = "ghs_cli_failure_mock_marker";
    for (const failure of ["401", "network", "JSON", "later-page"]) {
        const fixture = await cliFixture(t);
        const catalogueDir = join(fixture.root, ".vitepress/release-inputs");
        await mkdir(catalogueDir, { recursive: true });
        await writeFile(fixture.catalogue, "previous complete verified catalogue\n");
        const result = fixture.run(token, failure);
        assert.equal(result.status, 1);
        assert.equal(result.stdout, "");
        assert.match(result.stderr, /Release preparation failed:/);
        assert.ok(!result.stderr.includes(token));
        assert.doesNotMatch(result.stderr, /private|Authorization/);
        assert.equal(await readFile(fixture.catalogue, "utf8"), "previous complete verified catalogue\n");
        assert.equal(await readFile(join(fixture.root, "pages.log"), "utf8"), failure === "later-page" ? "1\n2\n" : "1\n");
        assert.deepEqual(await readdir(catalogueDir), ["catalogue.json"]);
    }
});
