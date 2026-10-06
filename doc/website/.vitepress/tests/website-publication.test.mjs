import assert from "node:assert/strict";
import test from "node:test";
import { createHash } from "node:crypto";
import { mkdtemp, mkdir, readFile, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { fileURLToPath } from "node:url";
import { checkPublication, fetchDeployedReceipt, needsPublication, preparePublication,
    publicationReceipt, publicationSnapshot, writePublicationReceipt } from "../../scripts/website-publication.mjs";

const date = "2026-10-01T12:00:00Z";
const tag = "v29.4-roots.1";
const bytes = Buffer.from("tiny synthetic patch\r\n");
const hash = (body, algorithm = "sha512") => createHash(algorithm).update(body).digest("hex");
const asset = (name = "bitcoin-roots-29.4-roots.1.patch", extra = {}) => ({ id: 10, name, size: bytes.length,
    updated_at: date, digest: `sha256:${hash(bytes, "sha256")}`,
    browser_download_url: `https://github.com/LuganoPlanB/bitcoin-roots/releases/download/${tag}/${name}`, ...extra });
const release = (extra = {}) => ({ id: 1, tag_name: tag, draft: false, prerelease: false,
    published_at: date, updated_at: date, name: "Roots", body: "Notes", target_commitish: "roots/29.4",
    html_url: `https://github.com/LuganoPlanB/bitcoin-roots/releases/tag/${tag}`, assets: [asset()], ...extra });
const revisions = { siteRevision: "a".repeat(40), themeRevision: "b".repeat(40), base: "/bitcoin-roots/" };
const api = (entries) => async (page) => page === 1 ? entries : [];
const snapshot = (entries = [release()], extra = {}) => publicationSnapshot({ ...revisions, api: api(entries), ...extra });
async function directories(t) {
    const path = await mkdtemp(join(tmpdir(), "roots-publication-test-"));
    t.after(() => rm(path, { recursive: true, force: true }));
    const outputDir = join(path, "dist");
    await mkdir(outputDir);
    return { inputDir: join(path, "inputs"), outputDir };
}
async function renderedCatalogue(verified, outputDir, base = revisions.base) {
    await mkdir(join(outputDir, "patches"), { recursive: true });
    await writeFile(join(outputDir, "patches/catalogue.json"), JSON.stringify({ base,
        releases: verified.releases.map((release) => ({ tag: release.tag, state: release.patchState,
            sha512: release.integrity?.sha512 ?? null })) }));
}

test("same complete inventory and revisions skip publication regardless of API ordering or cache presence", async () => {
    const entries = [release(), release({ id: 2, tag_name: "v29.3-roots.1", assets: [],
        html_url: "https://github.com/LuganoPlanB/bitcoin-roots/releases/tag/v29.3-roots.1" })];
    const original = await snapshot(entries);
    const reordered = await snapshot(entries.toReversed());
    assert.equal(original.fingerprint, reordered.fingerprint);
    assert.equal(needsPublication(reordered, publicationReceipt(original)), false);
    assert.equal(needsPublication(reordered, null), true);
    assert.equal(needsPublication(reordered, {}), true);
    assert.equal(needsPublication(reordered, { ...publicationReceipt(original), schemaVersion: 0 }), true);
    // Public receipt, not local cache/input contents, decides whether to build.
    assert.equal(needsPublication(reordered, { fingerprint: original.fingerprint }), true);
});

test("added, deleted, edited, unavailable and replaced inputs invalidate the deployed receipt", async () => {
    const original = await snapshot();
    const receipt = publicationReceipt(original);
    const other = release({ id: 2, tag_name: "v30.3-roots.1", assets: [],
        html_url: "https://github.com/LuganoPlanB/bitcoin-roots/releases/tag/v30.3-roots.1" });
    const changes = [[], [release(), other], [release({ assets: [] })],
        [release({ body: "Edited notes" })], [release({ name: "Edited name" })],
        [release({ updated_at: "2026-10-02T12:00:00Z" })], [release({ target_commitish: "different" })],
        [release({ prerelease: true })], [release({ published_at: "2026-10-02T12:00:00Z" })],
        [release({ id: 3 })]];
    for (const property of [{ id: 20 }, { size: 99 }, { updated_at: "2026-10-02T12:00:00Z" },
        { digest: `sha256:${"f".repeat(64)}` }, { digest: null }]) {
        changes.push([release({ assets: [asset(undefined, property)] })]);
    }
    for (const name of ["SHA512SUMS", "SHA512SUMS.asc"]) {
        const withChecksum = await snapshot([release({ assets: [asset(), asset(name, { id: 11 })] })]);
        assert.equal(needsPublication(withChecksum, receipt), true);
        for (const property of [{ id: 12 }, { size: 100 }, { updated_at: "2026-10-02T12:00:00Z" },
            { digest: `sha256:${"c".repeat(64)}` }]) {
            const edited = await snapshot([release({ assets: [asset(), asset(name, { id: 11, ...property })] })]);
            assert.equal(needsPublication(edited, publicationReceipt(withChecksum)), true);
        }
        assert.equal(needsPublication(original, publicationReceipt(withChecksum)), true);
    }
    for (const entries of changes) assert.equal(needsPublication(await snapshot(entries), receipt), true);
    for (const change of [{ siteRevision: "c".repeat(40) }, { themeRevision: "d".repeat(40) }, { base: "/" }]) {
        assert.equal(needsPublication(await snapshot(undefined, change), receipt), true);
    }
    assert.equal(needsPublication(await snapshot([release(), { draft: true }]), receipt), false);
    // Missing -> available is a change too; absence is never an API failure.
    assert.equal(needsPublication(original, publicationReceipt(await snapshot([release({ assets: [] })]))), true);
});

test("check is dependency-free, refreshes every page, and reads public receipt without credentials", async (t) => {
    const { inputDir } = await directories(t);
    const expected = await snapshot();
    const calls = [];
    const result = await checkPublication({ ...revisions, inputDir, siteUrl: "https://example.com/bitcoin-roots",
        api: async (page) => { calls.push(page); return page === 1 ? [release()] : []; },
        fetchImpl: async (url, options) => {
            assert.match(url, /^https:\/\/example.com\/bitcoin-roots\/deployment.json\?refresh=\d+$/);
            assert.equal(options.redirect, "error");
            assert.equal(options.cache, "no-store");
            assert.equal(options.headers.Authorization, undefined);
            return new Response(JSON.stringify(publicationReceipt(expected)));
        } });
    assert.deepEqual(calls, [1, 2]);
    assert.equal(result.changed, false);
    assert.deepEqual(JSON.parse(await readFile(join(inputDir, "publication.json"))), expected);
});

test("missing or malformed receipts force publication; transient HTTP, timeout and size errors stop it", async () => {
    const url = "https://example.com/bitcoin-roots/";
    assert.equal(await fetchDeployedReceipt(url, { fetchImpl: async () => new Response(null, { status: 404 }) }), null);
    assert.equal(await fetchDeployedReceipt(url, { fetchImpl: async () => new Response("<html>old site</html>") }), null);
    for (const status of [403, 429, 500]) await assert.rejects(fetchDeployedReceipt(url,
        { fetchImpl: async () => new Response("hidden body", { status }) }), new RegExp(`HTTP ${status}`));
    await assert.rejects(fetchDeployedReceipt(url, { maxBytes: 2, fetchImpl: async () => new Response("large") }), /byte limit/);
    await assert.rejects(fetchDeployedReceipt(url, { timeoutMs: 5,
        fetchImpl: (_url, { signal }) => new Promise((_resolve, reject) => signal.addEventListener("abort", () => reject(new Error("aborted")))) }), /aborted/);
    for (const value of ["http://example.com", "https://user@example.com/", "https://example.com/?token=secret"]) {
        await assert.rejects(fetchDeployedReceipt(value), /Pages URL/);
    }
});

test("checked snapshot prepares cold/warm inputs without rediscovery; receipt requires exact verified inventory", async (t) => {
    const directories_ = await directories(t);
    const original = await snapshot();
    let downloads = 0;
    const options = { ...directories_, fetchImpl: async () => { downloads++; return new Response(bytes); } };
    const cold = await preparePublication(original, options);
    assert.equal(downloads, 1);
    assert.equal(cold.releases[0].integrity.platformDigest, "verified");
    await preparePublication(original, { ...directories_, fetchImpl: async () => { throw new Error("warm network unnecessary"); } });
    await assert.rejects(writePublicationReceipt(original, directories_), /ENOENT/);
    await renderedCatalogue(cold, directories_.outputDir, "/");
    await assert.rejects(writePublicationReceipt(original, directories_), /Rendered catalogue differs/);
    await renderedCatalogue(cold, directories_.outputDir);
    await writePublicationReceipt(original, directories_);
    assert.deepEqual(JSON.parse(await readFile(join(directories_.outputDir, "deployment.json"))), publicationReceipt(original));
    await assert.rejects(writePublicationReceipt(await snapshot([release({ assets: [] })]), directories_), /differs/);
    await assert.rejects(preparePublication({ ...original, fingerprint: "f".repeat(64) }, options), /snapshot/);
});

test("discovery, integrity and receipt errors preserve the previous catalogue and deployed evidence", async (t) => {
    const directories_ = await directories(t);
    const original = await snapshot();
    const verified = await preparePublication(original, { ...directories_, fetchImpl: async () => new Response(bytes) });
    await renderedCatalogue(verified, directories_.outputDir);
    await writePublicationReceipt(original, directories_);
    const cataloguePath = join(directories_.inputDir, "catalogue.json");
    const receiptPath = join(directories_.outputDir, "deployment.json");
    const before = await readFile(cataloguePath);
    const deployed = await readFile(receiptPath);
    await assert.rejects(checkPublication({ ...revisions, ...directories_, siteUrl: "https://example.com/",
        api: async () => { throw new Error("API failure"); } }), /API failure/);
    const replaced = await snapshot([release({ assets: [asset(undefined, { id: 99 })] })]);
    await assert.rejects(preparePublication(replaced, { ...directories_, fetchImpl: async () => new Response(Buffer.alloc(bytes.length)) }), /digest/i);
    assert.deepEqual(await readFile(cataloguePath), before);
    assert.deepEqual(await readFile(receiptPath), deployed);
    await writeFile(cataloguePath, "{}");
    await assert.rejects(writePublicationReceipt(original, directories_));
    assert.deepEqual(await readFile(receiptPath), deployed);
});

test("workflow serializes current-main discovery through deploy, gates expensive work, and isolates Pages authority", async () => {
    const workflow = await readFile(fileURLToPath(new URL("../../../../.github/workflows/deploy-docs.yml", import.meta.url)), "utf8");
    assert.match(workflow, /schedule:\s*\n\s*- cron: '17 \*\/6 \* \* \*'/);
    assert.match(workflow, /workflow_dispatch:/);
    assert.match(workflow, /push:\s*\n\s*branches:\s*\n\s*- main/);
    assert.doesNotMatch(workflow, /\n\s*(release|pull_request|pull_request_target):/);
    assert.match(workflow, /concurrency:\s*\n\s*group: pages\s*\n\s*cancel-in-progress: false/);
    assert.match(workflow, /ref: main/);
    assert.match(workflow, /SITE_REVISION: \$\{\{ steps\.roots\.outputs\.commit \}\}/);
    const [build, deploy] = workflow.split(/\n  deploy:/);
    assert.doesNotMatch(build, /pages: write|id-token: write/);
    assert.match(deploy, /pages: write/);
    assert.match(deploy, /id-token: write/);
    for (const section of [build, deploy]) {
        assert.match(section, /github\.repository == 'LuganoPlanB\/bitcoin-roots'/);
        assert.match(section, /github\.ref == 'refs\/heads\/main'/);
    }
    for (const name of ["Check out the Plan B", "Set up Node.js\n", "Install website", "Test website",
        "Restore verified", "Fetch and verify", "Build website", "Add validated", "Upload GitHub"]) {
        const step = build.split(/\n      - name: /).find((step) => step.startsWith(name));
        assert.ok(step, name);
        assert.match(step, /if: steps\.inventory\.outputs\.changed == 'true'/, name);
    }
    assert.match(deploy, /needs: build/);
    assert.match(deploy, /if: needs\.build\.outputs\.changed == 'true'/);
    assert.ok(build.indexOf("website-publication.mjs prepare") < build.indexOf("run: npm run build"));
    assert.ok(build.indexOf("run: npm run build") < build.indexOf("website-publication.mjs receipt"));
    assert.ok(build.indexOf("website-publication.mjs receipt") < build.indexOf("actions/upload-pages-artifact"));
    assert.doesNotMatch(workflow, /secrets\.|--clobber|gh release|release: write|contents: write/);
});
