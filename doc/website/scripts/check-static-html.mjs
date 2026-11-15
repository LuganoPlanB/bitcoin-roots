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
  "privacy.html": [
    "<h1",
    "Privacy settings",
    "Defaults at a glance",
    "Private transaction broadcasting",
    "Wallet privacy and coin selection",
    "Versioned references",
  ],
  "index.html": [
    'class="roots-home"',
    "Your node. Your policy. Bitcoin consensus.",
    "Bitcoin Core-compatible consensus",
    "Independently verify Bitcoin",
    "Know exactly where policy ends",
    "Open development for security-critical software",
    "Get started",
  ],
  "principles.html": [
    "<h1",
    "Principles",
    "Bitcoin consensus is shared",
    "Policy belongs to the operator",
    "Practical consequences",
    "Compare implementations",
  ],
  "compare.html": [
    "<h1",
    "Compare Bitcoin node implementations",
    "Versions reviewed",
    "v32.1-roots.1",
    "v32.1",
    "v29.4.3.knots20260508",
    "Roots-only features",
    "exportaddresskey RPC",
    "--allow-dangerous-rpc",
    "getmempoolinfo.policy",
    "Private-key sweeping",
    "Advanced privacy-aware coin control",
    "Monotonic process-uptime reporting",
    "Windows taskbar synchronization progress",
    "Project-specific consensus changes",
    "Frequently asked questions",
  ],
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
  console.log("Verified server-rendered content in the homepage, Principles, Compare, and Privacy HTML.");
}
