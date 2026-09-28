<script setup lang="ts">
const sections = [
  {
    title: "Project and consensus",
    rows: [
      {
        feature: "Role and lineage",
        roots: "Bitcoin Core v29.4 plus a reviewable Roots patch carrying selected Knots-lineage behavior",
        knots: "A separate Bitcoin Core-derived implementation with a broader feature set",
        core: "The upstream reference implementation and the direct Roots base",
      },
      {
        feature: "Bitcoin Core-compatible consensus",
        roots: "Yes. Required for every Roots release.",
        knots: "Yes in the reviewed 29.3 lineage; no in v29.4.1.knots20260508",
        core: "Yes. Reference behavior.",
      },
      {
        feature: "RDTS / BIP-110 enforcement",
        roots: "Excluded",
        knots: "Not enforced by the reviewed 29.3 lineage; enforced by v29.4.1",
        core: "Not included",
      },
      {
        feature: "Project-specific consensus changes",
        roots: "Excluded",
        knots: "Present in v29.4.1, including BLAKE2b proof-of-work and a temporary 800 kWU limit",
        core: "Not included",
      },
    ],
  },
  {
    title: "Transaction and mining policy",
    rows: [
      {
        feature: "Embedded-data relay controls (-datacarrier, -datacarriersize)",
        roots: "Included",
        knots: "Included",
        core: "Included",
      },
      {
        feature: "Configurable embedded-data cost (-datacarriercost)",
        roots: "Included from the Knots lineage",
        knots: "Included",
        core: "Not included in v29.4",
      },
      {
        feature: "Extended replacement policy modes (-mempoolreplacement)",
        roots: "Included from the Knots lineage",
        knots: "Included",
        core: "Standard Core replacement policy; no equivalent mode selector",
      },
      {
        feature: "Coin-age and maturity relay filters (-minrelaycoinblocks, -minrelaymaturity)",
        roots: "Included from the Knots lineage",
        knots: "Included",
        core: "Not included in v29.4",
      },
      {
        feature: "Sub-dust effective-fee penalty (-subdustfeepenalty)",
        roots: "Included from the Knots lineage",
        knots: "Included",
        core: "Not included in v29.4",
      },
      {
        feature: "Bare multisig policy toggle (-permitbaremultisig)",
        roots: "Included",
        knots: "Included",
        core: "Included",
      },
      {
        feature: "Mining weight and minimum-fee controls (-blockmaxweight, -blockmintxfee)",
        roots: "Included",
        knots: "Included",
        core: "Included",
      },
      {
        feature: "Coin-age priority mining reserve (-blockprioritysize)",
        roots: "Included from the Knots lineage",
        knots: "Included",
        core: "Not included in v29.4",
      },
    ],
  },
  {
    title: "Network resources and operator tools",
    rows: [
      {
        feature: "Compact-block extra-transaction count bound (-blockreconstructionextratxn)",
        roots: "Included",
        knots: "Included",
        core: "Included",
      },
      {
        feature: "Compact-block reconstruction memory bound (-blockreconstructionextratxnsize)",
        roots: "Included from the Knots lineage",
        knots: "Included",
        core: "Not included in v29.4",
      },
      {
        feature: "Orphan-transaction cap (-maxorphantx)",
        roots: "Included",
        knots: "Included",
        core: "Included",
      },
      {
        feature: "Upload target and peer permissions (-maxuploadtarget, -whitebind, -whitelist)",
        roots: "Included",
        knots: "Included",
        core: "Included",
      },
      {
        feature: "Collected mempool samples (getmempoolstats RPC)",
        roots: "Included from the Knots lineage",
        knots: "Included",
        core: "Not included in v29.4",
      },
      {
        feature: "Monotonic process-uptime reporting",
        roots: "Included from the Knots lineage",
        knots: "Included",
        core: "The uptime RPC exists; v29.4 does not use the retained monotonic uptime source",
      },
      {
        feature: "RAM detection, memory-pressure response, and advisory I/O priority",
        roots: "Selected safeguards retained from the Knots lineage",
        knots: "Included",
        core: "No equivalent combined facility in v29.4",
      },
      {
        feature: "GUI peer-health view",
        roots: "Bounded live peer table and details; expanded and hardened in v29.4-roots.2",
        knots: "Included, with additional Knots monitoring surfaces",
        core: "Standard peer table and details",
      },
    ],
  },
  {
    title: "Wallet and desktop",
    rows: [
      {
        feature: "Descriptor wallet and basic GUI coin control",
        roots: "Included through the Core base",
        knots: "Included",
        core: "Included",
      },
      {
        feature: "Per-send Replace-By-Fee choice",
        roots: "Included; follows the wallet default and confirms the transaction's effective signaling",
        knots: "Included in the send dialog",
        core: "Included in the send dialog; checked by default in v29.4",
      },
      {
        feature: "Selected wallet database and signing hardening",
        roots: "Selected Knots-lineage fixes retained without changing wallet formats",
        knots: "Broader Knots wallet maintenance",
        core: "Core v29.4 wallet behavior",
      },
      {
        feature: "Private-key sweeping",
        roots: "Excluded from v29.4-roots.2; future work only",
        knots: "Included as sweepprivkeys RPC and a GUI dialog",
        core: "Not included in v29.4",
      },
      {
        feature: "Advanced privacy-aware coin control",
        roots: "Not shipped. Planned work spans wallet-side data and transaction contracts plus the Qt workflow.",
        knots: "Basic coin control plus Knots-specific wallet and GUI behavior; not the planned Roots contract",
        core: "Basic wallet coin selection and GUI coin control",
      },
      {
        feature: "QR receive-request image export",
        roots: "Included",
        knots: "Included",
        core: "Included",
      },
      {
        feature: "Windows taskbar synchronization progress",
        roots: "Included when the Windows GUI build enables taskbar progress",
        knots: "Included",
        core: "Not included in v29.4",
      },
      {
        feature: "Selected build and dependency portability",
        roots: "Qt 5 or Qt 6 selection plus retained QR, external-signer, Tor, and platform integration support",
        knots: "Included in the broader Knots build matrix",
        core: "Core v29.4 build and optional dependency support",
      },
    ],
  },
] as const;
</script>

<template>
  <div
    class="roots-comparison-table"
    role="region"
    aria-label="Feature comparison of Bitcoin Roots, Bitcoin Knots, and Bitcoin Core"
    tabindex="0"
  >
    <p class="roots-comparison-table__hint">Scroll horizontally to compare all three projects.</p>
    <table>
      <thead>
        <tr>
          <th scope="col">Feature</th>
          <th scope="col">
            <a href="https://github.com/LuganoPlanB/bitcoin-roots/releases/tag/v29.4-roots.2">Bitcoin Roots</a>
          </th>
          <th scope="col">
            <a href="https://github.com/bitcoinknots/bitcoin/releases/tag/v29.3.knots20260507">Bitcoin Knots</a>
          </th>
          <th scope="col">
            <a href="https://github.com/bitcoin/bitcoin/releases/tag/v29.4">Bitcoin Core</a>
          </th>
        </tr>
      </thead>
      <tbody v-for="section in sections" :key="section.title">
        <tr class="roots-comparison-table__section">
          <th scope="rowgroup" colspan="4">{{ section.title }}</th>
        </tr>
        <tr v-for="row in section.rows" :key="row.feature">
          <th scope="row">{{ row.feature }}</th>
          <td>{{ row.roots }}</td>
          <td>{{ row.knots }}</td>
          <td>{{ row.core }}</td>
        </tr>
      </tbody>
    </table>
  </div>
</template>
