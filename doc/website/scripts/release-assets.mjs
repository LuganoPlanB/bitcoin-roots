import { createHash } from "node:crypto";
import { mkdir, readFile, rename, stat, writeFile } from "node:fs/promises";
import { join } from "node:path";
import { expectedPatchName, validateReleaseUrl } from "./release-catalogue.mjs";

// Current published patches are ~3.3 MB. Bounds leave room for growth while
// preventing a remote asset from exhausting a static-build worker.
export const assetLimits = Object.freeze({ patchBytes: 20 * 1024 * 1024, checksumBytes: 2 * 1024 * 1024,
    timeoutMs: 30000, attempts: 3, redirects: 4 });
const hash = (bytes, algorithm = "sha512") => createHash(algorithm).update(bytes).digest("hex");
const requireValue = (condition, message) => { if (!condition) throw new Error(message); };
class RetryableError extends Error {}

function validateAsset(asset, tag, name, maxBytes) {
    requireValue(asset && asset.name === name, "Unexpected release asset name");
    validateReleaseUrl(asset.url, tag, name);
    requireValue(Number.isSafeInteger(asset.id) && asset.id > 0, "Invalid asset ID");
    requireValue(Number.isSafeInteger(asset.size) && asset.size >= 0 && asset.size <= maxBytes,
        "Release asset exceeds byte limit or has invalid size");
    requireValue(typeof asset.updatedAt === "string" && /^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z$/.test(asset.updatedAt)
        && new Date(asset.updatedAt).toISOString() === asset.updatedAt.replace("Z", ".000Z"), "Invalid asset update timestamp");
    requireValue(asset.digest === null || /^sha256:[a-f0-9]{64}$/.test(asset.digest), "Invalid platform digest");
}

function validatedDestination(value, initialUrl) {
    let url;
    try { url = new URL(value); } catch { throw new Error("Invalid asset redirect destination"); }
    requireValue(url.protocol === "https:" && !url.username && !url.password && !url.port && !url.hash,
        "Invalid asset redirect destination");
    requireValue(url.href === initialUrl || (url.hostname === "release-assets.githubusercontent.com"
        && /^\/github-production-release-asset\/[0-9]+\/[a-fA-F0-9-]+$/.test(url.pathname)),
        "Untrusted asset redirect destination");
    return url.href;
}

export async function downloadAsset(asset, { fetchImpl = fetch, maxBytes = assetLimits.patchBytes,
    timeoutMs = assetLimits.timeoutMs, attempts = assetLimits.attempts, maxRedirects = assetLimits.redirects,
    delay = (ms) => new Promise((resolve) => setTimeout(resolve, ms)) } = {}) {
    requireValue(Number.isSafeInteger(maxBytes) && maxBytes > 0 && maxBytes <= assetLimits.patchBytes,
        "Invalid download byte limit");
    requireValue(Number.isSafeInteger(timeoutMs) && timeoutMs > 0 && timeoutMs <= 120000, "Invalid download timeout");
    requireValue(Number.isSafeInteger(attempts) && attempts > 0 && attempts <= 3, "Invalid download attempt limit");
    requireValue(Number.isSafeInteger(maxRedirects) && maxRedirects >= 0 && maxRedirects <= 4, "Invalid redirect limit");
    requireValue(Number.isSafeInteger(asset.size) && asset.size >= 0 && asset.size <= maxBytes, "Asset exceeds byte limit");
    // Only validated catalogue URLs may start a request, including direct callers.
    const match = /^https:\/\/github\.com\/LuganoPlanB\/bitcoin-roots\/releases\/download\/([^/]+)\/([^/]+)$/.exec(asset.url);
    requireValue(match, "Invalid asset destination");
    requireValue([expectedPatchName(match[1]), "SHA512SUMS"].includes(match[2]), "Unexpected asset destination");
    validateAsset(asset, match[1], match[2], maxBytes);
    for (let attempt = 1; attempt <= attempts; attempt++) {
        const controller = new AbortController();
        let timer;
        const timeout = new Promise((_resolve, reject) => {
            timer = setTimeout(() => { controller.abort(); reject(new RetryableError("Asset download timed out")); }, timeoutMs);
        });
        try {
            return await Promise.race([timeout, (async () => {
                let url = asset.url;
                for (let redirects = 0; ; redirects++) {
                    let response;
                    try { response = await fetchImpl(url, { redirect: "manual", signal: controller.signal,
                        headers: { "Accept-Encoding": "identity" } }); }
                    catch { throw new RetryableError("Asset network request failed"); }
                    if ([301, 302, 303, 307, 308].includes(response.status)) {
                        await response.body?.cancel();
                        requireValue(redirects < maxRedirects, "Asset redirect limit exceeded");
                        const location = response.headers.get("location");
                        requireValue(location, "Missing asset redirect destination");
                        let destination;
                        try { destination = new URL(location, url).href; } catch { throw new Error("Invalid asset redirect destination"); }
                        url = validatedDestination(destination, asset.url);
                        continue;
                    }
                    if (!response.ok) {
                        await response.body?.cancel();
                        const ErrorType = [408, 429].includes(response.status) || response.status >= 500 ? RetryableError : Error;
                        throw new ErrorType(`Asset download failed (HTTP ${response.status})`);
                    }
                    if (response.status !== 200 || !response.body) {
                        await response.body?.cancel();
                        throw new Error("Invalid asset response");
                    }
                    const length = response.headers.get("content-length");
                    if (length !== null && (!/^\d+$/.test(length) || Number(length) !== asset.size)) {
                        await response.body.cancel();
                        throw new Error("Asset content length differs from release metadata");
                    }
                    const chunks = [];
                    let size = 0;
                    try {
                        for await (const chunk of response.body) {
                            size += chunk.length;
                            requireValue(size <= maxBytes && size <= asset.size, "Asset exceeds expected byte size");
                            chunks.push(Buffer.from(chunk));
                        }
                    } catch (error) {
                        if (error.message === "Asset exceeds expected byte size") throw error;
                        throw new RetryableError("Asset byte stream failed");
                    }
                    requireValue(size === asset.size, "Truncated asset or changed byte size");
                    return Buffer.concat(chunks);
                }
            })()]);
        } catch (error) {
            controller.abort();
            if (!(error instanceof RetryableError) || attempt === attempts) throw error;
            await delay(250 * attempt);
        } finally {
            clearTimeout(timer);
        }
    }
}

export function matchingChecksum(bytes, filename) {
    let text;
    try { text = new TextDecoder("utf-8", { fatal: true }).decode(bytes); }
    catch { throw new Error("Malformed SHA512SUMS encoding"); }
    const matches = [];
    for (const line of text.split(/\r?\n/)) {
        // Published manifests include comment-prefixed signing-key metadata.
        // It is informational only and does not authenticate these checksums.
        if (!line || line.startsWith("#")) continue;
        const entry = /^([a-fA-F0-9]{128}) [ *](.+)$/.exec(line);
        requireValue(entry, "Malformed SHA512SUMS entry");
        if (entry[2] === filename) matches.push(entry[1].toLowerCase());
    }
    requireValue(matches.length === 1, "SHA512SUMS must contain exactly one matching patch filename");
    return matches[0];
}

function checkBytes(bytes, asset, sha512 = null) {
    requireValue(bytes.length === asset.size, "Cached or downloaded asset size mismatch");
    if (asset.digest) requireValue(`sha256:${hash(bytes, "sha256")}` === asset.digest, "Platform asset digest mismatch");
    const digest = hash(bytes);
    if (sha512) requireValue(digest === sha512, "SHA512 asset digest mismatch");
    return digest;
}

// Bind each pointer to all selected public metadata. Replacements cannot reuse
// stale bytes, even when GitHub keeps the same filename or numeric asset ID.
export function assetCacheKey(asset) {
    return hash(Buffer.from(JSON.stringify([asset.id, asset.name, asset.url, asset.size, asset.updatedAt, asset.digest])), "sha256");
}

async function readOptional(path) {
    try {
        const info = await stat(path);
        requireValue(info.isFile() && info.size <= 512, "Invalid asset cache pointer size");
        return await readFile(path);
    } catch (error) { if (error.code === "ENOENT") return null; throw error; }
}

async function atomicWrite(path, bytes) {
    const temporary = `${path}.${process.pid}.tmp`;
    await writeFile(temporary, bytes);
    await rename(temporary, path);
}

export async function cachedAsset(asset, { cacheDir, tag, name = asset.name, maxBytes, sha512 = null, ...downloadOptions }) {
    validateAsset(asset, tag, name, maxBytes);
    const identity = assetCacheKey(asset);
    const pointerPath = join(cacheDir, `${identity}.json`);
    const pointerBytes = await readOptional(pointerPath);
    if (pointerBytes) {
        let pointer;
        try { pointer = JSON.parse(pointerBytes); } catch { throw new Error("Invalid asset cache pointer"); }
        requireValue(pointer.identity === identity && /^[a-f0-9]{128}$/.test(pointer.sha512), "Invalid asset cache identity or digest");
        const file = `${identity}-${pointer.sha512}.bin`;
        const path = join(cacheDir, file);
        let info;
        try { info = await stat(path); } catch { throw new Error("Missing cached asset bytes"); }
        requireValue(info.isFile() && info.size === asset.size && info.size <= maxBytes, "Cached asset size mismatch");
        const bytes = await readFile(path);
        checkBytes(bytes, asset, pointer.sha512);
        checkBytes(bytes, asset, sha512);
        return { bytes, sha512: pointer.sha512, file };
    }
    const bytes = await downloadAsset(asset, { ...downloadOptions, maxBytes });
    const digest = checkBytes(bytes, asset, sha512);
    const file = `${identity}-${digest}.bin`;
    await mkdir(cacheDir, { recursive: true });
    await atomicWrite(join(cacheDir, file), bytes);
    await atomicWrite(pointerPath, `${JSON.stringify({ identity, sha512: digest })}\n`);
    return { bytes, sha512: digest, file };
}

export async function verifyCatalogueAssets(catalogue, { cacheDir, ...downloadOptions }) {
    requireValue(typeof cacheDir === "string" && cacheDir.length > 0, "Missing asset cache directory");
    const releases = [];
    for (const release of catalogue.releases) {
        if (!release.patch) {
            requireValue(release.patchState === "unavailable", "Inconsistent unavailable patch state");
            releases.push({ ...release, integrity: null });
            continue;
        }
        const filename = expectedPatchName(release.tag);
        let expected = null;
        if (release.checksum) {
            const checksum = await cachedAsset(release.checksum, { ...downloadOptions, cacheDir,
                tag: release.tag, name: "SHA512SUMS", maxBytes: assetLimits.checksumBytes });
            expected = matchingChecksum(checksum.bytes, filename);
        }
        const patch = await cachedAsset(release.patch, { ...downloadOptions, cacheDir, tag: release.tag,
            name: filename, maxBytes: assetLimits.patchBytes, sha512: expected });
        releases.push({ ...release, integrity: { sha512: patch.sha512, cacheFile: patch.file,
            checksum: expected ? "verified" : "missing", platformDigest: release.patch.digest ? "verified" : "missing",
            signature: "not-verified" } });
    }
    return { ...catalogue, releases };
}
