// Public release metadata is an input, never a source of executable site code.
export const repository = "LuganoPlanB/bitcoin-roots";
export const releasesEndpoint = `https://api.github.com/repos/${repository}/releases`;
const tagPattern = /^v[0-9]+\.[0-9]+(?:\.[0-9]+|rc[0-9]+)?-roots\.[1-9][0-9]*$/;

function requireValue(condition, message) {
  if (!condition) throw new Error(message);
}

export function validateTag(tag) {
  requireValue(typeof tag === "string" && tag.length <= 100 && tagPattern.test(tag), "Invalid Roots release tag");
  return tag;
}

export function expectedPatchName(tag) {
  return `bitcoin-roots-${validateTag(tag).slice(1)}.patch`;
}

export function validateReleaseUrl(value, tag, name) {
  requireValue(typeof value === "string", "Missing release URL");
  const expected = `https://github.com/${repository}/releases/${name ? `download/${tag}/${name}` : `tag/${tag}`}`;
  requireValue(value === expected, "Invalid release URL or asset destination");
  return value;
}

function timestamp(value, field) {
  requireValue(typeof value === "string" && /^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z$/.test(value)
    && Number.isFinite(Date.parse(value)) && new Date(value).toISOString() === value.replace("Z", ".000Z"), `Invalid ${field}`);
  return value;
}

function assetMetadata(asset, tag) {
  requireValue(Number.isSafeInteger(asset.id) && asset.id > 0, "Invalid asset ID");
  requireValue(Number.isSafeInteger(asset.size) && asset.size >= 0, "Invalid asset byte size");
  requireValue(asset.digest == null || /^sha256:[a-fA-F0-9]{64}$/.test(asset.digest), "Invalid asset digest");
  return {
    id: asset.id,
    name: asset.name,
    url: validateReleaseUrl(asset.browser_download_url, tag, asset.name),
    size: asset.size,
    updatedAt: timestamp(asset.updated_at, "asset update timestamp"),
    digest: asset.digest?.toLowerCase() ?? null,
  };
}

function selectAsset(assets, name, tag) {
  const matches = assets.filter((asset) => asset.name === name);
  requireValue(matches.length <= 1, `Ambiguous ${name} asset`);
  return matches.length ? assetMetadata(matches[0], tag) : null;
}

export function createCatalogue(releases) {
  requireValue(Array.isArray(releases), "Malformed release list");
  const tags = new Set();
  const ids = new Set();
  const entries = [];
  for (const release of releases) {
    requireValue(release && typeof release === "object" && typeof release.draft === "boolean", "Malformed release");
    if (release.draft) continue;
    const tag = validateTag(release.tag_name);
    requireValue(Number.isSafeInteger(release.id) && release.id > 0 && !ids.has(release.id), "Invalid or duplicate release ID");
    requireValue(!tags.has(tag), "Duplicate release tag");
    requireValue(typeof release.prerelease === "boolean" && Array.isArray(release.assets)
      && release.assets.every((asset) => asset && typeof asset === "object" && typeof asset.name === "string"), "Malformed release assets or prerelease flag");
    tags.add(tag);
    ids.add(release.id);
    const patch = selectAsset(release.assets, expectedPatchName(tag), tag);
    entries.push({
      id: release.id,
      tag,
      url: validateReleaseUrl(release.html_url, tag),
      publishedAt: timestamp(release.published_at, "publication timestamp"),
      prerelease: release.prerelease,
      route: `/patches/${tag}/`,
      patchState: patch ? "available" : "unavailable",
      patch,
      checksum: selectAsset(release.assets, "SHA512SUMS", tag),
      signature: selectAsset(release.assets, "SHA512SUMS.asc", tag),
    });
  }
  entries.sort((a, b) => b.publishedAt.localeCompare(a.publishedAt) || (a.tag < b.tag ? -1 : a.tag > b.tag ? 1 : 0));
  return { schemaVersion: 1, repository, releases: entries };
}

// Explicit page numbers keep pagination confined to the fixed API endpoint.
// A final empty page proves completion even if an intermediate page is short.
export async function discoverReleases({ api = fetchReleasePage, maxPages = 1000 } = {}) {
  requireValue(Number.isSafeInteger(maxPages) && maxPages > 0 && maxPages <= 1000, "Invalid pagination limit");
  const releases = [];
  for (let page = 1; page <= maxPages; page++) {
    const batch = await api(page);
    requireValue(Array.isArray(batch) && batch.length <= 100, "Malformed GitHub releases page");
    if (batch.length === 0) return createCatalogue(releases);
    releases.push(...batch);
  }
  throw new Error("Release pagination limit exceeded; catalogue incomplete");
}

export async function fetchReleasePage(page, { fetchImpl = fetch, timeoutMs = 30000, maxBytes = 8 * 1024 * 1024 } = {}) {
  requireValue(Number.isSafeInteger(page) && page > 0, "Invalid page number");
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), timeoutMs);
  try {
    const response = await fetchImpl(`${releasesEndpoint}?per_page=100&page=${page}`, {
      redirect: "error", signal: controller.signal,
      headers: { Accept: "application/vnd.github+json", "X-GitHub-Api-Version": "2022-11-28" },
    });
    requireValue(response.ok, `GitHub release discovery failed (HTTP ${response.status})`);
    const chunks = [];
    let bytes = 0;
    for await (const chunk of response.body) {
      bytes += chunk.length;
      if (bytes > maxBytes) {
        controller.abort();
        throw new Error("GitHub release metadata exceeds byte limit");
      }
      chunks.push(chunk);
    }
    return JSON.parse(Buffer.concat(chunks).toString("utf8"));
  } catch (error) {
    if (controller.signal.aborted) throw new Error("GitHub release discovery timed out or exceeded byte limit");
    throw error;
  } finally {
    clearTimeout(timer);
  }
}

export function serializeCatalogue(catalogue) {
  return `${JSON.stringify(catalogue, null, 2)}\n`;
}
