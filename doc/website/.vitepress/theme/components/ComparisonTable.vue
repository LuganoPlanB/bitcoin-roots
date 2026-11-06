<script setup lang="ts">
const sections = [
  {
    title: "Roots-only features",
    rows: [
      {
        feature: "Advanced privacy-aware coin control",
        roots: "Included: wallet-reported effective value, reuse, grouping, locks and spendability; stale selections are cleared",
        knots: "Standard coin-control dialog; no Roots advanced wallet-facts view",
        core: "Standard coin-control dialog; no Roots advanced wallet-facts view",
      },
      {
        feature: "Address-specific spending-key export (exportaddresskey RPC)",
        roots: "Included for the selected local-key wallet, including supported multisig, Miniscript, Taproot and MuSig2 keys",
        knots: "No exportaddresskey RPC",
        core: "No exportaddresskey RPC",
      },
      {
        feature: "Explicit startup gate for key-disclosure RPCs (--allow-dangerous-rpc)",
        roots: "exportaddresskey, listdescriptors, gethdkeys and derivehdkey are disabled by default; only an explicit command-line opt-in enables them",
        knots: "No equivalent startup gate; normal wallet authorization still applies",
        core: "No equivalent startup gate; normal wallet authorization still applies",
      },
      {
        feature: "Effective local-policy reporting (getmempoolinfo.policy)",
        roots: "Included: initialized admission settings, units and enforcement gates, alongside native v32 mempool fields",
        knots: "Mempool information RPC; no Roots policy object",
        core: "Native v32 mempool information; no Roots policy object",
      },
    ],
  },
  {
    title: "Transaction and mining policy",
    rows: [
      {
        feature: "Embedded-data relay controls (-datacarrier, -datacarriersize)",
        roots: "Included; defaults to an 83-byte aggregate budget, recognized input/witness counting and one OP_RETURN output",
        knots: "Included",
        core: "Included",
      },
      {
        feature: "Configurable embedded-data cost (-datacarriercost)",
        roots: "Included from the Knots lineage",
        knots: "Included",
        core: "Not included in v32.1",
      },
      {
        feature: "Extended replacement policy modes (-mempoolreplacement)",
        roots: "Included from the Knots lineage",
        knots: "Included",
        core: "Native full-RBF policy; no -mempoolreplacement mode selector",
      },
      {
        feature: "Coin-age and maturity relay filters (-minrelaycoinblocks, -minrelaymaturity)",
        roots: "Included from the Knots lineage",
        knots: "Included",
        core: "Not included in v32.1",
      },
      {
        feature: "Sub-dust effective-fee penalty (-subdustfeepenalty)",
        roots: "Included from the Knots lineage",
        knots: "Included",
        core: "Not included in v32.1",
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
        core: "Not included in v32.1",
      },
    ],
  },
  {
    title: "Project and consensus",
    rows: [
      {
        feature: "Role and lineage",
        roots: "Bitcoin Core v32.1 plus a reviewable Roots patch carrying selected Knots-lineage behavior",
        knots: "A separate Core-derived implementation on its own 29.x release line",
        core: "The current upstream reference implementation and the direct v32.1 Roots base",
      },
      {
        feature: "Bitcoin Core-compatible consensus",
        roots: "Yes. Required for every Roots release.",
        knots: "No. The reviewed v29.4.3 release carries additional consensus rules",
        core: "Yes. Reference behavior.",
      },
      {
        feature: "RDTS / BIP-110 enforcement",
        roots: "Excluded",
        knots: "Included in the reviewed v29.4.3 release",
        core: "Not included",
      },
      {
        feature: "Project-specific consensus changes",
        roots: "Excluded",
        knots: "Includes BLAKE2b proof-of-work, RDTS and extended coinbase maturity; see the release notes",
        core: "Not included",
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
        roots: "Included: 10 decimal MB of charged transaction storage by default, alongside the count bound",
        knots: "Included",
        core: "Not included in v32.1",
      },
      {
        feature: "Orphan-transaction resource limits",
        roots: "Native v32 orphanage bounds; no -maxorphantx count option",
        knots: "Configurable transaction-count cap (-maxorphantx)",
        core: "Native v32 orphanage bounds; no -maxorphantx count option",
      },
      {
        feature: "Upload target and peer permissions (-maxuploadtarget, -whitebind, -whitelist)",
        roots: "Included",
        knots: "Included",
        core: "Included",
      },
      {
        feature: "Collected mempool samples (getmempoolstats RPC)",
        roots: "Not available in v32.1-roots.1",
        knots: "Included",
        core: "No getmempoolstats RPC",
      },
      {
        feature: "Monotonic process-uptime reporting",
        roots: "Included through the native Core v32.1 base",
        knots: "Included",
        core: "Included natively in v32.1",
      },
      {
        feature: "RAM, cache and index defaults",
        roots: "Native Core v32.1 defaults are preserved",
        knots: "Knots 29.x defaults and resource controls",
        core: "Native v32.1 resource defaults",
      },
      {
        feature: "GUI peer-health view",
        roots: "Bounded live peer table and current-connection details, with stale state cleared",
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
        core: "No per-send RBF checkbox in the v32.1 Qt send dialog",
      },
      {
        feature: "Wallet storage",
        roots: "Descriptor wallets using SQLite",
        knots: "Descriptor wallets; legacy-wallet support depends on the build",
        core: "Descriptor wallets using SQLite",
      },
      {
        feature: "Private-key sweeping",
        roots: "Included as sweepprivkeys RPC and a Qt dialog, with a non-broadcast preview and explicit final broadcast",
        knots: "Included as sweepprivkeys RPC and a GUI dialog",
        core: "Not included in v32.1",
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
        core: "Not included in v32.1",
      },
      {
        feature: "Selected build and dependency portability",
        roots: "Qt 6.2 or newer; native v32 HTTP, IPC and external-signer interfaces",
        knots: "Qt 5.11.3 or Qt 6.2 or newer, selected at build time",
        core: "Qt 6.2 or newer; native HTTP, IPC and external-signer interfaces",
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
            <a href="https://github.com/LuganoPlanB/bitcoin-roots/releases/tag/untagged-3450aebe1f5e3a73703b">Bitcoin Roots<br /><small>v32.1-roots.1</small></a>
          </th>
          <th scope="col">
            <a href="https://github.com/bitcoinknots/bitcoin/releases/tag/v29.4.3.knots20260508">Bitcoin Knots<br /><small>v29.4.3.knots20260508</small></a>
          </th>
          <th scope="col">
            <a href="https://github.com/bitcoin/bitcoin/releases/tag/v32.1">Bitcoin Core<br /><small>v32.1</small></a>
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
