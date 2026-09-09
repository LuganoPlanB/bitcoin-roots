<script setup lang="ts">
import { onBeforeUnmount, onMounted, ref } from "vue";
import { withBase } from "vitepress";
import rootsLogo from "../../../content/src/qt/res/src/bitcoinroots-logo.svg";

const signalStages = [
  {
    label: "Peer-to-peer network",
    title: "Receive from peers",
  },
  {
    label: "Independent validation",
    title: "Verify every block",
  },
  {
    label: "Local node policy",
    title: "Choose what you relay",
  },
  {
    label: "Your node",
    title: "Stay consensus-neutral",
  },
] as const;

const lottieSection = ref<HTMLElement>();
const lottieHost = ref<HTMLElement>();
const lottieReady = ref(false);
let lottieObserver: IntersectionObserver | undefined;
let lottieAnimation: { destroy: () => void } | undefined;

async function playRootsMark() {
  if (!lottieHost.value) return;

  try {
    const { default: lottie } = await import("lottie-web/build/player/lottie_light");
    if (!lottieHost.value) return;

    const animation = lottie.loadAnimation({
      container: lottieHost.value,
      renderer: "svg",
      loop: false,
      autoplay: true,
      path: withBase("/bitcoin-roots.lottie.json"),
      rendererSettings: {
        preserveAspectRatio: "xMidYMid meet",
        progressiveLoad: true,
      },
    });

    animation.addEventListener("DOMLoaded", () => {
      lottieReady.value = true;
    });
    lottieAnimation = animation;
  } catch {
    lottieReady.value = false;
  }
}

onMounted(() => {
  if (
    !lottieSection.value
    || window.matchMedia("(prefers-reduced-motion: reduce)").matches
  ) {
    return;
  }

  lottieObserver = new IntersectionObserver((entries) => {
    if (!entries.some((entry) => entry.isIntersecting)) return;
    lottieObserver?.disconnect();
    void playRootsMark();
  }, { rootMargin: "0px 0px -12%", threshold: 0.3 });

  lottieObserver.observe(lottieSection.value);
});

onBeforeUnmount(() => {
  lottieObserver?.disconnect();
  lottieAnimation?.destroy();
});
</script>

<template>
  <main class="roots-home" id="main-content">
    <section class="roots-signal" aria-labelledby="roots-title">
      <div class="roots-signal__message">
        <!-- <img class="roots-signal__logo" :src="rootsLogo" alt="Bitcoin Roots"> -->
        <h1 id="roots-title">Your node.<br>No spam.<br>Just Bitcoin.</h1>
        <p>
          We are a non-rdts/bip110 fork of Knots, forever faithful to Bitcoin Core, with configurable node policy.
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

      <div class="roots-signal__editorial">
        <p>The Plan-₿ foundation presents</p>
        <p>
        <img  :src="rootsLogo" alt="Bitcoin Roots">
    Bitcoin Roots.<br/>
          Built by OGs who keep the nodes running.
        </p>
      </div>

      <ol class="roots-signal__sequence" aria-label="From the peer-to-peer network to your node">
        <li v-for="(stage, index) in signalStages" :key="stage.label">
          <span>0{{ index + 1 }}</span>
          <div>
            <small>{{ stage.label }}</small>
            <strong>{{ stage.title }}</strong>
          </div>
        </li>
      </ol>
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

    <section ref="lottieSection" class="roots-mark" aria-labelledby="roots-mark-title">
      <div class="roots-mark__visual" :class="{ 'is-ready': lottieReady }" aria-hidden="true">
        <img :src="rootsLogo" alt="">
        <div ref="lottieHost" class="roots-mark__lottie"></div>
      </div>
      <div class="roots-mark__copy">
        <h2 id="roots-mark-title">Bitcoin rises from its roots.</h2>
        <p>
          Receive from peers. Verify every block. Apply your own policy to
          unconfirmed transactions while remaining compatible with consensus.
        </p>
      </div>
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
