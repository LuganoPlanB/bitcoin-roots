import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const websiteRoot = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const homepage = await readFile(resolve(websiteRoot, ".vitepress/dist/index.html"), "utf8");

for (const expected of [
  'class="roots-home"',
  "Your node.",
  "Bitcoin Roots.",
  "Built by OGs who keep the nodes running.",
  "Know exactly where policy ends.",
  "Get started",
]) {
  assert.ok(homepage.includes(expected), `static homepage is missing: ${expected}`);
}

console.log("Verified server-rendered homepage content in .vitepress/dist/index.html.");
