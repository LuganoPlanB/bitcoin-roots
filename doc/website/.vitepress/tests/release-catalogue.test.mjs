import assert from "node:assert/strict";
import test from "node:test";
import { createCatalogue, discoverReleases, expectedPatchName, fetchLatestRpcRelease, fetchReleasePage, serializeCatalogue } from "../../scripts/release-catalogue.mjs";

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

test("unsupported tags create no routes; unsafe known URLs and malformed metadata are rejected", () => {
  for (const tag of ["../x", "v29.4-roots.0", "v29.4-roots.1/evil", "v29.4-roots.1?x", "v29.4-roots.1%2f", "v29.4-roots.1\n"]) {
    const result = createCatalogue([release(tag)]);
    assert.equal(result.releases.length, 0);
    assert.equal(result.unsupportedReleases.length, 1);
    assert.equal(result.unsupportedReleases[0].tag, tag);
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

test("published temporary tags keep actual URLs and derive only an asset-declared version", () => {
  const tag = 'untagged-3450aebe1f5e3a73703b';
  const filename = 'bitcoin-roots-32.1-roots.1.patch';
  const result = createCatalogue([release(tag, 432629489, { assets: [asset(tag, filename), asset(tag, 'SHA512SUMS', { id: 101 })] })]);
  const entry = result.releases[0];
  assert.equal(entry.tag, tag);
  assert.equal(entry.temporaryTag, true);
  assert.equal(entry.versionTag, 'v32.1-roots.1');
  assert.equal(entry.route, `/patches/${tag}/`);
  assert.equal(entry.patch.url, `https://github.com/LuganoPlanB/bitcoin-roots/releases/download/${tag}/${filename}`);
  assert.equal(expectedPatchName(entry), filename);
  assert.throws(() => expectedPatchName(tag), /tag/);
  assert.equal(entry.signature, null);
});

test("unsupported or incomplete publication identities are visible without blocking known releases", () => {
  const tag = 'untagged-3450aebe1f5e3a73703b';
  const known = release('v31.1-roots.6', 1);
  for (const incomplete of [release(tag, 2), release('unversioned', 2), release('../<script>', 2), release(`v${'9'.repeat(120)}.1-roots.1`, 2),
    release(tag, 2, { assets: [asset(tag, 'bitcoin-roots-32.1-roots.1.patch')] }),
    release(tag, 2, { assets: [asset(tag, 'bitcoin-roots-32.1-roots.1.patch'), asset(tag, 'bitcoin-roots-31.1-roots.1.patch', { id: 101 })] })]) {
    const result = createCatalogue([known, incomplete, { draft: true }]);
    assert.equal(result.releases.length, 1);
    assert.equal(result.releases[0].tag, known.tag_name);
    assert.equal(result.unsupportedReleases.length, 1);
    assert.equal(result.unsupportedReleases[0].id, 2);
    assert.equal(result.unsupportedReleases[0].patch, undefined);
  }
  assert.throws(() => createCatalogue([release(tag, 2, { assets: [asset(tag, 'bitcoin-roots-32.1-roots.1.patch', { browser_download_url: 'https://evil.example/patch' }), asset(tag, 'SHA512SUMS', { id: 101 })] })]), /URL/);
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

test("optional API credentials stay in the fixed request header with redirects disabled", async () => {
  for (const token of [undefined, "ghs_mock_read_only_token"]) {
    const calls = [];
    await fetchReleasePage(3, { token, fetchImpl: async (url, options) => {
      calls.push({ url, options });
      return new Response("[]");
    } });
    assert.equal(calls.length, 1);
    const { url, options } = calls[0];
    assert.equal(url, "https://api.github.com/repos/LuganoPlanB/bitcoin-roots/releases?per_page=100&page=3");
    assert.equal(options.redirect, "error");
    assert.equal(options.method, "GET");
    assert.equal(options.body, undefined);
    assert.equal(options.headers.Authorization, token === undefined ? undefined : `Bearer ${token}`);
    assert.equal(options.headers.Accept, "application/vnd.github+json");
    assert.equal(options.headers["X-GitHub-Api-Version"], "2022-11-28");
  }
  for (const token of ["", "bad\r\nheader", "has space", 123, "x".repeat(4097)]) {
    await assert.rejects(fetchReleasePage(1, { token, fetchImpl: () => assert.fail("invalid credential must not fetch") }),
      { message: "Invalid GitHub releases API token" });
  }
});

test("authenticated discovery finishes pagination and excludes drafts before validating or serializing them", async (t) => {
  const token = "ghs_private_mock_marker";
  const calls = [];
  const pages = [[{ draft: true, id: -1, assets: token, tag_name: token, body: token }],
    [release("v30.3rc1-roots.1", 1, { prerelease: true })], []];
  t.mock.method(globalThis, "fetch", async (url, options) => {
    calls.push(url);
    assert.equal(options.headers.Authorization, `Bearer ${token}`);
    assert.equal(options.redirect, "error");
    return Response.json(pages[Number(new URL(url).searchParams.get("page")) - 1]);
  });
  const result = await discoverReleases({ token });
  assert.deepEqual(calls.map((url) => new URL(url).searchParams.get("page")), ["1", "2", "3"]);
  assert.equal(result.releases.length, 1);
  assert.equal(result.releases[0].prerelease, true);
  assert.doesNotMatch(serializeCatalogue(result), new RegExp(token));
});

test("authenticated HTTP failures never retry anonymously or expose response bodies", async () => {
  const token = "ghs_error_mock_marker";
  for (const status of [301, 302, 401, 403, 429, 500]) {
    let calls = 0;
    await assert.rejects(fetchReleasePage(1, { token, fetchImpl: async (_url, options) => {
      calls++;
      assert.equal(options.headers.Authorization, `Bearer ${token}`);
      assert.equal(options.redirect, "error");
      return new Response(`private body ${token}`, { status, headers: { location: "https://evil.example/" } });
    } }), { message: `GitHub release discovery failed (HTTP ${status})` });
    assert.equal(calls, 1);
  }
});

test("request, stream and JSON exceptions are redacted; authenticated bounds remain enforced", async () => {
  const token = "ghs_exception_mock_marker";
  for (const fetchImpl of [
    async () => { throw new Error(`request Authorization: Bearer ${token}`); },
    async () => new Response(new ReadableStream({ start(controller) { controller.error(new Error(`stream ${token}`)); } })),
    async () => new Response(`private malformed JSON ${token}`),
  ]) {
    await assert.rejects(fetchReleasePage(1, { token, fetchImpl }), (error) => {
      assert.doesNotMatch(error.stack, new RegExp(`${token}|private|Authorization`));
      assert.equal(error.cause, undefined);
      return true;
    });
  }
  await assert.rejects(fetchReleasePage(1, { token, maxBytes: 2, fetchImpl: async () => new Response("large") }), /byte limit/);
  await assert.rejects(fetchReleasePage(1, { token, timeoutMs: 5,
    fetchImpl: (_url, { signal }) => new Promise((_resolve, reject) => signal.addEventListener("abort", () => reject(new Error(token)))),
  }), /timed out/);
});

test("the RPC source follows GitHub's stable alias and requires the package digest", async () => {
  const tag = "v32.1-roots.2";
  const binary = asset(tag, "bitcoin-roots-linux-x86_64.tar.gz");
  const resolve = data => fetchLatestRpcRelease({ fetchImpl: async url => {
    assert.equal(url, "https://api.github.com/repos/LuganoPlanB/bitcoin-roots/releases/latest");
    return Response.json(data);
  } });
  assert.deepEqual(await resolve(release(tag, 1, { assets: [binary] })), { tag, digest: binary.digest });
  await assert.rejects(resolve(release(tag, 1, { prerelease: true })), /stable Roots release/);
  await assert.rejects(resolve(release(tag)), /digest/);
});
