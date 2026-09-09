<script setup lang="ts">
const rows = [
  {
    question: "Primary role",
    core: "Reference Bitcoin full-node implementation",
    roots: "Bitcoin Core-compatible full node with selected conservative Bitcoin Knots policy controls",
    knots: "Enhanced node and wallet software with additional policy controls and, in this release, independent consensus rules",
  },
  {
    question: "Source lineage",
    core: "Primary upstream",
    roots: "Based on Bitcoin Core, with selected features retained from the Bitcoin Knots lineage",
    knots: "Derived from Bitcoin Core",
  },
  {
    question: "Bitcoin network and chain",
    core: "Bitcoin",
    roots: "Bitcoin",
    knots: "Uses a BLAKE2b proof-of-work chain after its 2026 hard fork, which is not the Bitcoin Core-compatible chain",
  },
  {
    question: "Consensus compatibility with current Bitcoin Core",
    core: "Native reference",
    roots: "Yes. This is a project release requirement.",
    knots: "No for v29.4.1.knots20260508",
  },
  {
    question: "RDTS/BIP-110 enforcement",
    core: "No",
    roots: "No",
    knots: "Yes. The reviewed release activates RDTS at its proof-of-work change.",
  },
  {
    question: "Proof-of-work rules",
    core: "Bitcoin SHA-256d proof of work",
    roots: "Same as Bitcoin Core",
    knots: "BLAKE2b after the release’s hard-fork activation",
  },
  {
    question: "Transaction relay policy",
    core: "Bitcoin Core defaults and configuration",
    roots: "Conservative and configurable",
    knots: "Additional configurable filters; rejecttokens is enabled by default in the reviewed release",
  },
  {
    question: "Mempool policy",
    core: "Bitcoin Core policy",
    roots: "Conservative and configurable",
    knots: "Bitcoin Core-derived policy with additional Bitcoin Knots options",
  },
  {
    question: "Resource controls",
    core: "Standard Bitcoin Core controls",
    roots: "Bitcoin Core controls plus selected retained controls",
    knots: "Bitcoin Core-derived controls plus additional Bitcoin Knots options",
  },
  {
    question: "Project-specific consensus changes",
    core: "None relative to itself",
    roots: "None intended relative to Bitcoin Core",
    knots: "The reviewed release includes BLAKE2b proof of work, RDTS activation, an 800 kWU temporary block-weight limit, and other consensus changes",
  },
  {
    question: "Relationship to Bitcoin Core releases",
    core: "Upstream",
    roots: "Suitable changes are reviewed and incorporated per release",
    knots: "Derived from Bitcoin Core, with an independent release line and project-specific changes",
  },
  {
    question: "New coin or token",
    core: "No",
    roots: "No",
    knots: "No new token is described, but the reviewed release follows a consensus-incompatible proof-of-work chain",
  },
] as const;
</script>

<template>
  <div
    class="roots-comparison-table"
    role="region"
    aria-label="Comparison of Bitcoin Core, Bitcoin Roots, and Bitcoin Knots"
    tabindex="0"
  >
    <p class="roots-comparison-table__hint">Scroll horizontally to compare all three projects.</p>
    <table>
      <thead>
        <tr>
          <th scope="col">Question</th>
          <th scope="col">
            <a href="https://github.com/bitcoin/bitcoin/releases/tag/v31.1">Bitcoin Core</a>
          </th>
          <th scope="col">
            <a href="https://github.com/LuganoPlanB/bitcoin-roots/commit/d18c044e6a30da77ef5bee13423b6e5db6e6bc01">Bitcoin Roots</a>
          </th>
          <th scope="col">
            <a href="https://github.com/bitcoinknots/bitcoin/releases/tag/v29.4.1.knots20260508">Bitcoin Knots</a>
          </th>
        </tr>
      </thead>
      <tbody>
        <tr v-for="row in rows" :key="row.question">
          <th scope="row">{{ row.question }}</th>
          <td>{{ row.core }}</td>
          <td>{{ row.roots }}</td>
          <td>{{ row.knots }}</td>
        </tr>
      </tbody>
    </table>
  </div>
</template>
