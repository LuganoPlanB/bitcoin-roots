import { mkdir, rename, writeFile } from "node:fs/promises";
import { join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { discoverReleases, serializeCatalogue } from "./release-catalogue.mjs";
import { verifyCatalogueAssets } from "./release-assets.mjs";

export const defaultInputDir = fileURLToPath(new URL("../.vitepress/release-inputs/", import.meta.url));

// Publish only a complete verified snapshot. A failed refresh leaves the last
// catalogue intact; individually verified cache objects remain reusable.
export async function prepareReleases({ inputDir = defaultInputDir, api, fetchImpl } = {}) {
    const catalogue = await discoverReleases({ api });
    const verified = await verifyCatalogueAssets(catalogue, { cacheDir: join(inputDir, "assets"), fetchImpl });
    await mkdir(inputDir, { recursive: true });
    const output = join(inputDir, "catalogue.json");
    const temporary = `${output}.${process.pid}.tmp`;
    await writeFile(temporary, serializeCatalogue(verified));
    await rename(temporary, output);
    return verified;
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
    if (process.argv.length !== 2) {
        console.error("Usage: node scripts/prepare-releases.mjs");
        process.exitCode = 1;
    } else {
        try {
            const catalogue = await prepareReleases();
            const available = catalogue.releases.filter((entry) => entry.patchState === "available").length;
            console.log(`Prepared ${catalogue.releases.length} public releases: ${available} patches, ${catalogue.releases.length - available} unavailable.`);
            console.log(`Catalogue: ${join(defaultInputDir, "catalogue.json")}`);
        } catch (error) {
            console.error(`Release preparation failed: ${error.message}`);
            process.exitCode = 1;
        }
    }
}
