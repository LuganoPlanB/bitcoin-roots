import assert from "node:assert/strict";
import test from "node:test";
import { createHash } from "node:crypto";
import { mkdtemp, readFile, readdir, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { assetCacheKey, assetLimits, downloadAsset, matchingChecksum, verifyCatalogueAssets } from "../../scripts/release-assets.mjs";

const tag = "v29.4-roots.1";
const name = "bitcoin-roots-29.4-roots.1.patch";
const bytes = Buffer.from("From 0123\r\nSubject: café 🌳\r\n\r\n+patch\r\nNo final newline", "utf8");
const digest = (data, algorithm = "sha512") => createHash(algorithm).update(data).digest("hex");
const metadata = (name, body, extra = {}) => ({ id: name === "SHA512SUMS" ? 2 : 1, name, size: body.length,
    updatedAt: "2026-10-01T00:00:00Z", url: `https://github.com/LuganoPlanB/bitcoin-roots/releases/download/${tag}/${name}`,
    digest: `sha256:${digest(body, "sha256")}`, ...extra });
const catalogue = (patch = metadata(name, bytes), checksum = null) => ({ schemaVersion: 1, releases: [{ tag,
    patchState: patch ? "available" : "unavailable", patch, checksum, signature: { id: 3 } }] });
const sums = (body = bytes) => Buffer.from(`${digest(body)}  ${name}\r\n`);
async function cache(t) {
    const path = await mkdtemp(join(tmpdir(), "roots-assets-test-"));
    t.after(() => rm(path, { recursive: true, force: true }));
    return path;
}
const fetchBytes = (patch = bytes, checksum = sums()) => async (url) => new Response(url.endsWith("SHA512SUMS") ? checksum : patch);

test("original CRLF, Unicode and missing final newline survive verified cold and warm caches", async (t) => {
    const cacheDir = await cache(t);
    const input = catalogue(metadata(name, bytes), metadata("SHA512SUMS", sums()));
    const result = await verifyCatalogueAssets(input, { cacheDir, fetchImpl: fetchBytes() });
    const integrity = result.releases[0].integrity;
    assert.deepEqual(await readFile(join(cacheDir, integrity.cacheFile)), bytes);
    assert.equal(integrity.sha512, digest(bytes));
    assert.equal(integrity.checksum, "verified");
    assert.equal(integrity.platformDigest, "verified");
    assert.equal(integrity.signature, "not-verified");
    const warm = await verifyCatalogueAssets(input, { cacheDir, fetchImpl: () => { throw new Error("must not fetch"); } });
    assert.deepEqual(warm, result);
});

test("legacy missing checksum and digest remain honest; absent patch is never fetched", async (t) => {
    const cacheDir = await cache(t);
    const result = await verifyCatalogueAssets(catalogue(metadata(name, bytes, { digest: null })), { cacheDir, fetchImpl: fetchBytes() });
    assert.equal(result.releases[0].integrity.checksum, "missing");
    assert.equal(result.releases[0].integrity.platformDigest, "missing");
    const absent = await verifyCatalogueAssets(catalogue(null), { cacheDir, fetchImpl: () => assert.fail("fetch missing patch") });
    assert.equal(absent.releases[0].patchState, "unavailable");
    assert.equal(absent.releases[0].integrity, null);
});

test("SHA512 mismatch stops publication and never caches the invalid patch", async (t) => {
    const cacheDir = await cache(t);
    const wrong = sums(Buffer.from("other bytes"));
    await assert.rejects(verifyCatalogueAssets(catalogue(metadata(name, bytes), metadata("SHA512SUMS", wrong)),
        { cacheDir, fetchImpl: fetchBytes(bytes, wrong) }), /SHA512 asset digest mismatch/);
    assert.equal((await readdir(cacheDir)).filter((file) => file.startsWith(assetCacheKey(metadata(name, bytes)))).length, 0);
});

test("platform digest mismatch on patch or checksum metadata is a hard failure", async (t) => {
    const cacheDir = await cache(t);
    const invalid = `sha256:${"0".repeat(64)}`;
    for (const input of [catalogue(metadata(name, bytes, { digest: invalid })),
        catalogue(metadata(name, bytes), metadata("SHA512SUMS", sums(), { digest: invalid }))]) {
        await assert.rejects(verifyCatalogueAssets(input, { cacheDir, fetchImpl: fetchBytes() }), /Platform asset digest mismatch/);
    }
});

test("checksum entry must match exact filename once, including when metadata is present", async (t) => {
    const cacheDir = await cache(t);
    for (const body of [Buffer.from(`${digest(bytes)}  other.patch\n`), Buffer.concat([sums(), sums()]), Buffer.from("bad checksum\n")]) {
        await assert.rejects(verifyCatalogueAssets(catalogue(metadata(name, bytes), metadata("SHA512SUMS", body)),
            { cacheDir, fetchImpl: fetchBytes(bytes, body) }), /matching patch filename|Malformed SHA512SUMS/);
    }
    assert.equal(matchingChecksum(Buffer.from(`${digest(bytes).toUpperCase()} *${name}`), name), digest(bytes));
    assert.equal(matchingChecksum(Buffer.from(`# Bitcoin Roots release checksums\r\n# -----BEGIN PGP PUBLIC KEY BLOCK-----\r\n# informational key\r\n${digest(bytes)}  ${name}\r\n`), name), digest(bytes));
    assert.throws(() => matchingChecksum(Buffer.from(`# ${digest(bytes)}  ${name}\n`), name), /exactly one matching/);
    assert.throws(() => matchingChecksum(Buffer.from([0xff]), name), /encoding/);
});

test("truncated, oversized, Content-Length mismatch and partial responses fail without retries", async () => {
    for (const response of [() => new Response(bytes.subarray(1)), () => new Response(Buffer.concat([bytes, bytes])),
        () => new Response(bytes, { headers: { "Content-Length": String(bytes.length + 1) } }),
        () => new Response(bytes, { status: 206 })]) {
        let calls = 0;
        await assert.rejects(downloadAsset(metadata(name, bytes), { fetchImpl: async () => { calls++; return response(); } }),
            /Truncated|expected byte size|content length|Invalid asset response/);
        assert.equal(calls, 1);
    }
    await assert.rejects(downloadAsset(metadata(name, bytes), { maxBytes: bytes.length - 1 }), /byte limit/);
    await assert.rejects(downloadAsset(metadata(name, bytes, { size: assetLimits.patchBytes + 1 })), /byte limit/);
});

test("cache corruption, pointer tampering and missing objects fail before reuse", async (t) => {
    for (const corruption of ["body", "size", "pointer", "missing", "traversal"]) {
        const cacheDir = await cache(t);
        const input = catalogue();
        const result = await verifyCatalogueAssets(input, { cacheDir, fetchImpl: fetchBytes() });
        const path = join(cacheDir, result.releases[0].integrity.cacheFile);
        const pointer = join(cacheDir, `${assetCacheKey(input.releases[0].patch)}.json`);
        if (corruption === "body") await writeFile(path, Buffer.alloc(bytes.length));
        if (corruption === "size") await writeFile(path, bytes.subarray(1));
        if (corruption === "pointer") await writeFile(pointer, "bad json");
        if (corruption === "traversal") await writeFile(pointer, JSON.stringify({ identity: assetCacheKey(input.releases[0].patch), sha512: "../escape" }));
        if (corruption === "missing") await rm(path);
        await assert.rejects(verifyCatalogueAssets(input, { cacheDir, fetchImpl: () => assert.fail("corrupt cache must fail closed") }),
            /digest mismatch|size mismatch|cache pointer|cache identity|Missing cached/);
    }
});

test("every identity field invalidates cached bytes", async (t) => {
    const cacheDir = await cache(t);
    const input = catalogue(metadata(name, bytes, { digest: null }));
    await verifyCatalogueAssets(input, { cacheDir, fetchImpl: fetchBytes() });
    for (const extra of [{ id: 10 }, { updatedAt: "2026-10-02T00:00:00Z" }, { digest: `sha256:${digest(bytes, "sha256")}` }]) {
        let calls = 0;
        await verifyCatalogueAssets(catalogue({ ...input.releases[0].patch, ...extra }), { cacheDir, fetchImpl: async () => { calls++; return new Response(bytes); } });
        assert.equal(calls, 1);
    }
    const longer = Buffer.concat([bytes, Buffer.from("\n")]);
    let calls = 0;
    await verifyCatalogueAssets(catalogue(metadata(name, longer, { digest: null })), { cacheDir, fetchImpl: async () => { calls++; return new Response(longer); } });
    assert.equal(calls, 1);
});

test("replaced checksum metadata cannot reuse a previously verified patch claim", async (t) => {
    const cacheDir = await cache(t);
    await verifyCatalogueAssets(catalogue(metadata(name, bytes), metadata("SHA512SUMS", sums())), { cacheDir, fetchImpl: fetchBytes() });
    const replacement = sums(Buffer.from("replaced patch"));
    await assert.rejects(verifyCatalogueAssets(catalogue(metadata(name, bytes), metadata("SHA512SUMS", replacement, { id: 9 })),
        { cacheDir, fetchImpl: fetchBytes(bytes, replacement) }), /SHA512 asset digest mismatch/);
});

test("HTTPS redirects are manual and confined to the known GitHub release asset host", async () => {
    const destination = "https://release-assets.githubusercontent.com/github-production-release-asset/123/abcd-1234?token=secret";
    const calls = [];
    const body = await downloadAsset(metadata(name, bytes), { fetchImpl: async (url, options) => {
        calls.push(url); assert.equal(options.redirect, "manual");
        return calls.length === 1 ? new Response(null, { status: 302, headers: { location: destination } }) : new Response(bytes);
    } });
    assert.deepEqual(body, bytes);
    assert.deepEqual(calls, [metadata(name, bytes).url, destination]);
    for (const location of ["http://release-assets.githubusercontent.com/github-production-release-asset/123/abcd", "https://evil.example/secret",
        "https://release-assets.githubusercontent.com.evil.example/github-production-release-asset/123/abcd", "https://user:secret@release-assets.githubusercontent.com/github-production-release-asset/123/abcd",
        "https://release-assets.githubusercontent.com/other", "https://release-assets.githubusercontent.com:444/github-production-release-asset/123/abcd", "https://api.github.com/repos/evil", "//evil.example/"]) {
        let calls = 0;
        await assert.rejects(downloadAsset(metadata(name, bytes), { fetchImpl: async () => { calls++; return new Response(null, { status: 302, headers: { location } }); } }), /redirect destination/);
        assert.equal(calls, 1);
    }
    await assert.rejects(downloadAsset(metadata(name, bytes), { fetchImpl: async () => new Response(null, { status: 302, headers: { location: destination } }) }), /redirect limit/);
    await assert.rejects(downloadAsset(metadata(name, bytes), { fetchImpl: async () => new Response(null, { status: 302 }) }), /Missing asset redirect/);
});

test("transient responses and network errors retry with a strict bound and sanitized diagnostics", async () => {
    for (const status of [408, 429, 500, 503]) {
        let calls = 0;
        const delays = [];
        assert.deepEqual(await downloadAsset(metadata(name, bytes), { delay: async (ms) => delays.push(ms), fetchImpl: async () => {
            calls++; return calls < 3 ? new Response("private body", { status }) : new Response(bytes);
        } }), bytes);
        assert.equal(calls, 3); assert.deepEqual(delays, [250, 500]);
    }
    let calls = 0;
    await assert.rejects(downloadAsset(metadata(name, bytes), { delay: async () => {}, fetchImpl: async () => { calls++; throw new Error("TOKEN=secret private patch text"); } }),
        { message: "Asset network request failed" });
    assert.equal(calls, 3);
    calls = 0;
    await assert.rejects(downloadAsset(metadata(name, bytes), { fetchImpl: async () => { calls++; return new Response("secret", { status: 404 }); } }), /HTTP 404/);
    assert.equal(calls, 1);
});

test("fetch and byte-stream stalls abort and exhaust bounded retries", async () => {
    let calls = 0;
    const signals = [];
    await assert.rejects(downloadAsset(metadata(name, bytes), { timeoutMs: 5, delay: async () => {}, fetchImpl: (_url, { signal }) => {
        calls++; signals.push(signal); return new Promise(() => {});
    } }), /timed out/);
    assert.equal(calls, 3); assert.ok(signals.every((signal) => signal.aborted));
    await assert.rejects(downloadAsset(metadata(name, bytes), { attempts: 1, timeoutMs: 5, fetchImpl: async () => new Response(new ReadableStream({ start() {} })) }), /timed out/);
    let attempts = 0;
    assert.deepEqual(await downloadAsset(metadata(name, bytes), { delay: async () => {}, fetchImpl: async () => {
        attempts++;
        return attempts === 1 ? new Response(new ReadableStream({ start(controller) { controller.error(new Error("secret")); } })) : new Response(bytes);
    } }), bytes);
    assert.equal(attempts, 2);
});

test("invalid destinations and option limits fail before network access", async () => {
    for (const url of ["https://evil.example/", metadata(name, bytes).url.replace(tag, ".."), metadata(name, bytes).url.replace(name, "SHA512SUMS.asc")]) {
        await assert.rejects(downloadAsset(metadata(name, bytes, { url }), { fetchImpl: () => assert.fail("unsafe URL") }));
    }
    for (const options of [{ attempts: 4 }, { attempts: 0 }, { timeoutMs: 0 }, { timeoutMs: 120001 }, { maxRedirects: 5 }, { maxBytes: 0 }]) {
        await assert.rejects(downloadAsset(metadata(name, bytes), { ...options, fetchImpl: () => assert.fail("invalid options") }), /Invalid/);
    }
});

test("network failure never changes an available release into a missing-patch state", async (t) => {
    const cacheDir = await cache(t);
    const input = catalogue();
    await assert.rejects(verifyCatalogueAssets(input, { cacheDir, attempts: 1, fetchImpl: async () => new Response(null, { status: 500 }) }), /HTTP 500/);
    assert.equal(input.releases[0].patchState, "available");
});
