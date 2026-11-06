import assert from "node:assert/strict";
import { mkdtemp, mkdir, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import test from "node:test";
import { createHash } from "node:crypto";
import { loadSiteShell, validateSiteShell } from "../../scripts/patch-site-shell.mjs";
import { patchDocument } from "../../scripts/patch-page.mjs";
import { siteShell } from "./site-shell-fixture.mjs";

test("shared header and footer are taken verbatim from the ordinary VitePress shell", async (t) => {
    const root = await mkdtemp(join(tmpdir(), "roots-shell-"));
    t.after(() => rm(root, { recursive: true, force: true }));
    const expected = { ...siteShell("/bitcoin-roots/"), styles: ["assets/style.fixture.css"] };
    const metadata = { site: { base: expected.base, themeConfig: {} }, hashMap: {} };
    expected.data = `assets/patch-site-data.${createHash("sha256").update(JSON.stringify(metadata)).digest("hex").slice(0, 16)}.json`;
    await mkdir(join(root, "patches"));
    await writeFile(join(root, "patch-header-assets.json"), JSON.stringify({ schemaVersion: 1, script: expected.script, styles: [] }));
    await writeFile(join(root, "patches/index.html"), `<html lang="en-US"><head><link rel="stylesheet" href="/bitcoin-roots/assets/style.fixture.css"></head><body>${expected.layout}${expected.skip}${expected.header}${expected.footer}</div></body></html>`);
    await writeFile(join(root, "getting-started.html"), `<script>window.__VP_SITE_DATA__=JSON.parse(${JSON.stringify(JSON.stringify(metadata.site))});window.__VP_HASH_MAP__=JSON.parse("{}");</script>`);
    assert.deepEqual(await loadSiteShell(root, expected.base), expected);
});

test("missing shared chrome, unsafe runtime assets and mismatched bases cannot replace the main header", () => {
    const shell = siteShell();
    for (const changes of [{ header: '<header class="patch-header">Other menu</header>' }, { header: undefined },
        { lang: undefined }, { script: "https://external.example/header.js" }, { script: "assets/../header.js" },
        { styles: ["https://external.example/theme.css"] }, { footer: "" }]) {
        assert.throws(() => validateSiteShell({ ...shell, ...changes }), /Invalid shared website shell/);
    }
    assert.throws(() => patchDocument({ title: "Fixture", body: "", base: "/other/", siteShell: shell }), /base mismatch/);
});
