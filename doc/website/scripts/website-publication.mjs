import { createHash } from "node:crypto";
import { appendFile, mkdir, readFile, rename, writeFile } from "node:fs/promises";
import { join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { discoverReleases, fetchReleasePage, repository } from "./release-catalogue.mjs";
import { defaultInputDir, prepareReleases } from "./prepare-releases.mjs";

const schemaVersion = 1;
export const snapshotPath = join(defaultInputDir, "publication.json");
const digest = (value) => createHash("sha256").update(JSON.stringify(value)).digest("hex");

function revisions({ siteRevision, themeRevision, base }) {
    if (![siteRevision, themeRevision].every((value) => /^[a-f0-9]{40}$/.test(value))
        || typeof base !== "string" || !/^\/(?:[A-Za-z0-9_-]+\/)*$/.test(base)) {
        throw new Error("Invalid publication revision or base");
    }
}

// The complete normalized catalogue includes missing patches and checksum /
// signature identities. Release edits are hashed too, without logging bodies.
export async function publicationSnapshot({ siteRevision, themeRevision, base, api = fetchReleasePage }) {
    revisions({ siteRevision, themeRevision, base });
    const metadata = new Map();
    const catalogue = await discoverReleases({ api: async (page) => {
        const batch = await api(page);
        if (Array.isArray(batch)) for (const release of batch) {
            if (release && !release.draft) metadata.set(release.id, {
                id: release.id, updatedAt: release.updated_at ?? null,
                name: release.name ?? null, body: release.body ?? null,
                target: release.target_commitish ?? null,
            });
        }
        return batch;
    } });
    const inputs = { schemaVersion, repository, siteRevision, themeRevision, base, catalogue,
        releaseMetadataDigest: digest(catalogue.releases.map((release) => metadata.get(release.id))) };
    return { ...inputs, fingerprint: digest(inputs) };
}

function validateSnapshot(snapshot) {
    revisions(snapshot);
    const { fingerprint, ...inputs } = snapshot;
    if (snapshot.schemaVersion !== schemaVersion || snapshot.repository !== repository
        || snapshot.catalogue?.repository !== repository || !Array.isArray(snapshot.catalogue?.releases)
        || !/^[a-f0-9]{64}$/.test(snapshot.releaseMetadataDigest)
        || fingerprint !== digest(inputs)) throw new Error("Invalid publication snapshot");
    return snapshot;
}

export function publicationReceipt(snapshot) {
    validateSnapshot(snapshot);
    return { schemaVersion, repository, fingerprint: snapshot.fingerprint,
        siteRevision: snapshot.siteRevision, themeRevision: snapshot.themeRevision, base: snapshot.base,
        releaseCount: snapshot.catalogue.releases.length,
        availableCount: snapshot.catalogue.releases.filter((release) => release.patch).length };
}

export function needsPublication(snapshot, deployedReceipt) {
    const expected = publicationReceipt(snapshot);
    return !deployedReceipt || Object.entries(expected).some(([key, value]) => deployedReceipt[key] !== value);
}

// Only the receipt served by the successful Pages artifact is deployed evidence.
// Neither an Actions cache nor a previously uploaded/failed artifact is enough.
export async function fetchDeployedReceipt(siteUrl, { fetchImpl = fetch, timeoutMs = 15000, maxBytes = 65536 } = {}) {
    const url = new URL(siteUrl);
    if (url.protocol !== "https:" || url.username || url.password || url.search || url.hash) {
        throw new Error("Invalid public Pages URL");
    }
    url.pathname = `${url.pathname.replace(/\/$/, "")}/deployment.json`;
    url.searchParams.set("refresh", String(Date.now()));
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), timeoutMs);
    try {
        const response = await fetchImpl(url.href, { redirect: "error", signal: controller.signal,
            cache: "no-store", headers: { "Cache-Control": "no-cache" } });
        if (response.status === 404) { await response.body?.cancel(); return null; }
        if (!response.ok) throw new Error(`Deployed receipt fetch failed (HTTP ${response.status})`);
        let bytes = 0;
        const chunks = [];
        for await (const chunk of response.body) {
            bytes += chunk.length;
            if (bytes > maxBytes) { controller.abort(); throw new Error("Deployed receipt exceeds byte limit"); }
            chunks.push(chunk);
        }
        try { return JSON.parse(Buffer.concat(chunks).toString("utf8")); }
        catch { return null; } // Legacy HTML/invalid receipts require a fresh build.
    } finally {
        clearTimeout(timer);
    }
}

async function writeJson(path, value) {
    const temporary = `${path}.${process.pid}.tmp`;
    await writeFile(temporary, `${JSON.stringify(value, null, 2)}\n`);
    await rename(temporary, path);
}

export async function checkPublication({ inputDir = defaultInputDir, siteUrl, fetchImpl, ...options }) {
    const snapshot = await publicationSnapshot(options);
    const receipt = await fetchDeployedReceipt(siteUrl, { fetchImpl });
    const changed = needsPublication(snapshot, receipt);
    await mkdir(inputDir, { recursive: true });
    await writeJson(join(inputDir, "publication.json"), snapshot);
    return { snapshot, changed };
}

export async function preparePublication(snapshot, options = {}) {
    validateSnapshot(snapshot);
    return prepareReleases({ ...options, catalogue: snapshot.catalogue });
}

// Call only after the full build and its output checks succeed. This candidate
// becomes deployed evidence only when deploy-pages publishes the whole artifact.
export async function writePublicationReceipt(snapshot, { inputDir = defaultInputDir,
    outputDir = fileURLToPath(new URL("../.vitepress/dist/", import.meta.url)) } = {}) {
    validateSnapshot(snapshot);
    const verified = JSON.parse(await readFile(join(inputDir, "catalogue.json"), "utf8"));
    const inventory = { ...verified, releases: verified.releases.map(({ integrity, ...release }) => release) };
    if (digest(inventory) !== digest(snapshot.catalogue)) throw new Error("Built catalogue differs from publication snapshot");
    const rendered = JSON.parse(await readFile(join(outputDir, "patches/catalogue.json"), "utf8"));
    const expectedReleases = verified.releases.map((release) => ({ tag: release.tag, state: release.patchState,
        sha512: release.integrity?.sha512 ?? null }));
    const renderedReleases = rendered.releases?.map((release) => ({ tag: release.tag, state: release.state,
        sha512: release.sha512 ?? null }));
    if (rendered.base !== snapshot.base || digest(renderedReleases) !== digest(expectedReleases)) {
        throw new Error("Rendered catalogue differs from verified publication inputs");
    }
    await writeJson(join(outputDir, "deployment.json"), publicationReceipt(snapshot));
}

async function main() {
    const [command, ...args] = process.argv.slice(2);
    if (command === "check" && args.length === 0) {
        const { snapshot, changed } = await checkPublication({ siteRevision: process.env.SITE_REVISION,
            themeRevision: process.env.THEME_REVISION, base: process.env.DOCS_BASE, siteUrl: process.env.PAGES_URL });
        if (process.env.GITHUB_OUTPUT) await appendFile(process.env.GITHUB_OUTPUT,
            `changed=${changed}\nfingerprint=${snapshot.fingerprint}\n`);
        console.log(`Public inventory: ${snapshot.catalogue.releases.length} releases; publication ${changed ? "required" : "unchanged"}.`);
    } else if (["prepare", "receipt"].includes(command) && args.length === 0) {
        const snapshot = JSON.parse(await readFile(snapshotPath, "utf8"));
        if (command === "prepare") {
            const catalogue = await preparePublication(snapshot);
            console.log(`Prepared checked snapshot: ${catalogue.releases.length} public releases.`);
        } else {
            await writePublicationReceipt(snapshot);
            console.log("Validated publication receipt added to Pages artifact.");
        }
    } else throw new Error("Usage: node scripts/website-publication.mjs check|prepare|receipt");
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
    main().catch((error) => { console.error(`Website publication failed: ${error.message}`); process.exitCode = 1; });
}
