// Share VitePress's appearance preference across full-document navigation.
// This small head script applies the palette before styles paint.
(() => {
    const key = "vitepress-theme-appearance";
    const system = matchMedia("(prefers-color-scheme: dark)");
    let preference = "auto";
    try { preference = localStorage.getItem(key) ?? "auto"; } catch { /* Storage may be disabled. */ }
    const valid = (value) => ["light", "dark", "auto"].includes(value) ? value : "auto";
    function apply(value) {
        preference = valid(value);
        const dark = preference === "dark" || (preference === "auto" && system.matches);
        document.documentElement.classList.toggle("dark", dark);
        document.documentElement.dataset.theme = dark ? "dark" : "light";
        const control = document.querySelector("[data-patch-appearance]");
        if (control) control.value = preference;
    }
    apply(preference);
    system.addEventListener("change", () => apply(preference));
    addEventListener("storage", (event) => { if (event.key === key) apply(event.newValue); });
    addEventListener("DOMContentLoaded", () => {
        const control = document.querySelector("[data-patch-appearance]");
        if (!control) return;
        control.value = preference;
        control.closest("label").hidden = false;
        control.addEventListener("change", () => {
            apply(control.value);
            try { localStorage.setItem(key, preference); } catch { /* Keep the current document usable. */ }
        });
    });
})();
