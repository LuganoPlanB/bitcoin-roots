import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { mkdtemp, readFile, rm } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
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
