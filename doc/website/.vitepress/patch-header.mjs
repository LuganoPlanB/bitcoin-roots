import { createSSRApp, h, provide, reactive } from "vue";
import { dataSymbol, initData, siteDataRef } from "vitepress/dist/client/app/data.js";
import { RouterSymbol } from "vitepress/dist/client/app/router.js";
import VPNav from "vitepress/dist/client/theme-default/components/VPNav.vue";

const host = document.getElementById("patch-site-header");
async function hydrateHeader() {
    // Local search uses the same published documentation modules as the normal
    // site. Load its small route/hash map, never patch bodies or patch metadata.
    const response = await fetch(host.dataset.siteData, { redirect: "error", signal: AbortSignal.timeout(10000) });
    if (!response.ok) throw new Error("Website search map is unavailable");
    const metadata = await response.json();
    if (!metadata.site || typeof metadata.site.base !== "string" || !metadata.site.themeConfig) throw new Error("Invalid website initialization data");
    siteDataRef.value = metadata.site;
    window.__VP_SITE_DATA__ = metadata.site;
    const map = metadata.hashMap;
    if (!map || Array.isArray(map) || typeof map !== "object" || Object.values(map).some((hash) => typeof hash !== "string" || !/^[A-Za-z0-9_-]+$/.test(hash))) throw new Error("Invalid website search map");
    window.__VP_HASH_MAP__ = map;
    createSSRApp({
        name: "PatchSiteHeader",
        setup() {
            // Matches the ordinary shell page used for SSR. All patch routes
            // belong to Patches; the actual patch document is outside this app.
            const route = reactive({ path: location.pathname, component: null, data: {
                title: "Release patches", relativePath: "patches/index.md", filePath: "patches/index.md",
                description: "Browse published Bitcoin Roots patch series.", headers: [],
                frontmatter: { layout: "page", sidebar: false, outline: false, search: false },
            } });
            provide(dataSymbol, initData(route));
            provide(RouterSymbol, { route, async go(href = location.href) { location.assign(href); } });
            return () => h(VPNav);
        },
    }).mount(host);
    // The standard mobile menu supplies its aria-expanded state. Escape also
    // returns focus to its trigger, without adding another navigation menu.
    host.addEventListener("keydown", (event) => {
        const trigger = host.querySelector('.VPNavBarHamburger[aria-expanded="true"]');
        if (event.key === "Escape" && trigger) {
            trigger.click();
            trigger.focus();
        }
        if (event.key === "Tab" && trigger) {
            const controls = [...host.querySelectorAll('a[href], button, input, select, [tabindex="0"]')]
                .filter((element) => element.getClientRects().length && !element.disabled);
            const edge = event.shiftKey ? controls[0] : controls.at(-1);
            if (document.activeElement === edge) {
                event.preventDefault();
                (event.shiftKey ? controls.at(-1) : controls[0]).focus();
            }
        }
    });
    host.dataset.hydrated = "true";
}
if (host) hydrateHeader().catch((error) => console.error("Shared website header could not initialize", error));
