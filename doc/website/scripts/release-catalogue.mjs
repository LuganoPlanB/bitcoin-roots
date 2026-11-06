// Public release metadata is an input, never a source of executable site code.
export const repository = "LuganoPlanB/bitcoin-roots";
export const releasesEndpoint = `https://api.github.com/repos/${repository}/releases`;
const tagPattern = /^v[0-9]+\.[0-9]+(?:\.[0-9]+|rc[0-9]+)?-roots\.[1-9][0-9]*$/;
const temporaryTagPattern = /^untagged-[a-f0-9]{20}$/;

function requireValue(condition, message) {
  if (!condition) throw new Error(message);
}

export function validateTag(tag) {
  requireValue(typeof tag === "string" && tag.length <= 100 && tagPattern.test(tag), "Invalid Roots release tag");
  return tag;
}

export function validateReleaseTag(tag) {
  requireValue(typeof tag === "string" && tag.length <= 100
    && (tagPattern.test(tag) || temporaryTagPattern.test(tag)), "Invalid release tag reference");
  return tag;
}

export const isTemporaryTag = (tag) => typeof tag === "string" && temporaryTagPattern.test(tag);

export function releaseVersionTag(release) {
  if (typeof release === "string") return validateTag(release);
  validateReleaseTag(release.tag);
  if (!isTemporaryTag(release.tag)) {
    requireValue(!release.temporaryTag, "Unexpected temporary tag disclosure");
    return validateTag(release.tag);
  }
  requireValue(release.temporaryTag === true, "Missing temporary tag disclosure");
  return validateTag(release.versionTag);
}

export function expectedPatchName(release) {
  return `bitcoin-roots-${releaseVersionTag(release).slice(1)}.patch`;
}

export function validateReleaseUrl(value, tag, name) {
  validateReleaseTag(tag);
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
  const unsupportedReleases = [];
  for (const release of releases) {
    requireValue(release && typeof release === "object" && typeof release.draft === "boolean", "Malformed release");
    if (release.draft) continue;
    requireValue(Number.isSafeInteger(release.id) && release.id > 0 && !ids.has(release.id), "Invalid or duplicate release ID");
    requireValue(typeof release.prerelease === "boolean" && Array.isArray(release.assets)
      && release.assets.every((asset) => asset && typeof asset === "object" && typeof asset.name === "string"), "Malformed release assets or prerelease flag");
    ids.add(release.id);
    const publishedAt = timestamp(release.published_at, "publication timestamp");
    const unsupported = (reason) => unsupportedReleases.push({ id: release.id,
      tag: typeof release.tag_name === "string" ? release.tag_name.slice(0, 100) : "Missing tag",
      publishedAt, reason });
    const tag = release.tag_name;
    if (typeof tag !== "string" || tag.length > 100 || (!tagPattern.test(tag) && !isTemporaryTag(tag))) {
      unsupported("Unrecognized release tag; no patch was selected or downloaded.");
      continue;
    }
    requireValue(!tags.has(tag), "Duplicate release tag");
    tags.add(tag);
    const identity = { tag };
    if (isTemporaryTag(tag)) {
      const candidates = release.assets.filter((asset) => asset.name.endsWith(".patch"));
      const match = candidates.length === 1 && /^bitcoin-roots-(.+)\.patch$/.exec(candidates[0].name);
      if (!match || !tagPattern.test(`v${match[1]}`)) {
        unsupported("Temporary tag has no unique versioned Roots patch; no patch was downloaded.");
        continue;
      }
      identity.versionTag = validateTag(`v${match[1]}`);
      identity.temporaryTag = true;
    }
    const patch = selectAsset(release.assets, expectedPatchName(identity), tag);
    const checksum = selectAsset(release.assets, "SHA512SUMS", tag);
    if (identity.temporaryTag && (!patch.digest || !checksum?.digest)) {
      unsupported("Temporary tag requires SHA512SUMS and GitHub digests before patch browsing.");
      continue;
    }
    entries.push({
      id: release.id,
      ...identity,
      url: validateReleaseUrl(release.html_url, tag),
      publishedAt,
      prerelease: release.prerelease,
      route: `/patches/${tag}/`,
      patchState: patch ? "available" : "unavailable",
      patch,
      checksum,
      signature: selectAsset(release.assets, "SHA512SUMS.asc", tag),
    });
  }
  entries.sort((a, b) => b.publishedAt.localeCompare(a.publishedAt) || (a.tag < b.tag ? -1 : a.tag > b.tag ? 1 : 0));
  unsupportedReleases.sort((a, b) => b.publishedAt.localeCompare(a.publishedAt) || a.id - b.id);
  return { schemaVersion: 1, repository, releases: entries, ...(unsupportedReleases.length ? { unsupportedReleases } : {}) };
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
