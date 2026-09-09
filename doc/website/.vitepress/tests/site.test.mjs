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

test("the homepage states the project position and operator journey precisely", async () => {
  const home = await readFile(resolve(siteRoot, ".vitepress/theme/components/Home.vue"), "utf8");

  assert.match(home, /Your node\. Your policy\. Bitcoin consensus\./);
  assert.match(home, /Bitcoin Core-compatible consensus/);
  assert.match(home, /does not enforce\s+RDTS\/BIP-110/);
  assert.match(home, /source-code fork/);
  assert.match(home, /It is not a separate cryptocurrency/);
  assert.match(home, /Get started/);
  assert.match(home, /Independently verify Bitcoin/);
  assert.match(home, /Know exactly where policy ends/);
  assert.match(home, /Documentation stays with the source/);
  assert.match(home, /Open development for security-critical software/);
});

test("new editorial copy avoids prohibited claims and punctuation", async () => {
  const prose = await Promise.all([
    readFile(resolve(siteRoot, ".vitepress/theme/components/Home.vue"), "utf8"),
    readFile(resolve(siteRoot, "content/principles.md"), "utf8"),
    readFile(resolve(siteRoot, "content/compare.md"), "utf8"),
    readFile(resolve(siteRoot, ".vitepress/theme/components/ComparisonTable.vue"), "utf8"),
    readFile(resolve(siteRoot, ".vitepress/theme/components/DocumentationIndex.vue"), "utf8"),
  ]);
  const source = prose.join("\n");

  assert.doesNotMatch(source, /forever faithful/i);
  assert.doesNotMatch(source, /built by OGs/i);
  assert.doesNotMatch(source, /revolutionary|next generation/i);
  assert.doesNotMatch(source, /—/);
  assert.doesNotMatch(source, /Plan-₿|Plan â|Bitcoin â/);
});

test("website-owned navigation only targets published pages", async () => {
  const availableRoutes = new Set([
    "/",
    "/bitcoin-roots.lottie.json",
    "/compare",
    "/documentation",
    "/getting-started",
    "/principles",
    ...documentationGroups.flatMap((group) => group.documents.map((document) => document.link)),
  ]);
  const websiteFiles = [
    ".vitepress/theme/components/Home.vue",
    "content/compare.md",
    "content/getting-started.md",
    "content/principles.md",
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

test("primary navigation includes the two editorial routes", async () => {
  const config = await readFile(resolve(siteRoot, ".vitepress/config.ts"), "utf8");

  for (const [label, route] of [
    ["Get started", "/getting-started"],
    ["Principles", "/principles"],
    ["Compare", "/compare"],
    ["Documentation", "/documentation"],
    ["Policy", "/doc/policy/README"],
    ["Contribute", "/CONTRIBUTING"],
  ]) {
    assert.match(config, new RegExp(`text: "${label}", link: "${route}"`));
  }
});

test("the comparison is versioned and linked to primary sources", async () => {
  const [page, table] = await Promise.all([
    readFile(resolve(siteRoot, "content/compare.md"), "utf8"),
    readFile(resolve(siteRoot, ".vitepress/theme/components/ComparisonTable.vue"), "utf8"),
  ]);
  const comparison = `${page}\n${table}`;

  assert.match(comparison, /v31\.1/);
  assert.match(comparison, /29\.3\.0\.roots20260507/);
  assert.match(comparison, /v29\.4\.1\.knots20260508/);
  assert.match(comparison, /9 September 2026/);
  assert.match(comparison, /github\.com\/bitcoin\/bitcoin/);
  assert.match(comparison, /github\.com\/LuganoPlanB\/bitcoin-roots/);
  assert.match(comparison, /github\.com\/bitcoinknots\/bitcoin/);
  assert.match(comparison, /BLAKE2b proof-of-work/);
  assert.match(comparison, /800 kWU/);
});

test("the comparison table is semantic and keyboard-scrollable", async () => {
  const table = await readFile(
    resolve(siteRoot, ".vitepress/theme/components/ComparisonTable.vue"),
    "utf8",
  );

  assert.match(table, /role="region"/);
  assert.match(table, /tabindex="0"/);
  assert.match(table, /<th scope="col">/);
  assert.match(table, /<th scope="row">/);
});
