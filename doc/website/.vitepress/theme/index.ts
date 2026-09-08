import DefaultTheme from "vitepress/theme";
import "lugano-planb-vite-theme/theme.css";
import "./custom.css";

import DocumentationIndex from "./components/DocumentationIndex.vue";
import Home from "./components/Home.vue";

export default {
  extends: DefaultTheme,
  enhanceApp({ app }) {
    app.component("DocumentationIndex", DocumentationIndex);
    app.component("Home", Home);
  },
};
