<script setup lang="ts">
import { withBase } from "vitepress";
import rootsLogo from "../../../content/src/qt/res/src/bitcoinroots-logo.svg";
</script>

<template>
  <main class="roots-home" id="main-content">
    <section class="roots-signal" aria-labelledby="roots-title">
      <div class="roots-signal__message">
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

      <div class="roots-signal__editorial">
        <p class="roots-signal__provenance">THE PLAN ₿ FOUNDATION PRESENTS</p>
        <div class="roots-signal__identity">
          <img :src="rootsLogo" alt="" aria-hidden="true">
          <p>Connected to the trunk. Conservative in policy. Neutral in consensus.</p>
        </div>
      </div>
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
