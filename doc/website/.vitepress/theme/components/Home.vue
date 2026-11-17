<script setup lang="ts">
import { withBase } from "vitepress";
import { onBeforeUnmount, onMounted, ref } from "vue";
import type { AnimationItem } from "lottie-web";
import rootsLogo from "../../../content/src/qt/res/src/bitcoinroots-logo.svg";

const logoHost = ref<HTMLDivElement>();
const logoReady = ref(false);
let player: AnimationItem | undefined;
let motion: MediaQueryList | undefined;

function showStaticLogo() {
  logoReady.value = false;
  player?.destroy();
  player = undefined;
}

onMounted(async () => {
  if (!window.matchMedia || !window.SVGSVGElement) return;
  motion = window.matchMedia("(prefers-reduced-motion: reduce)");
  if (motion.matches) return;
  motion.addEventListener?.("change", showStaticLogo);
  try {
    const { default: lottie } = await import("lottie-web/build/player/lottie_light");
    if (!logoHost.value || motion.matches) return;
    player = lottie.loadAnimation({
      container: logoHost.value,
      renderer: "svg",
      path: withBase("/bitcoin-roots.lottie.json"),
      loop: false,
      autoplay: false,
    });
    player.addEventListener("DOMLoaded", () => {
      logoReady.value = true;
      player?.play();
    });
    player.addEventListener("data_failed", showStaticLogo);
    player.addEventListener("error", showStaticLogo);
  } catch {
    showStaticLogo();
  }
});

onBeforeUnmount(() => {
  motion?.removeEventListener?.("change", showStaticLogo);
  showStaticLogo();
});
</script>

<template>
  <main class="roots-home" id="main-content">
    <section class="roots-signal" aria-labelledby="roots-title">
      <div class="roots-signal__message">
        <p class="roots-signal__provenance">The Plan ₿ Foundation presents</p>
        <h1 id="roots-title">Run your own Bitcoin node</h1>
        <div class="roots-signal__introduction">
          <p>With Bitcoin Roots, your computer checks Bitcoin payments and blocks for itself.
            It follows Bitcoin Core’s rules for valid blocks and gives you more say over
            which unconfirmed transactions you share with other nodes.</p>
        </div>
        <div class="roots-actions">
          <a class="roots-action roots-action--primary" :href="withBase('/getting-started')">
            Get started
            <svg viewBox="0 0 20 20" aria-hidden="true"><path d="M4 10h11m-4-4 4 4-4 4" /></svg>
          </a>
          <a
            class="roots-action roots-action--quiet"
            :href="withBase('/compare')"
          >
            Compare with Core
          </a>
        </div>
      </div>

      <figure class="roots-signal__identity">
        <div class="roots-signal__mark" aria-hidden="true">
          <div ref="logoHost" class="roots-signal__player" :class="{ 'is-ready': logoReady }"></div>
          <img v-show="!logoReady" class="roots-signal__fallback" :src="rootsLogo" alt="" width="512" height="512">
        </div>
        <figcaption>Connected to the trunk.<br>Conservative in policy.<br>Neutral in consensus.</figcaption>
      </figure>
    </section>

    <section class="roots-library" aria-labelledby="library-title">
      <header>
        <h2 id="library-title">Running Roots at home</h2>
        <p>Start with the desktop app. You can choose how much disk space to use and let your node catch up while you do other things.</p>
      </header>
      <nav class="roots-library__routes" aria-label="Node guides">
        <a :href="withBase('/getting-started#choose-a-drive-and-a-storage-limit')">
          <strong>Choose how many blocks to keep</strong>
          <span>See how pruning works and what to expect on the first download.</span>
        </a>
        <a :href="withBase('/principles')">
          <strong>Understand relay policy</strong>
          <span>Follow a transaction from your node’s mempool into a block.</span>
        </a>
        <a :href="withBase('/privacy')">
          <strong>Protect your privacy</strong>
          <span>Learn what peers can see about your connection and your payments.</span>
        </a>
      </nav>
      <a class="roots-library__all" :href="withBase('/documentation')">Browse all guides</a>
    </section>
  </main>
</template>
