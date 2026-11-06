export function siteShell(base = "/") {
    return { schemaVersion: 1, base, lang: "en-US", script: "assets/chunks/patch-header.fixture.js", data: "assets/patch-site-data.0000000000000000.json", styles: [],
        layout: '<div class="Layout">',
        skip: '<a href="#VPContent" class="VPSkipLink">Skip to content</a>',
        header: `<header class="VPNav"><a href="${base}">Bitcoin Roots</a><nav>${[
            ["Get started", "getting-started"], ["Principles", "principles"], ["Compare", "compare"], ["Privacy", "privacy"],
            ["Documentation", "documentation"], ["Patches", "patches/"], ["32.x source", "doc/roots-v32-port-review"],
            ["Policy", "doc/policy/README"], ["Contribute", "CONTRIBUTING"],
        ].map(([text, path]) => `<a href="${base}${path}">${text}</a>`).join("")}</nav></header>`,
        footer: '<footer class="VPFooter">Bitcoin Roots is released under the MIT License.</footer>',
    };
}
