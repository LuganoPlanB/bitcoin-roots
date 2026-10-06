// Small progressive enhancement. All source and navigation are static HTML.
for (const form of document.querySelectorAll("[data-patch-filter]")) {
    const section = form.closest("section");
    const items = [...section.querySelectorAll("[data-filter-item]")];
    const commits = [...section.querySelectorAll("[data-commit-item]")];
    const search = form.elements.q;
    const group = form.elements.group;
    const params = new URLSearchParams(location.search);
    search.value = params.get("q") ?? "";
    if (group) group.value = params.get("group") ?? "";
    function filter(updateUrl = true) {
        const query = search.value.trim().toLocaleLowerCase();
        const selected = group?.value ?? "";
        let count = 0;
        for (const item of items) {
            item.hidden = !item.dataset.search.toLocaleLowerCase().includes(query) || Boolean(selected && item.dataset.group !== selected);
            if (!item.hidden) count++;
        }
        for (const commit of commits) {
            const matching = [...commit.querySelectorAll("[data-filter-item]")].some((item) => !item.hidden);
            commit.hidden = Boolean(query || selected) && !matching;
            const details = commit.querySelector("details");
            details.open = Boolean(query || selected) && matching;
        }
        form.querySelector("[role=status]").textContent = `${count} ${form.dataset.patchFilter === "releases" ? "releases" : "file changes"} shown`;
        form.querySelector(".patch-no-match").hidden = count !== 0 || items.length === 0;
        if (updateUrl) {
            const url = new URL(location.href);
            search.value ? url.searchParams.set("q", search.value) : url.searchParams.delete("q");
            selected ? url.searchParams.set("group", selected) : url.searchParams.delete("group");
            history.replaceState(null, "", url);
        }
    }
    form.hidden = false;
    form.addEventListener("submit", (event) => event.preventDefault());
    form.addEventListener("input", () => filter());
    form.addEventListener("reset", () => {
        search.value = "";
        if (group) group.value = "";
        filter();
    });
    filter(false);
}
