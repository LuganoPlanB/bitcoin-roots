import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const websiteRoot = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const pages = {
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
    "v29.4.1.knots20260508",
    "Project-specific consensus changes",
    "Frequently asked questions",
  ],
  "features.html": [
    "<h1",
    "What Bitcoin Roots adds for users",
    "Inherited from Bitcoin Knots",
    "Spam filtering",
    "Network Watch",
    "Policy is not consensus",
  ],
};

for (const [filename, expectedContent] of Object.entries(pages)) {
  const html = await readFile(resolve(websiteRoot, ".vitepress/dist", filename), "utf8");

  for (const expected of expectedContent) {
    assert.ok(html.includes(expected), `${filename} is missing server-rendered content: ${expected}`);
  }
}

console.log("Verified server-rendered content in the homepage, Principles, Compare, and Features HTML.");
