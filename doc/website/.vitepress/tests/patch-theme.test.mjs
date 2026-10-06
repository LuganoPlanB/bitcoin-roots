import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";
import { runInNewContext } from "node:vm";
import { fileNavigation, patchDocument } from "../../scripts/patch-page.mjs";

const script = await readFile(new URL("../../scripts/patch-theme.js", import.meta.url), "utf8");
function appearance({ preference, systemDark = false, storageDisabled = false } = {}) {
    const events = new Map();
    const controlEvents = new Map();
    const systemEvents = new Map();
    const classes = new Set();
    const label = { hidden: true };
    const control = { value: "", closest: () => label, addEventListener: (name, fn) => controlEvents.set(name, fn) };
    const root = { dataset: {}, classList: { toggle(name, active) { active ? classes.add(name) : classes.delete(name); } } };
    const storage = new Map(preference ? [["vitepress-theme-appearance", preference]] : []);
    const system = { matches: systemDark, addEventListener: (name, fn) => systemEvents.set(name, fn) };
    runInNewContext(script, { document: { documentElement: root, querySelector: () => control },
        matchMedia: () => system, addEventListener: (name, fn) => events.set(name, fn),
        localStorage: { getItem(key) { if (storageDisabled) throw Error("disabled"); return storage.get(key); },
            setItem(key, value) { if (storageDisabled) throw Error("disabled"); storage.set(key, value); } } });
    events.get("DOMContentLoaded")();
    return { root, classes, control, label, storage, system, events, controlEvents, systemEvents };
}

test("explicit appearance overrides system and reuses the VitePress preference key", () => {
    for (const [preference, systemDark] of [["light", true], ["dark", false]]) {
        const state = appearance({ preference, systemDark });
        assert.equal(state.root.dataset.theme, preference);
        assert.equal(state.classes.has("dark"), preference === "dark");
        assert.equal(state.control.value, preference);
        assert.equal(state.label.hidden, false);
        state.control.value = preference === "light" ? "dark" : "light";
        state.controlEvents.get("change")();
        assert.equal(state.storage.get("vitepress-theme-appearance"), state.control.value);
        assert.equal(state.root.dataset.theme, state.control.value);
    }
});

test("system appearance follows changes; blocked storage and invalid values stay usable", () => {
    const state = appearance({ preference: "auto", systemDark: true });
    assert.equal(state.root.dataset.theme, "dark");
    state.system.matches = false;
    state.systemEvents.get("change")();
    assert.equal(state.root.dataset.theme, "light");
    state.events.get("storage")({ key: "vitepress-theme-appearance", newValue: "dark" });
    assert.equal(state.root.dataset.theme, "dark");
    assert.equal(state.control.value, "dark");
    assert.equal(appearance({ preference: "invalid", systemDark: true }).root.dataset.theme, "dark");
    const disabled = appearance({ storageDisabled: true });
    disabled.control.value = "dark";
    disabled.controlEvents.get("change")();
    assert.equal(disabled.root.dataset.theme, "dark");
});

test("theme initializes before CSS and diff view links expose their current state", () => {
    const html = patchDocument({ title: "Fixture", body: "<h1>Fixture</h1>", base: "/bitcoin-roots/" });
    assert.ok(html.indexOf("patch-theme.js") < html.indexOf('rel="stylesheet"'));
    assert.match(html, /Appearance<select data-patch-appearance>/);
    const nav = fileNavigation({ fileRoute: "/patches/tag/commit-1/file-1/", page: 2, pageCount: 3, view: "side-by-side" });
    assert.match(nav, /side-by-side\/page-2\/" aria-current="page">Side by side/);
    assert.match(nav, /href="\/patches\/tag\/commit-1\/file-1\/page-2\/">Unified/);
});
