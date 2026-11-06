import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";
import { runInNewContext } from "node:vm";
import { fileNavigation, patchDocument } from "../../scripts/patch-page.mjs";
import { siteShell } from "./site-shell-fixture.mjs";

const script = await readFile(new URL("../../scripts/patch-theme.js", import.meta.url), "utf8");
function appearance({ preference, systemDark = false, storageDisabled = false } = {}) {
    const classes = new Set();
    runInNewContext(script, {
        document: { documentElement: { classList: { toggle(name, active) { active ? classes.add(name) : classes.delete(name); } } } },
        matchMedia: () => ({ matches: systemDark }),
        localStorage: { getItem(key) { assert.equal(key, "vitepress-theme-appearance"); if (storageDisabled) throw Error("disabled"); return preference; } },
    });
    return classes;
}

test("prepaint appearance respects the shared VitePress preference", () => {
    assert.equal(appearance({ preference: "light", systemDark: true }).has("dark"), false);
    assert.equal(appearance({ preference: "dark", systemDark: false }).has("dark"), true);
});

test("system appearance, blocked storage and invalid values initialize safely", () => {
    assert.equal(appearance({ preference: "auto", systemDark: true }).has("dark"), true);
    assert.equal(appearance({ preference: "auto", systemDark: false }).has("dark"), false);
    assert.equal(appearance({ preference: "invalid", systemDark: true }).has("dark"), true);
    assert.equal(appearance({ storageDisabled: true, systemDark: true }).has("dark"), true);
});

test("the shared site header precedes patch content and diff navigation remains local", () => {
    const shell = siteShell("/bitcoin-roots/");
    const html = patchDocument({ title: "Fixture", body: "<h1>Fixture</h1>", base: shell.base, siteShell: shell });
    assert.ok(html.indexOf("patch-theme.js") < html.indexOf('rel="stylesheet"'));
    assert.ok(html.includes(`<div id="patch-site-header" data-site-data="${shell.base}${shell.data}">${shell.header}</div>`));
    assert.ok(html.indexOf(shell.header) < html.indexOf('id="patch-content"'));
    assert.match(html, /id="VPContent" class="patch-content" tabindex="-1"/);
    assert.doesNotMatch(html, /patch-header-controls|data-patch-appearance/);
    const nav = fileNavigation({ fileRoute: "/patches/tag/commit-1/file-1/", page: 2, pageCount: 3, view: "side-by-side" });
    assert.match(nav, /side-by-side\/page-2\/" aria-current="page">Side by side/);
    assert.match(nav, /href="\/patches\/tag\/commit-1\/file-1\/page-2\/">Unified/);
});
