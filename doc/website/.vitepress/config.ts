import { defineConfig } from "vitepress";
import { documentationGroups } from "./generated/catalog.mjs";

const docsBase = process.env.DOCS_BASE || "/";

const htmlTags = new Set([
  "a", "abbr", "address", "article", "aside", "audio", "b", "blockquote", "br", "button", "caption", "cite", "code", "col",
  "colgroup", "dd", "del", "details", "div", "dl", "dt", "em", "figcaption",
  "figure", "footer", "form", "h1", "h2", "h3", "h4", "h5", "h6", "header", "hr", "i", "img", "input", "kbd",
  "label", "li", "main", "mark", "nav", "ol", "p", "picture", "pre", "s", "samp", "section", "small", "source", "span",
  "strong", "sub", "summary", "sup", "table", "tbody", "td", "template", "tfoot", "th",
  "thead", "time", "tr", "u", "ul", "var", "video", "home", "documentationindex",
]);

function makeLegacyMarkdownVueSafe(rendered: string) {
  const closedInlineNotation = rendered
    .replace(/<(sub|sup)>([^<>]*)<\1>/gi, "<$1>$2</$1>")
    .replace(
      /<(sub|sup)>([^<>]*)<\/(sub|sup)>/gi,
      (_, opening: string, body: string) => `<${opening}>${body}</${opening}>`,
    );

  const closedLists = closedInlineNotation.replace(
    /<(ul|ol)>([\s\S]*?)(?:<\/\1>|$)/gi,
    (_, tag: string, body: string) => {
      const items = body.replace(/<li>([\s\S]*?)(?=<li>|$)/gi, "<li>$1</li>");
      return `<${tag}>${items}</${tag}>`;
    },
  );

  return closedLists.replace(
    /<\/?([A-Za-z][\w-]*)(?:\s[^>]*)?>/g,
    (tag, name: string) => htmlTags.has(name.toLowerCase())
      ? tag
      : tag.replaceAll("<", "&lt;").replaceAll(">", "&gt;"),
  );
}

function legacyMarkdownPlaceholders() {
  return {
    name: "bitcoin-roots:legacy-markdown-placeholders",
    enforce: "pre" as const,
    transform(source: string, id: string) {
      if (!id.split("?", 1)[0].endsWith(".md")) return;

      const escapedPlaceholders = source.replace(
        /<\/?([A-Za-z][\w-]*)(?:\s[^>]*)?>/g,
        (tag, name: string) => htmlTags.has(name.toLowerCase())
          ? tag
          : tag.replaceAll("<", "&lt;").replaceAll(">", "&gt;"),
      );

      return escapedPlaceholders.replace(
        /(<(?:img|source)\b[^>]*\bsrc=["'])(?![./#]|https?:|data:)([^"']+)/gi,
        "$1./$2",
      );
    },
  };
}

const sidebarGroup = (id: string) => {
  const group = documentationGroups.find((candidate) => candidate.id === id);
  return group ? [{
    text: group.label,
    collapsed: id === "releases" || id === "internals",
    items: group.documents.map(({ link, title }) => ({ link, text: title })),
  }] : [];
};

export default defineConfig({
  srcDir: "./content",
  base: docsBase,
  title: "Bitcoin Roots",
  titleTemplate: ":title · Bitcoin Roots",
  description: "Connected to the trunk. Conservative in policy. Neutral in consensus.",
  lang: "en-US",
  cleanUrls: true,
  // The inherited corpus intentionally links to source files and directories
  // that are useful in a checkout but are not VitePress pages.
  ignoreDeadLinks: true,
  lastUpdated: true,
  srcExclude: [
    ".github/**",
    ".gestalt/**",
    ".impeccable/**",
    "AGENTS.md",
    "DESIGN.md",
    "PRODUCT.md",
    "node_modules/**",
  ],
  markdown: {
    lineNumbers: true,
    toc: { level: [2, 3] },
    config(markdown) {
      const renderInline = markdown.renderer.renderInline.bind(markdown.renderer);
      markdown.renderer.renderInline = (tokens, options, environment) =>
        makeLegacyMarkdownVueSafe(renderInline(tokens, options, environment));
    },
  },
  head: [
    ["link", { rel: "icon", type: "image/svg+xml", href: `${docsBase}favicon.svg` }],
    ["meta", { name: "theme-color", content: "#fffefa" }],
    ["meta", { name: "color-scheme", content: "light dark" }],
  ],
  vite: {
    plugins: [legacyMarkdownPlaceholders()],
    resolve: {
      // Keep linked Markdown modules anchored in this package so Vue and
      // VitePress runtime imports resolve from doc/website/node_modules.
      preserveSymlinks: true,
    },
  },
  themeConfig: {
    siteTitle: "Bitcoin Roots",
    nav: [
      { text: "Get started", link: "/getting-started" },
      { text: "Documentation", link: "/documentation" },
      { text: "Policy", link: "/doc/policy/README" },
      { text: "Contribute", link: "/CONTRIBUTING" },
    ],
    sidebar: {
      "/doc/policy/": sidebarGroup("policy"),
      "/doc/design/": sidebarGroup("design"),
      "/doc/release-notes/": sidebarGroup("releases"),
      "/doc/": sidebarGroup("operations"),
      "/contrib/": sidebarGroup("contrib"),
      "/src/": sidebarGroup("internals"),
      "/test/": sidebarGroup("testing"),
      "/ci/": sidebarGroup("testing"),
      "/depends/": sidebarGroup("dependencies"),
      "/share/": sidebarGroup("utilities"),
    },
    search: {
      provider: "local",
      options: {
        miniSearch: {
          searchOptions: { fuzzy: 0.2, prefix: true },
        },
      },
    },
    outline: { level: [2, 3], label: "On this page" },
    docFooter: { prev: "Previous guide", next: "Next guide" },
    lastUpdated: { text: "Source updated" },
    externalLinkIcon: true,
    socialLinks: [
      { icon: "github", link: "https://github.com/LuganoPlanB/bitcoin-roots" },
    ],
    footer: {
      message: "Released under the MIT License.",
      copyright: "Maintained by the Plan ₿ Foundation.",
    },
  },
});
