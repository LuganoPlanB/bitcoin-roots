// Match VitePress before the first paint. The shared VitePress header owns
// appearance controls, system changes and storage synchronization after hydration.
(() => {
    let preference = "auto";
    try { preference = localStorage.getItem("vitepress-theme-appearance") ?? "auto"; } catch { /* Storage may be disabled. */ }
    const dark = preference === "dark" || (preference !== "light" && matchMedia("(prefers-color-scheme: dark)").matches);
    document.documentElement.classList.toggle("dark", dark);
})();
