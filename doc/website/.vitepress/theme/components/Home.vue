<script setup lang="ts">
import { withBase } from "vitepress";
import rootsLogo from "../../../content/src/qt/res/src/bitcoinroots-logo.svg";

const validationStages = [
  {
    title: "Connect",
    copy: "Join Bitcoin’s peer-to-peer network and receive blocks and transactions directly from other nodes.",
  },
  {
    title: "Verify",
    copy: "Independently validate every block according to Bitcoin consensus rules.",
  },
  {
    title: "Configure",
    copy: "Choose the relay, mempool, privacy, and resource policies applied by your node.",
  },
  {
    title: "Stay compatible",
    copy: "Follow the Bitcoin chain while keeping local policy under your control.",
  },
] as const;
</script>

<template>
  <main class="roots-home" id="main-content">
    <section class="roots-signal" aria-labelledby="roots-title">
      <div class="roots-signal__message">
        <h1 id="roots-title">Your node. Your policy. Bitcoin consensus.</h1>
        <div class="roots-signal__introduction">
          <p>
            Bitcoin Roots is a Bitcoin Core-based full node that preserves selected
            conservative policy and operator controls from Bitcoin Knots.
          </p>
          <p>
            It follows Bitcoin Core-compatible consensus and does not enforce
            RDTS/BIP-110.
          </p>
        </div>
        <div class="roots-actions">
          <a class="roots-action roots-action--primary" :href="withBase('/getting-started')">
            Get started
            <svg viewBox="0 0 20 20" aria-hidden="true"><path d="M4 10h11m-4-4 4 4-4 4" /></svg>
          </a>
          <a
            class="roots-action roots-action--quiet"
            href="https://github.com/LuganoPlanB/bitcoin-roots"
          >
            View on GitHub
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

    <section class="roots-validation" aria-labelledby="validation-title">
      <div class="roots-section-heading">
        <h2 id="validation-title">Independently verify Bitcoin</h2>
        <p>
          Your node receives the network’s data, checks it independently, and
          applies your choices before confirmation.
        </p>
      </div>
      <ol class="roots-validation__steps">
        <li v-for="(stage, index) in validationStages" :key="stage.title">
          <span>0{{ index + 1 }}</span>
          <div>
            <h3>{{ stage.title }}</h3>
            <p>{{ stage.copy }}</p>
          </div>
        </li>
      </ol>
    </section>

    <section class="roots-lineage" aria-labelledby="lineage-title">
      <img :src="rootsLogo" alt="" aria-hidden="true">
      <div>
        <h2 id="lineage-title">From Knots, without leaving Bitcoin</h2>
        <p>
          Bitcoin Roots preserves selected policy features and operator controls
          from Bitcoin Knots while remaining compatible with Bitcoin Core consensus.
        </p>
        <p>
          The repository records <code>29.3.knots20260507</code> as its Knots
          lineage reference, not as an exact Git parent. Bitcoin Roots does not
          adopt RDTS/BIP-110 consensus rules.
        </p>
        <p>
          Bitcoin Roots is a source-code fork. It is not a separate cryptocurrency,
          proof-of-work network, or competing Bitcoin chain.
        </p>
        <a class="roots-text-link roots-text-link--light" :href="withBase('/principles')">
          Read our principles
          <svg viewBox="0 0 20 20" aria-hidden="true"><path d="M4 10h11m-4-4 4 4-4 4" /></svg>
        </a>
      </div>
    </section>

    <section class="roots-boundary" aria-labelledby="boundary-title">
      <div class="roots-section-heading">
        <h2 id="boundary-title">Know exactly where policy ends</h2>
        <p>
          Bitcoin Roots separates the shared rules that keep Bitcoin nodes in
          agreement from the local choices each operator makes about unconfirmed
          transactions.
        </p>
      </div>
      <div class="roots-boundary__map">
        <article>
          <span>Shared boundary</span>
          <h3>Consensus is shared</h3>
          <p>
            Bitcoin Roots applies Bitcoin Core-compatible block-validity rules and
            follows the Bitcoin chain.
          </p>
          <p>
            A transaction rejected by local mempool or relay policy may still be
            valid under Bitcoin consensus. If a valid block contains that
            transaction, Bitcoin Roots accepts the block.
          </p>
        </article>
        <div class="roots-boundary__connector" aria-hidden="true">
          <span></span>
        </div>
        <article>
          <span>Operator boundary</span>
          <h3>Policy is local</h3>
          <p>
            Your node decides which unconfirmed transactions it accepts into its
            mempool and relays to peers.
          </p>
          <p>
            Bitcoin Roots provides conservative, configurable policy options
            without turning those preferences into new consensus rules.
          </p>
        </article>
      </div>
      <a class="roots-text-link" :href="withBase('/doc/policy/README')">
        Read the transaction relay policy
        <svg viewBox="0 0 20 20" aria-hidden="true"><path d="M4 10h11m-4-4 4 4-4 4" /></svg>
      </a>
    </section>

    <section class="roots-why" aria-labelledby="why-title">
      <div class="roots-why__lead">
        <h2 id="why-title">Why Bitcoin Roots?</h2>
        <p>Bitcoin Core is the reference implementation and the upstream development trunk.</p>
        <p>
          Bitcoin Roots builds on that foundation while preserving selected controls
          for operators who want stricter transaction relay, explicit resource
          limits, and greater visibility into node behaviour.
        </p>
        <p>
          Suitable Bitcoin Core developments are reviewed and incorporated into
          Bitcoin Roots releases. Project-specific differences should remain
          limited, documented, and testable.
        </p>
        <a class="roots-text-link" :href="withBase('/compare')">
          Compare Bitcoin Roots
          <svg viewBox="0 0 20 20" aria-hidden="true"><path d="M4 10h11m-4-4 4 4-4 4" /></svg>
        </a>
      </div>
      <div class="roots-why__list">
        <p>Bitcoin Roots is intended for operators who want:</p>
        <ul>
          <li>Bitcoin Core-compatible consensus</li>
          <li>Conservative, configurable mempool policy</li>
          <li>Explicit resource and network controls</li>
          <li>Independent validation</li>
          <li>Transparent differences from upstream</li>
          <li>No RDTS/BIP-110 consensus enforcement</li>
        </ul>
      </div>
    </section>

    <section class="roots-start" aria-labelledby="start-title">
      <div class="roots-start__lead">
        <h2 id="start-title">From source to a running node</h2>
        <p>
          Follow the platform guide, choose resource limits deliberately, and keep
          the complete reference available when you need it.
        </p>
        <a class="roots-action roots-action--primary" :href="withBase('/getting-started')">
          Choose your platform
          <svg viewBox="0 0 20 20" aria-hidden="true"><path d="M4 10h11m-4-4 4 4-4 4" /></svg>
        </a>
      </div>
      <ol class="roots-start__path">
        <li>
          <span>1. Build</span>
          <div>
            <strong>Choose your system</strong>
            <p>Follow the repository instructions for Linux, macOS, Windows, or BSD.</p>
          </div>
        </li>
        <li>
          <span>2. Configure</span>
          <div>
            <strong>Set deliberate limits</strong>
            <p>
              Review the available memory, storage, network, privacy, relay, and
              mempool settings before changing the defaults.
            </p>
          </div>
        </li>
        <li>
          <span>3. Verify</span>
          <div>
            <strong>Run your node</strong>
            <p>
              Connect to the Bitcoin network, independently validate the chain, and
              apply your chosen policy to unconfirmed transactions.
            </p>
          </div>
        </li>
      </ol>
    </section>

    <section class="roots-library" aria-labelledby="library-title">
      <header>
        <h2 id="library-title">Documentation stays with the source</h2>
        <p>
          The website renders the Markdown documentation maintained in the
          repository. The code, documentation, and review history can therefore
          evolve together.
        </p>
      </header>
      <nav class="roots-library__routes" aria-label="Documentation highlights">
        <a :href="withBase('/doc/bitcoin-conf')">
          <strong>Configuration</strong>
          <span>Review available options and the configuration-file format.</span>
        </a>
        <a :href="withBase('/doc/managing-wallets')">
          <strong>Wallets</strong>
          <span>Create, load, and manage wallets.</span>
        </a>
        <a :href="withBase('/doc/JSON-RPC-interface')">
          <strong>JSON-RPC</strong>
          <span>Inspect and control your node programmatically.</span>
        </a>
        <a :href="withBase('/doc/reduce-traffic')">
          <strong>Network use</strong>
          <span>Understand and reduce bandwidth consumption.</span>
        </a>
        <a :href="withBase('/doc/design/libraries')">
          <strong>Architecture</strong>
          <span>Learn how validation, networking, policy, and other components are separated.</span>
        </a>
        <a :href="withBase('/CONTRIBUTING')">
          <strong>Contribute</strong>
          <span>Review the development process, test changes, and contribute code or documentation.</span>
        </a>
      </nav>
      <a class="roots-library__all" :href="withBase('/documentation')">
        Browse all documentation
        <svg viewBox="0 0 20 20" aria-hidden="true"><path d="M4 10h11m-4-4 4 4-4 4" /></svg>
      </a>
    </section>

    <section class="roots-development" aria-labelledby="development-title">
      <div class="roots-section-heading">
        <h2 id="development-title">Open development for security-critical software</h2>
        <div>
          <p>
            Bitcoin Roots is developed in public. Changes can be inspected, tested,
            and discussed through the project repository.
          </p>
          <p>
            Consensus compatibility is a release requirement. Differences in relay,
            mempool, privacy, and resource policy should be explicit and documented.
          </p>
          <p>
            Testing and code review are essential. Contributions from node operators,
            reviewers, developers, and documentation writers are welcome.
          </p>
          <div class="roots-actions">
            <a
              class="roots-action roots-action--primary"
              href="https://github.com/LuganoPlanB/bitcoin-roots"
            >
              View the source
              <svg viewBox="0 0 20 20" aria-hidden="true"><path d="M4 10h11m-4-4 4 4-4 4" /></svg>
            </a>
            <a class="roots-action roots-action--quiet" :href="withBase('/CONTRIBUTING')">
              Contribute
            </a>
          </div>
        </div>
      </div>
    </section>

    <section class="roots-close" aria-labelledby="close-title">
      <img :src="rootsLogo" alt="" aria-hidden="true">
      <div>
        <h2 id="close-title">Run the node. Read the diff. Keep the choice yours.</h2>
        <p>
          Bitcoin Roots gives node operators conservative policy controls without
          asking them to leave Bitcoin consensus.
        </p>
      </div>
      <div class="roots-actions">
        <a class="roots-action roots-action--light" :href="withBase('/getting-started')">
          Get started
          <svg viewBox="0 0 20 20" aria-hidden="true"><path d="M4 10h11m-4-4 4 4-4 4" /></svg>
        </a>
        <a
          class="roots-action roots-action--light-quiet"
          href="https://github.com/LuganoPlanB/bitcoin-roots"
        >
          View on GitHub
        </a>
      </div>
    </section>
  </main>
</template>
