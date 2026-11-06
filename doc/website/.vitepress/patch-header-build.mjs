import { fileURLToPath } from "node:url";

// Build the ordinary VitePress navigation as an island. Patch text never enters
// the Vue compiler or hydration tree, and links retain full-document navigation.
export function patchHeaderBuild() {
    const entry = fileURLToPath(new URL("./patch-header.mjs", import.meta.url));
    return {
        name: "bitcoin-roots:patch-header",
        apply: (config, { command }) => command === "build" && !config.build?.ssr,
        buildStart() {
            this.emitFile({ type: "chunk", id: entry, name: "patch-header" });
        },
        generateBundle: {
            order: "post",
            handler(_options, bundle) {
                const header = Object.values(bundle).find((item) => item.type === "chunk" && item.facadeModuleId === entry);
                if (!header) throw new Error("Shared patch header entry was not built");
                const styles = new Set();
                const seen = new Set();
                const visit = (chunk) => {
                    if (!chunk || chunk.type !== "chunk" || seen.has(chunk.fileName)) return;
                    seen.add(chunk.fileName);
                    for (const css of chunk.viteMetadata?.importedCss ?? []) styles.add(css);
                    for (const imported of chunk.imports) visit(bundle[imported]);
                };
                visit(header);
                this.emitFile({ type: "asset", fileName: "patch-header-assets.json",
                    source: JSON.stringify({ schemaVersion: 1, script: header.fileName, styles: [...styles].sort() }) });
            },
        },
    };
}
