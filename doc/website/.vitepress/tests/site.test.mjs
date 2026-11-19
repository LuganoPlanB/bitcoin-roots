import assert from "node:assert/strict";
import { access, readFile, readdir } from "node:fs/promises";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import test from "node:test";
import { filterCommands } from "../theme/components/rpc-filter.mjs";

import { documentationCount, documentationGroups } from "../generated/catalog.mjs";

const siteRoot = resolve(dirname(fileURLToPath(import.meta.url)), "../..");
const repositoryRoot = resolve(siteRoot, "../..");
const workflowPath = resolve(repositoryRoot, ".github/workflows/deploy-docs.yml");

test("documentation mirror symlinks target existing source paths", async () => {
  async function checkDirectory(directory) {
    for (const entry of await readdir(directory, { withFileTypes: true })) {
      const path = resolve(directory, entry.name);
      if (entry.isSymbolicLink()) {
        await access(path);
      } else if (entry.isDirectory()) {
        await checkDirectory(path);
      }
    }
  }

  await checkDirectory(resolve(siteRoot, "content"));
});

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

  assert.match(home, /Run your own Bitcoin node/);
  assert.match(home, /withBase\('\/getting-started'\)/);
  assert.match(home, /withBase\('\/compare'\)/);
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
  const rpcFiles = await readdir(resolve(siteRoot, "content/documentation/rpc"));
  const availableRoutes = new Set([
    "/documentation/rpc/",
    ...rpcFiles.filter(name => name.endsWith(".md")).map(name => "/documentation/rpc/" + name.slice(0, -3)),
    "/",
    "/bitcoin-roots.lottie.json",
    "/compare",
    "/documentation",
    "/getting-started",
    "/principles",
    "/privacy",
    "/wallet-privacy",
    "/patches/",
    ...documentationGroups.flatMap((group) => group.documents.map((document) => document.link)),
  ]);
  const websiteFiles = [
    ".vitepress/theme/components/DocumentationIndex.vue",
    ".vitepress/theme/components/Home.vue",
    "content/compare.md",
    "content/getting-started.md",
    "content/principles.md",
    "content/privacy.md",
    "content/wallet-privacy.md",
  ];

  for (const file of websiteFiles) {
    const source = await readFile(resolve(siteRoot, file), "utf8");
    const routes = [
      ...source.matchAll(/withBase\(['"](\/[^'"]+)['"]\)/g),
      ...source.matchAll(/\]\((\/[^)]+)\)/g),
    ].map((match) => match[1]);

    for (const route of routes) {
      const page = route.split(/[?#]/, 1)[0];
      assert.ok(availableRoutes.has(page), `${file} targets unpublished route ${route}`);
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
    ["Privacy", "/privacy"],
    ["Documentation", "/documentation"],
    ["Patches", "/patches/"],
  ]) {
    assert.match(config, new RegExp(`text: "${label}", link: "${route}"`));
  }
});

test("the first-run guide links the five published packages and both screenshots", async () => {
  const guide = await readFile(resolve(siteRoot, "content/getting-started.md"), "utf8");
  const latest = "https://github.com/LuganoPlanB/bitcoin-roots/releases/latest";
  assert.ok(guide.includes(`](${latest})`));
  for (const asset of [
    "darwin-arm64.zip", "darwin-x86_64.zip", "windows-x86_64.zip",
    "linux-aarch64.tar.gz", "linux-x86_64.tar.gz",
  ]) {
    assert.ok(guide.includes(`](${latest}/download/bitcoin-roots-${asset})`));
  }
  for (const image of ["pruning.png", "first-sync.png"]) {
    assert.ok(guide.includes(`](./images/${image})`));
    await access(resolve(siteRoot, "content/images", image));
  }
});

test("wallet guidance keeps the sweep and key-disclosure boundaries", async () => {
  const page = await readFile(resolve(siteRoot, "content/wallet-privacy.md"), "utf8");
  assert.match(page, /confirmed coins/);
  assert.match(page, /cancel until broadcast begins/);
  assert.match(page, /independent backup/);
  assert.match(page, /do not add CoinJoin or payjoin/);
  for (const method of ["exportaddresskey", "listdescriptors", "gethdkeys", "derivehdkey"]) {
    assert.ok(page.includes(method));
  }
  assert.match(page, /disabled by default/);
  assert.match(page, /--allow-dangerous-rpc/);
  assert.match(page, /cannot enable/);
  assert.match(page, /unlock/);
});

test("the contribution guide separates patch maintenance from Core ports", async () => {
  const guide = await readFile(resolve(repositoryRoot, "CONTRIBUTING.md"), "utf8");

  assert.match(guide, /Journey 1: Change the Roots patch/);
  assert.match(guide, /Journey 2: Port Roots to a new Core release/);
  assert.match(guide, /oldest supported canonical line that needs the change/);
  assert.match(guide, /create the canonical branch directly at that commit/);
  assert.match(guide, /Do not merge the complete old `roots\/<core-version>` branch/);
  assert.match(guide, /A release itself is the\s+immutable annotated tag/);
  assert.match(guide, /A commit being on `main` does not by itself make it part of\s+a release patch/);
});

test("Compare presents Core and Roots at two levels of detail", async () => {
  const page = await readFile(resolve(siteRoot, "content/compare.md"), "utf8");
  const table = await readFile(resolve(siteRoot, ".vitepress/theme/components/ComparisonTable.vue"), "utf8");
  assert.match(page, /<ComparisonTable \/>[\s\S]*<ComparisonTable detailed \/>/);
  assert.match(page, /github\.com\/bitcoin\/bitcoin\/tree\/v32\.1/);
  assert.match(page, /github\.com\/LuganoPlanB\/bitcoin-roots\/releases\/tag\/v32\.1-roots\.2/);
  assert.doesNotMatch(page + table, /knots/i);
  assert.match(table, /<th scope="col">Bitcoin Core<\/th>\s*<th scope="col">Bitcoin Roots<\/th>/);
  assert.match(table, /<th scope="row">/);
  assert.match(table, /role="region"/);
  assert.match(table, /tabindex="0"/);
});

test("RPC search combines descriptions, category and Roots changes", () => {
  const commands = [
    { name: "signmessage", category: "wallet", description: "Make a BIP322 proof.", change: "SegWit" },
    { name: "verifymessage", category: "util", description: "Check a BIP322 proof.", change: "SegWit" },
    { name: "signmessagewithprivkey", category: "util", description: "Legacy signature.", change: "" },
  ];
  assert.deepEqual(filterCommands(commands, "  BIP322 proof ", "", true), commands.slice(0, 2));
  assert.deepEqual(filterCommands(commands, "MESSAGE", "util", true), [commands[1]]);
  assert.deepEqual(filterCommands(commands, "legacy", "wallet", false), []);
  assert.deepEqual(filterCommands(commands, "", "", false), commands);
});
