import { createHash } from "node:crypto";
import { cp, lstat, mkdir, readFile, readdir, rename, rm, writeFile } from "node:fs/promises";
import { join } from "node:path";

const digest = (bytes) => createHash("sha256").update(bytes).digest("hex");
const sourceFiles = ["scripts/patch-series.mjs", "scripts/patch-renderer.mjs", "scripts/patch-page.mjs",
    "scripts/render-patches.mjs", "scripts/patch-cache.mjs", ".vitepress/theme/patches.css", "package-lock.json"];

export async function patchCacheKey({ websiteRoot, catalogue, base, siteStyles, limits, rendererVersion, rendererSettings }) {
    const sources = await Promise.all(sourceFiles.map(async (path) => [path, digest(await readFile(join(websiteRoot, path)))]));
    // Catalogue includes original asset digests and release metadata; source
    // hashes bind parser, adapter, templates, CSS and exact dependency versions.
    return digest(JSON.stringify({ schemaVersion: 1, catalogue, base, siteStyles, limits, rendererVersion, rendererSettings, sources }));
}

function safePath(path) {
    return typeof path === "string" && path.split("/").every((part) => /^[A-Za-z0-9_.-]+$/.test(part) && part !== "." && part !== "..");
}

async function filesBelow(directory, prefix = "") {
    const files = [];
    for (const name of (await readdir(join(directory, prefix))).sort()) {
        const path = prefix ? `${prefix}/${name}` : name;
        if (!safePath(path)) throw new Error("Unsafe generated cache path");
        const stat = await lstat(join(directory, path));
        if (stat.isSymbolicLink()) throw new Error("Symlink in generated cache");
        if (stat.isDirectory()) files.push(...await filesBelow(directory, path));
        else if (stat.isFile()) files.push(path);
        else throw new Error("Unsupported generated cache object");
    }
    return files;
}

export async function restorePatchCache({ cacheDir, key, temporary }) {
    if (!cacheDir) return null;
    const entry = join(cacheDir, key);
    let summary;
    try {
        if ((await lstat(entry)).isSymbolicLink() || (await lstat(join(entry, "pages"))).isSymbolicLink()) return null;
        const manifest = JSON.parse(await readFile(join(entry, "manifest.json"), "utf8"));
        if (manifest.schemaVersion !== 1 || manifest.key !== key || !Array.isArray(manifest.files)) return null;
        const actual = await filesBelow(join(entry, "pages"));
        if (actual.length !== manifest.files.length || !actual.includes("catalogue.json")) return null;
        for (let index = 0; index < actual.length; index++) {
            const expected = manifest.files[index];
            if (expected.path !== actual[index] || !safePath(expected.path) || !/^[a-f0-9]{64}$/.test(expected.sha256)) return null;
            const bytes = await readFile(join(entry, "pages", expected.path));
            if (bytes.length !== expected.bytes || digest(bytes) !== expected.sha256) return null;
        }
        summary = JSON.parse(await readFile(join(entry, "pages/catalogue.json"), "utf8"));
    } catch (error) {
        if (["EACCES", "ENOSPC", "EROFS"].includes(error.code)) throw error;
        return null;
    }
    // Copy failures are build errors, not permission to publish a partial cache.
    await cp(join(entry, "pages"), temporary, { recursive: true });
    return summary;
}

export async function storePatchCache({ cacheDir, key, directory }) {
    if (!cacheDir) return;
    const entry = join(cacheDir, key);
    const temporary = `${entry}.tmp-${process.pid}`;
    await mkdir(cacheDir, { recursive: true });
    await rm(temporary, { recursive: true, force: true });
    try {
        await cp(directory, join(temporary, "pages"), { recursive: true });
        const files = [];
        for (const path of await filesBelow(join(temporary, "pages"))) {
            const bytes = await readFile(join(temporary, "pages", path));
            files.push({ path, bytes: bytes.length, sha256: digest(bytes) });
        }
        await writeFile(join(temporary, "manifest.json"), `${JSON.stringify({ schemaVersion: 1, key, files })}\n`);
        await rm(entry, { recursive: true, force: true });
        await rename(temporary, entry);
    } catch (error) {
        await rm(temporary, { recursive: true, force: true });
        throw error;
    }
}
