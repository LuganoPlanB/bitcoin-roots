import assert from "node:assert/strict";
import { readFile, readdir, stat } from "node:fs/promises";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const websiteRoot = resolve(dirname(fileURLToPath(import.meta.url)), "..");

export async function checkDocumentationRoutes(groups, outputDirectory) {
  const routes = new Set(groups.flatMap(({ documents }) => documents.map(({ link }) => link)));
  const missing = [];

  for (const route of routes) {
    try {
      const info = await stat(resolve(outputDirectory, `.${route}.html`));
      if (!info.isFile()) missing.push(route);
    } catch (error) {
      if (error.code !== "ENOENT") throw error;
      missing.push(route);
    }
  }

  assert.equal(missing.length, 0, `Missing built documentation routes:\n${missing.join("\n")}`);
  return routes.size;
}

const pages = {
  "privacy.html": ["Who can see my connection?", "private broadcasting"],
  "index.html": ['class="roots-home"', "Run your own Bitcoin node", "Get started", "Compare with Core", 'class="roots-signal__fallback"'],
  "principles.html": ["Every node checks the blocks", "You choose what your node relays"],
  "compare.html": ["At a glance", "A closer look", "Disk space", "Choosing coins to spend", "--allow-dangerous-rpc", "BIP322 simple proofs", "signmessage", "verifymessage"],
  "getting-started.html": ["Windows ZIP", "Apple silicon ZIP", "Linux ARM64 archive", "Let the first sync run"],
};

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const { documentationGroups } = await import("../.vitepress/generated/catalog.mjs");
  const outputDirectory = resolve(websiteRoot, ".vitepress/dist");
  const routeCount = await checkDocumentationRoutes(documentationGroups, outputDirectory);

  for (const [filename, expectedContent] of Object.entries(pages)) {
    const html = await readFile(resolve(outputDirectory, filename), "utf8");

    for (const expected of expectedContent) {
      assert.ok(html.includes(expected), `${filename} is missing server-rendered content: ${expected}`);
    }
  }

  const rpcDirectory = "documentation/rpc";
  const files = (await readdir(resolve(websiteRoot, "content", rpcDirectory)))
    .filter(name => name.endsWith(".md") && name !== "index.md");
  const index = await readFile(resolve(outputDirectory, rpcDirectory, "index.html"), "utf8");
  const base = process.env.DOCS_BASE || "/";
  assert.ok(files.length > 0 && index.includes("New or changed in Roots"));
  for (const file of files) {
    const name = file.slice(0, -3);
    const page = await readFile(resolve(outputDirectory, rpcDirectory, name + ".html"), "utf8");
    assert.ok(index.includes(`href="${base}${rpcDirectory}/${name}"`), `Missing RPC link: ${name}`);
    for (const section of ["bitcoin-cli", 'id="arguments"', 'id="return-value"']) {
      assert.ok(page.includes(section), `${name} missing ${section}`);
    }
  }
  const compare = await readFile(resolve(outputDirectory, "compare.html"), "utf8");
  for (const name of ["signmessage", "verifymessage", "getmempoolinfo", "sweepprivkeys", "exportaddresskey"]) {
    assert.ok(compare.includes(`href="${base}${rpcDirectory}/${name}"`), `Missing Compare link: ${name}`);
  }
  console.log(`Verified ${files.length} linked RPC command pages.`);
  console.log(`Verified all ${routeCount} catalogue documentation routes.`);
  console.log("Verified server-rendered content in the homepage, Get started, Principles, Compare, and Privacy HTML.");
}
