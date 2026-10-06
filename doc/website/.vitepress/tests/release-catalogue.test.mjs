import assert from "node:assert/strict";
import test from "node:test";
import { createCatalogue, discoverReleases, expectedPatchName, fetchReleasePage, serializeCatalogue } from "../../scripts/release-catalogue.mjs";

const date = "2026-10-01T12:00:00Z";
const release = (tag = "v29.4-roots.1", id = 1, extra = {}) => ({
  id, tag_name: tag, draft: false, prerelease: false, published_at: date,
  html_url: `https://github.com/LuganoPlanB/bitcoin-roots/releases/tag/${tag}`, assets: [], ...extra,
});
const asset = (tag, name = expectedPatchName(tag), extra = {}) => ({
  id: 100, name, size: 23, updated_at: date, digest: `sha256:${"a".repeat(64)}`,
  browser_download_url: `https://github.com/LuganoPlanB/bitcoin-roots/releases/download/${tag}/${name}`, ...extra,
});

test("pagination includes short pages, prereleases and missing patches; drafts are excluded", async () => {
  const tag = "v30.3rc1-roots.1";
  const pages = [
    [release(tag, 1, { prerelease: true, assets: [asset(tag), asset(tag, "SHA512SUMS", { id: 101 })] }), { draft: true }],
    [release("v29.4-roots.2", 2, { published_at: "2026-10-02T12:00:00Z" })], [],
  ];
  const calls = [];
  const catalogue = await discoverReleases({ api: async (page) => { calls.push(page); return pages[page - 1]; } });
  assert.deepEqual(calls, [1, 2, 3]);
  assert.deepEqual(catalogue.releases.map((entry) => entry.tag), ["v29.4-roots.2", tag]);
  assert.equal(catalogue.releases[0].patchState, "unavailable");
  assert.equal(catalogue.releases[1].prerelease, true);
  assert.equal(catalogue.releases[1].patch.size, 23);
  assert.equal(catalogue.releases[1].checksum.id, 101);
  assert.equal(catalogue.releases[1].route, `/patches/${tag}/`);
});

test("output is deterministic across API order and equal timestamps", () => {
  const entries = [release("v30.3-roots.1", 1), release("v29.4-roots.2", 2)];
  assert.equal(serializeCatalogue(createCatalogue(entries)), serializeCatalogue(createCatalogue(entries.toReversed())));
});

test("exact patch filename is required", () => {
  const tag = "v29.4-roots.1";
  const catalogue = createCatalogue([release(tag, 1, { assets: [asset(tag, "other.patch")] })]);
  assert.equal(catalogue.releases[0].patch, null);
});

test("ambiguous patch, checksum, signature or release identities fail closed", () => {
  const tag = "v29.4-roots.1";
  for (const name of [expectedPatchName(tag), "SHA512SUMS", "SHA512SUMS.asc"]) {
    assert.throws(() => createCatalogue([release(tag, 1, { assets: [asset(tag, name), asset(tag, name)] })]), /Ambiguous/);
  }
  assert.throws(() => createCatalogue([release(), release()]), /duplicate/i);
  assert.throws(() => createCatalogue([release(), release("v29.4-roots.1", 2)]), /Duplicate release tag/);
});

test("invalid tags, paths, URLs and malformed metadata are rejected", () => {
  for (const tag of ["../x", "v29.4-roots.0", "v29.4-roots.1/evil", "v29.4-roots.1?x", "v29.4-roots.1%2f", "v29.4-roots.1\n"]) {
    assert.throws(() => createCatalogue([release(tag)]), /tag/);
  }
  for (const html_url of ["http://github.com/LuganoPlanB/bitcoin-roots/releases/tag/v29.4-roots.1", "https://evil.example/", "javascript:alert(1)"]) {
    assert.throws(() => createCatalogue([release(undefined, 1, { html_url })]), /URL/);
  }
  for (const extra of [{ published_at: null }, { published_at: "2026-02-30T00:00:00Z" }, { assets: {} }, { prerelease: "false" }, { id: -1 }]) {
    assert.throws(() => createCatalogue([release(undefined, 1, extra)]));
  }
  const tag = "v29.4-roots.1";
  for (const extra of [{ size: -1 }, { updated_at: "bad" }, { digest: "sha256:bad" }, { browser_download_url: "https://evil.example/" }]) {
    assert.throws(() => createCatalogue([release(tag, 1, { assets: [asset(tag, undefined, extra)] })]));
  }
});

test("malformed pages, repeated pages, exhausted pagination and API errors cannot produce partial catalogues", async () => {
  await assert.rejects(discoverReleases({ api: async () => ({ message: "rate limit" }) }), /Malformed/);
  await assert.rejects(discoverReleases({ api: async () => [release()], maxPages: 2 }), /pagination limit/);
  await assert.rejects(discoverReleases({ api: async () => { throw new Error("rate limit"); } }), /rate limit/);
  await assert.rejects(discoverReleases({ api: async (page) => page < 3 ? [release()] : [] }), /duplicate/i);
});

test("HTTP rate limits, server errors and malformed JSON fail without exposing bodies", async () => {
  for (const status of [403, 429, 500]) {
    await assert.rejects(fetchReleasePage(1, { fetchImpl: async () => new Response("private body", { status }) }), new RegExp(`HTTP ${status}`));
  }
  await assert.rejects(fetchReleasePage(1, { fetchImpl: async () => new Response("not JSON") }), /JSON/);
});

test("API requests use fixed paginated URL and bounded time/body", async () => {
  let request;
  assert.deepEqual(await fetchReleasePage(2, { fetchImpl: async (url, options) => { request = { url, options }; return new Response("[]"); } }), []);
  assert.equal(request.url, "https://api.github.com/repos/LuganoPlanB/bitcoin-roots/releases?per_page=100&page=2");
  assert.equal(request.options.redirect, "error");
  await assert.rejects(fetchReleasePage(1, { maxBytes: 2, fetchImpl: async () => new Response("large") }), /byte limit/);
  await assert.rejects(fetchReleasePage(1, {
    timeoutMs: 5,
    fetchImpl: (_url, { signal }) => new Promise((_resolve, reject) => signal.addEventListener("abort", () => reject(new Error("abort")))),
  }), /timed out/);
});
