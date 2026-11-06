import { mkdir, readFile, writeFile } from "node:fs/promises";
import { createHash } from "node:crypto";
import { join } from "node:path";

const asset = (path, extension) => typeof path === "string" && new RegExp(`^assets/[A-Za-z0-9_.-]+\\.${extension}$`).test(path);

export function validateSiteShell(shell) {
    if (!shell || shell.schemaVersion !== 1 || !/^assets\/(?:chunks\/)?patch-header\.[A-Za-z0-9_-]+\.js$/.test(shell.script)
        || !/^assets\/patch-site-data\.[a-f0-9]{16}\.json$/.test(shell.data)
        || typeof shell.base !== "string" || !/^\/(?:[A-Za-z0-9_-]+\/)*$/.test(shell.base) || !Array.isArray(shell.styles)
        || !shell.styles.every((path) => asset(path, "css")) || typeof shell.lang !== "string" || !/^[A-Za-z-]+$/.test(shell.lang)
        || [shell.layout, shell.header, shell.footer, shell.skip].some((html) => typeof html !== "string")
        || !/^<div\b[^>]*class="Layout(?: [^"]*)?"[^>]*>$/.test(shell.layout)
        || !/^<header\b[^>]*class="VPNav"/.test(shell.header) || !shell.header.endsWith("</header>")
        || !/^<footer\b[^>]*class="VPFooter(?: [^"]*)?"/.test(shell.footer) || !shell.footer.endsWith("</footer>")
        || !/^<a\b[^>]*href="#VPContent"/.test(shell.skip) || !shell.skip.endsWith("</a>")
        || [shell.layout, shell.header, shell.footer, shell.skip].some((html) => /<script\b/i.test(html))) {
        throw new Error("Invalid shared website shell");
    }
    return shell;
}

// Read the normal VitePress page before the bounded patch renderer replaces its
// content. This keeps the header, footer, styles and links tied to the same build.
export async function loadSiteShell(siteDir, base) {
    const [html, assets, ordinaryPage] = await Promise.all([
        readFile(join(siteDir, "patches/index.html"), "utf8"),
        readFile(join(siteDir, "patch-header-assets.json"), "utf8").then(JSON.parse),
        readFile(join(siteDir, "getting-started.html"), "utf8"),
    ]);
    const embedded = (name) => {
        const match = new RegExp(`window\\.${name}=JSON\\.parse\\(("(?:\\\\.|[^"\\\\])*")\\)`).exec(ordinaryPage);
        if (!match) throw new Error("Missing VitePress initialization data");
        return JSON.parse(JSON.parse(match[1]));
    };
    const data = JSON.stringify({ site: embedded("__VP_SITE_DATA__"), hashMap: embedded("__VP_HASH_MAP__") });
    const dataFile = `assets/patch-site-data.${createHash("sha256").update(data).digest("hex").slice(0, 16)}.json`;
    await mkdir(join(siteDir, "assets"), { recursive: true });
    await writeFile(join(siteDir, dataFile), data);
    const styles = [...html.matchAll(/<link\b[^>]*rel="stylesheet"[^>]*href="([^"]+)"[^>]*>/g)]
        .map((match) => match[1]).filter((href) => href.startsWith(`${base}assets/`)).map((href) => href.slice(base.length));
    return validateSiteShell({ schemaVersion: assets.schemaVersion, base, script: assets.script, data: dataFile,
        styles: [...new Set([...styles, ...assets.styles])],
        lang: /<html\b[^>]*lang="([^"]+)"/.exec(html)?.[1],
        layout: /<div\b[^>]*class="Layout(?: [^"]*)?"[^>]*>/.exec(html)?.[0],
        header: /<header\b[^>]*class="VPNav"[\s\S]*?<\/header>/.exec(html)?.[0],
        footer: /<footer\b[^>]*class="VPFooter(?: [^"]*)?"[\s\S]*?<\/footer>/.exec(html)?.[0],
        skip: /<a\b[^>]*href="#VPContent"[^>]*>[\s\S]*?<\/a>/.exec(html)?.[0],
    });
}
