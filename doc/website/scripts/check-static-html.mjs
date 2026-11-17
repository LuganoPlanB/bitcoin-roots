import assert from "node:assert/strict";
import { readFile, stat } from "node:fs/promises";
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
  "index.html": ['class="roots-home"', "Run your own Bitcoin node", "Get started", "Compare with Core"],
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

  console.log(`Verified all ${routeCount} catalogue documentation routes.`);
  console.log("Verified server-rendered content in the homepage, Get started, Principles, Compare, and Privacy HTML.");
}
