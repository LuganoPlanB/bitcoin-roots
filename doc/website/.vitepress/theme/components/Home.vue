<script setup lang="ts">
import { onBeforeUnmount, onMounted, ref } from "vue";
import { withBase } from "vitepress";
import rootsLogo from "../../../content/src/qt/res/src/bitcoinroots-logo.svg";

const signalStages = [
  {
    label: "Peer-to-peer network",
    title: "Receive the signal",
    copy: "Download blocks and transactions from the Bitcoin peer-to-peer network.",
  },
  {
    label: "Independent validation",
    title: "Verify for yourself",
    copy: "Fully validate the blocks and transactions your node receives.",
  },
  {
    label: "Local node policy",
    title: "Choose what you relay",
    copy: "Use conservative, configurable transaction relay and mempool policy.",
  },
  {
    label: "Your node",
    title: "Stay consensus-neutral",
    copy: "Remain compatible with Bitcoin Core consensus without enforcing RDTS/BIP110 rules.",
  },
] as const;

const activeStage = ref(0);
let cycleTimer: ReturnType<typeof setInterval> | undefined;

function chooseStage(index: number) {
  activeStage.value = index;
  if (cycleTimer) window.clearInterval(cycleTimer);
}

onMounted(() => {
  if (!window.matchMedia("(prefers-reduced-motion: reduce)").matches) {
    cycleTimer = window.setInterval(() => {
      activeStage.value = (activeStage.value + 1) % signalStages.length;
    }, 4200);
  }
});

onBeforeUnmount(() => {
  if (cycleTimer) window.clearInterval(cycleTimer);
});
</script>

<template>
  <main class="roots-home" id="main-content">
    <section class="roots-signal" aria-labelledby="roots-title">
      <div class="roots-signal__message">
        <img class="roots-signal__logo" :src="rootsLogo" alt="Bitcoin Roots">
        <h1 id="roots-title">Your signal.<br>Your rules.<br>Your node.</h1>
        <p>
          Connected to the trunk. Conservative in policy. Neutral in consensus.
        </p>
        <div class="roots-actions">
          <a class="roots-action roots-action--primary" :href="withBase('/getting-started')">
            Get started
            <svg viewBox="0 0 20 20" aria-hidden="true"><path d="M4 10h11m-4-4 4 4-4 4" /></svg>
          </a>
          <a class="roots-action roots-action--quiet" :href="withBase('/documentation')">
            Explore all docs
          </a>
        </div>
      </div>

      <div class="roots-signal__instrument" aria-label="From Bitcoin network to your node">
        <div class="roots-signal__mast" aria-hidden="true">
          <span class="roots-signal__pulse"></span>
        </div>
        <ol class="roots-signal__stages">
          <li v-for="(stage, index) in signalStages" :key="stage.label">
            <button
              type="button"
              :class="{ 'is-active': activeStage === index }"
              :aria-pressed="activeStage === index"
              @click="chooseStage(index)"
              @focus="chooseStage(index)"
            >
              <span class="roots-signal__marker" aria-hidden="true"></span>
              <span>
                <small>{{ stage.label }}</small>
                <strong>{{ stage.title }}</strong>
              </span>
            </button>
          </li>
        </ol>
        <div class="roots-signal__readout" aria-live="polite">
          <span>0{{ activeStage + 1 }} / 04</span>
          <p>{{ signalStages[activeStage].copy }}</p>
        </div>
      </div>
    </section>

    <section class="roots-boundary" aria-labelledby="boundary-title">
      <div class="roots-section-heading">
        <h2 id="boundary-title">Know exactly where policy ends.</h2>
        <p>
          Bitcoin Roots distinguishes the rules that keep the network in agreement
          from the choices your node makes about unconfirmed transactions.
        </p>
      </div>
      <div class="roots-boundary__map">
        <article>
          <span>Shared boundary</span>
          <h3>Consensus</h3>
          <p>Bitcoin Core-compatible consensus keeps valid historical and incoming blocks acceptable.</p>
        </article>
        <div class="roots-boundary__connector" aria-hidden="true">
          <span></span>
        </div>
        <article>
          <span>Operator boundary</span>
          <h3>Policy</h3>
          <p>Conservative, configurable relay and mempool choices govern what your node accepts before confirmation.</p>
        </article>
      </div>
      <a class="roots-text-link" :href="withBase('/doc/policy/README')">
        Read the transaction relay policy
        <svg viewBox="0 0 20 20" aria-hidden="true"><path d="M4 10h11m-4-4 4 4-4 4" /></svg>
      </a>
    </section>

    <section class="roots-start" aria-labelledby="start-title">
      <div class="roots-start__lead">
        <h2 id="start-title">From checkout to a node you understand.</h2>
        <p>
          Follow the platform guide, make resource choices deliberately, and keep
          the complete reference close when you need it.
        </p>
        <a class="roots-action roots-action--primary" :href="withBase('/getting-started')">
          Choose your platform
          <svg viewBox="0 0 20 20" aria-hidden="true"><path d="M4 10h11m-4-4 4 4-4 4" /></svg>
        </a>
      </div>
      <ol class="roots-start__path">
        <li>
          <span>Build</span>
          <div><strong>Choose your system</strong><p>Use the repository guide for Linux, macOS, Windows, or BSD.</p></div>
        </li>
        <li>
          <span>Configure</span>
          <div><strong>Set explicit limits</strong><p>Review configuration, memory, and traffic guidance before changing defaults.</p></div>
        </li>
        <li>
          <span>Operate</span>
          <div><strong>Keep verifying</strong><p>Run a full node that validates the chain and applies your local policy.</p></div>
        </li>
      </ol>
    </section>

    <section class="roots-library" aria-labelledby="library-title">
      <header>
        <h2 id="library-title">The source is the documentation.</h2>
        <p>
          Every Markdown guide remains where maintainers wrote it. The website adds
          navigation and search—not a second, drifting copy.
        </p>
      </header>
      <nav class="roots-library__routes" aria-label="Documentation highlights">
        <a :href="withBase('/doc/bitcoin-conf')"><strong>Configuration</strong><span>Options and file format</span></a>
        <a :href="withBase('/doc/managing-wallets')"><strong>Wallets</strong><span>Manage loaded wallets</span></a>
        <a :href="withBase('/doc/JSON-RPC-interface')"><strong>JSON-RPC</strong><span>Control your node</span></a>
        <a :href="withBase('/doc/reduce-traffic')"><strong>Network use</strong><span>Reduce node traffic</span></a>
        <a :href="withBase('/doc/design/libraries')"><strong>Architecture</strong><span>Understand boundaries</span></a>
        <a :href="withBase('/CONTRIBUTING')"><strong>Contribute</strong><span>Join development</span></a>
      </nav>
      <a class="roots-library__all" :href="withBase('/documentation')">
        Browse the complete documentation atlas
        <svg viewBox="0 0 20 20" aria-hidden="true"><path d="M4 10h11m-4-4 4 4-4 4" /></svg>
      </a>
    </section>

    <section class="roots-close" aria-labelledby="close-title">
      <img :src="rootsLogo" alt="" aria-hidden="true">
      <div>
        <h2 id="close-title">Run the node. Read the source. Keep the choice yours.</h2>
        <p>Bitcoin Roots is released under the terms of the MIT license.</p>
      </div>
      <a class="roots-action roots-action--light" :href="withBase('/getting-started')">
        Get started
        <svg viewBox="0 0 20 20" aria-hidden="true"><path d="M4 10h11m-4-4 4 4-4 4" /></svg>
      </a>
    </section>
  </main>
</template>
