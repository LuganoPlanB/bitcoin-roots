<script setup lang="ts">
defineProps<{ detailed?: boolean }>();

const overview = [
  { feature: "Checking blocks", core: "Verifies the Bitcoin blockchain", roots: "Uses the same rules as Core" },
  { feature: "Disk space", core: "Keep all blocks or turn on pruning", roots: "Same choice" },
  { feature: "Sharing transactions", core: "Uses Core’s relay defaults", roots: "Tighter limits on embedded data, with more settings to choose from" },
  { feature: "Using a wallet", core: "Send, receive and choose which coins to spend", roots: "Also see address reuse and preview a private-key sweep" },
];

const details = [
  { feature: "Data in transactions", core: "Allows multiple OP_RETURN outputs within the data limit.", roots: "By default, allows one OP_RETURN output and up to 83 bytes of recognized data in total, including data in inputs." },
  { feature: "Replacing transactions", core: "Uses full replace-by-fee policy.", roots: "Adds a replacement policy selector (-mempoolreplacement) and a fee-replacement checkbox when sending." },
  { feature: "Inspecting the mempool", core: "getmempoolinfo reports size, fees and memory use.", roots: "The same command also reports the local policy settings in effect." },
  { feature: "Choosing coins to spend", core: "Coin control lets you select individual outputs.", roots: "Also shows whether an address was reused and how much an output is worth after the fee to spend it." },
  { feature: "Sweeping a private key", core: "No built-in sweep dialog.", roots: "Preview a transfer from a supported private key into your wallet, then decide whether to broadcast it." },
  { feature: "Access to spending keys", core: "Wallet permissions and unlock rules apply.", roots: "Also disables key-disclosure commands by default. They require an explicit startup option (--allow-dangerous-rpc)." },
  { feature: "Reconstructing blocks", core: "Limits the number of extra transactions kept for compact blocks.", roots: "Also limits how much memory those transactions use. The default is 10 MB." },
];
</script>

<template>
  <div class="roots-comparison-table" role="region" tabindex="0"
    :aria-label="detailed ? 'Detailed feature comparison' : 'Core and Roots at a glance'">
    <table>
      <thead>
        <tr>
          <th scope="col">Feature</th>
          <th scope="col">Bitcoin Core</th>
          <th scope="col">Bitcoin Roots</th>
        </tr>
      </thead>
      <tbody>
        <tr v-for="row in detailed ? details : overview" :key="row.feature">
          <th scope="row">{{ row.feature }}</th>
          <td>{{ row.core }}</td>
          <td>{{ row.roots }}</td>
        </tr>
      </tbody>
    </table>
  </div>
</template>
