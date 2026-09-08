import assert from "node:assert/strict";
import { access, readFile } from "node:fs/promises";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import test from "node:test";

import { documentationCount, documentationGroups } from "../generated/catalog.mjs";

const siteRoot = resolve(dirname(fileURLToPath(import.meta.url)), "../..");
const repositoryRoot = resolve(siteRoot, "../..");
const workflowPath = resolve(repositoryRoot, ".github/workflows/deploy-docs.yml");

test("the catalog exposes every discovered source document exactly once", async () => {
  const documents = documentationGroups.flatMap((group) => group.documents);

  assert.ok(documentationCount >= 200, "expected the complete repository documentation set");
  assert.equal(documents.length, documentationCount);
  assert.equal(new Set(documents.map((document) => document.path)).size, documentationCount);

  await Promise.all(documents.map((document) => access(resolve(repositoryRoot, document.path))));
});

test("catalog routes are clean and rooted", () => {
  const documents = documentationGroups.flatMap((group) => group.documents);

  for (const document of documents) {
    assert.match(document.link, /^\//);
    assert.doesNotMatch(document.link, /\.md$/);
    assert.ok(document.title.length > 0, `missing title for ${document.path}`);
  }
});

test("the homepage keeps the operator journey and primary action", async () => {
  const home = await readFile(resolve(siteRoot, ".vitepress/theme/components/Home.vue"), "utf8");

  assert.match(home, /Get started/);
  assert.match(home, /Peer-to-peer network/);
  assert.match(home, /Independent validation/);
  assert.match(home, /Local node policy/);
  assert.match(home, /Your node/);
});

test("the homepage uses one observed Lottie play instead of a cycling hero", async () => {
  const [home, packageJson] = await Promise.all([
    readFile(resolve(siteRoot, ".vitepress/theme/components/Home.vue"), "utf8"),
    readFile(resolve(siteRoot, "package.json"), "utf8"),
    access(resolve(siteRoot, "content/public/bitcoin-roots.lottie.json")),
  ]);

  assert.match(home, /IntersectionObserver/);
  assert.match(home, /prefers-reduced-motion: reduce/);
  assert.match(home, /import\("lottie-web\/build\/player\/lottie_light"\)/);
  assert.match(home, /loop:\s*false/);
  assert.doesNotMatch(home, /setInterval/);
  assert.equal(JSON.parse(packageJson).dependencies["lottie-web"], "5.13.0");
});

test("website-owned navigation only targets published pages", async () => {
  const availableRoutes = new Set([
    "/",
    "/bitcoin-roots.lottie.json",
    "/documentation",
    "/getting-started",
    ...documentationGroups.flatMap((group) => group.documents.map((document) => document.link)),
  ]);
  const websiteFiles = [
    ".vitepress/theme/components/Home.vue",
    "content/getting-started.md",
  ];

  for (const file of websiteFiles) {
    const source = await readFile(resolve(siteRoot, file), "utf8");
    const routes = [
      ...source.matchAll(/withBase\(['"](\/[^'"]+)['"]\)/g),
      ...source.matchAll(/\]\((\/[^)]+)\)/g),
    ].map((match) => match[1]);

    for (const route of routes) {
      assert.ok(availableRoutes.has(route), `${file} targets unpublished route ${route}`);
    }
  }
});

test("the Pages workflow builds the website at the repository base path", async () => {
  const [workflow, config, atlas] = await Promise.all([
    readFile(workflowPath, "utf8"),
    readFile(resolve(siteRoot, ".vitepress/config.ts"), "utf8"),
    readFile(resolve(siteRoot, ".vitepress/theme/components/DocumentationIndex.vue"), "utf8"),
  ]);

  assert.match(workflow, /actions\/upload-pages-artifact@v4/);
  assert.match(workflow, /actions\/deploy-pages@v4/);
  assert.match(workflow, /DOCS_BASE:\s*\/\$\{\{ github\.event\.repository\.name \}\}\//);
  assert.match(workflow, /path:\s*bitcoin-roots\/doc\/website\/\.vitepress\/dist/);
  assert.match(config, /base:\s*docsBase/);
  assert.match(atlas, /withBase\(document\.link\)/);
});

test("explicit VitePress light mode overrides a dark system preference", async () => {
  const styles = await readFile(resolve(siteRoot, ".vitepress/theme/custom.css"), "utf8");

  assert.match(styles, /:root:not\(\.dark\):not\(\[data-theme="dark"\]\)/);
  assert.match(styles, /--planb-color-canvas:\s*#fffefa/);
  assert.match(styles, /--planb-color-text:\s*#030b20/);
  assert.match(styles, /--planb-color-accent:\s*#4f97e9/);
});
