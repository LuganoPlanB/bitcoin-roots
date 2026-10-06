import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";
import { releaseIndex, releaseOverview } from "../../scripts/patch-overview.mjs";

const release = (tag, date, available = true) => ({ tag, publishedAt: `${date}T00:00:00Z`, prerelease: false,
    url: `https://github.com/LuganoPlanB/bitcoin-roots/releases/tag/${tag}`, patchState: available ? "available" : "unavailable",
    patch: available ? { size: 1024, url: `https://github.com/LuganoPlanB/bitcoin-roots/releases/download/${tag}/source.patch` } : null,
    integrity: { checksum: "verified", platformDigest: "missing" } });
const local = (route) => `/bitcoin-roots/patches/${route}`;

test("publication order includes missing and prerelease entries; highlight selects newest available", () => {
    const newest = release("v29.4-roots.4", "2026-10-06", false);
    const available = { ...release("v29.4-roots.3", "2026-10-05"), prerelease: true };
    const older = release("v30.3-roots.1", "2026-09-01");
    const html = releaseIndex([older, available, newest], local);
    assert.match(html, /Newest available patch<\/p><h2 id="patch-latest">v29.4-roots.3/);
    const list = html.slice(html.indexOf('<ol class="patch-release-list">'));
    assert.ok(list.indexOf(newest.tag) < list.indexOf(available.tag));
    assert.ok(list.indexOf(available.tag) < list.indexOf(older.tag));
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
