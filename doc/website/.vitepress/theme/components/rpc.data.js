import { createContentLoader } from "vitepress";

export default createContentLoader("documentation/rpc/*.md", {
  includeSrc: true,
  transform(pages) {
    return pages.filter(({ frontmatter }) => frontmatter.rpcCategory).map(({ frontmatter, src, url }) => ({
      name: frontmatter.title,
      category: frontmatter.rpcCategory,
      summary: frontmatter.description,
      description: src.match(/\n## How it works\n\n([\s\S]*?)\n## Arguments/)?.[1].replaceAll("\\_", "_") || "",
      change: frontmatter.rootsChange || "",
      url,
    })).sort((a, b) => a.name.localeCompare(b.name));
  },
});
