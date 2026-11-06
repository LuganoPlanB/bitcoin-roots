import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";
import { releaseIndex, releaseOverview } from "../../scripts/patch-overview.mjs";

const release = (tag, date, available = true) => ({ tag, publishedAt: `${date}T00:00:00Z`, prerelease: false,
    url: `https://github.com/LuganoPlanB/bitcoin-roots/releases/tag/${tag}`, patchState: available ? "available" : "unavailable",
    patch: available ? { size: 1024, url: `https://github.com/LuganoPlanB/bitcoin-roots/releases/download/${tag}/source.patch` } : null,
    integrity: { checksum: "verified", platformDigest: "missing" } });
const local = (route) => `/bitcoin-roots/patches/${route}`;

test("major groups retain publication order, missing and prerelease entries; highlight selects newest available", () => {
    const newest = release("v29.4-roots.4", "2026-10-06", false);
    const available = { ...release("v29.4-roots.3", "2026-10-05"), prerelease: true };
    const older = release("v30.3-roots.1", "2026-09-01");
    const html = releaseIndex([older, available, newest], local);
    assert.match(html, /Newest available patch<\/p><h2 id="patch-latest">v29.4-roots.3/);
    const list = html.slice(html.indexOf('<ol class="patch-release-list">'));
    assert.ok(list.indexOf(newest.tag) < list.indexOf(available.tag));
    assert.ok(list.indexOf(older.tag) < list.indexOf(newest.tag));
    assert.ok(html.indexOf("<h3>Bitcoin Core 30</h3>") < html.indexOf("<h3>Bitcoin Core 29</h3>"));
    assert.match(list, /Prerelease/);
    assert.match(list, /Patch unavailable/);
    assert.match(list, /data-group="30.3"/);
    assert.match(html, /\/bitcoin-roots\/patches\/v29.4-roots.3\//);
});

test("missing and empty states remain explanatory without JavaScript", () => {
    assert.match(releaseIndex([], local), /No public releases/);
    const html = releaseOverview(release("v29.3-roots.1", "2026-01-01", false), null, local);
    assert.match(html, /Patch unavailable/);
    assert.match(html, /Release notes/);
    assert.doesNotMatch(html, /Download original patch|Download verification/);
});

test("32.x source navigation never creates an unpublished release entry or download", () => {
    const historical = release("v31.1-roots.6", "2026-10-20");
    const html = releaseIndex([historical], local);
    assert.match(html, /Source availability does not establish release publication/);
    assert.match(html, /href="\/bitcoin-roots\/doc\/roots-v32-port-review"/);
    assert.match(html, /href="\/bitcoin-roots\/patches\/v31\.1-roots\.6\/"/);
    assert.doesNotMatch(html, /releases\/download\/v32|patches\/v32/);
});

test("temporary publication is grouped by asset version with explicit provenance, without invented tag URLs", () => {
    const item = { ...release('untagged-3450aebe1f5e3a73703b', '2026-11-05'), temporaryTag: true, versionTag: 'v32.1-roots.1',
        patch: { size: 1234, url: 'https://github.com/LuganoPlanB/bitcoin-roots/releases/download/untagged-3450aebe1f5e3a73703b/bitcoin-roots-32.1-roots.1.patch' } };
    const index = releaseIndex([item], local, [{ tag: '<script>other</script>', reason: 'Unsupported tag' }]);
    assert.match(index, /Bitcoin Core 32/);
    assert.match(index, /v32\.1-roots\.1 \(temporary tag\)/);
    assert.match(index, /Other public release tags/);
    assert.match(index, /&lt;script&gt;other/);
    assert.doesNotMatch(index, /<script>|patches\/v32\.1-roots\.1/);
    const html = releaseOverview(item, { commits: [], fileCount: 0 }, local);
    assert.match(html, /Temporary GitHub tag/);
    assert.match(html, /from patch filename/);
    assert.match(html, /No SHA512SUMS.asc signature was published/);
    assert.match(html, /untagged-3450aebe1f5e3a73703b\/bitcoin-roots-32\.1-roots\.1.patch/);
});

test("overview retains distinct repeated paths, commit order, honest totals and escaped metadata", () => {
    const commit = (ordinal) => ({ id: `commit-${ordinal}`, ordinal, subject: `<script>Subject ${ordinal}</script>`, author: "Author",
        files: [{ ordinal: 1, path: 'src/<img onerror="x">.cpp', state: "text" }, { ordinal: 2, path: "README.md", state: "metadata-only" }] });
    const html = releaseOverview(release("v29.4-roots.4", "2026-10-06"), { commits: [commit(1), commit(2)], fileCount: 4 }, local);
    assert.ok(html.indexOf('id="commit-1"') < html.indexOf('id="commit-2"'));
    assert.match(html, /commit-1\/file-1\//);
    assert.match(html, /commit-2\/file-1\//);
    assert.match(html, /data-group="src"/);
    assert.match(html, /data-group="\(root files\)"/);
    assert.match(html, /including repeats/);
    assert.match(html, /not a delta/);
    assert.match(html, /Signature: not verified/);
    assert.doesNotMatch(html, /<(?:script|img)\b/);
});

test("Patches shell link opts into full-document navigation at the configured base", async () => {
    const config = await readFile(new URL("../config.ts", import.meta.url), "utf8");
    assert.match(config, /text: "Patches", link: "\/patches\/", target: "_self"/);
});


test("major grouping keeps exact Core lines, all release URLs and published ordinals", () => {
    const releases = [release("v31.1-roots.1", "2026-10-01"), release("v31.1-roots.2", "2026-10-10"),
        release("v31.0-roots.1", "2026-09-01"), release("v31.2rc1-roots.1", "2026-10-11")];
    const html = releaseIndex(releases, local);
    assert.equal((html.match(/<h3>Bitcoin Core 31<\/h3>/g) ?? []).length, 1);
    assert.match(html, /data-group="31.1"/);
    assert.match(html, /data-group="31.0"/);
    assert.match(html, /data-group="31.2rc1"/);
    const list = html.slice(html.indexOf('<ol class="patch-release-list">'));
    assert.ok(list.indexOf("v31.1-roots.2") < list.indexOf("v31.1-roots.1"));
    for (const item of releases) assert.ok(list.includes(`href="${local(`${item.tag}/`)}"`));
    assert.match(html, /Folding/);
    assert.match(html, /does not imply a shared base/);
});
