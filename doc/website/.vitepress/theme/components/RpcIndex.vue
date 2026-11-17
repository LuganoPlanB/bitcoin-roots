<script setup>
import { computed, ref } from "vue";
import { withBase } from "vitepress";
import { filterCommands } from "./rpc-filter.mjs";
import { data as commands } from "./rpc.data.js";

const labels = {
  blockchain: "Blockchain", control: "Node control", mining: "Mining", network: "Network",
  rawtransactions: "Raw transactions", signer: "External signers", util: "Utilities",
  wallet: "Wallet", zmq: "ZeroMQ notifications",
};
const categories = [...new Set(commands.map(command => command.category))].sort();
const query = ref("");
const category = ref("");
const rootsOnly = ref(false);
const matches = computed(() => filterCommands(commands, query.value, category.value, rootsOnly.value));
const groups = computed(() => categories.map(key => ({
  label: labels[key] || key, commands: matches.value.filter(command => command.category === key),
})).filter(group => group.commands.length));
function clear() {
  query.value = "";
  category.value = "";
  rootsOnly.value = false;
}
</script>

<template>
  <div class="rpc-index">
    <div class="rpc-fields">
      <label>Find a command<input v-model="query" type="search" placeholder="Try signmessage or BIP322"></label>
      <label>Category<select v-model="category">
        <option value="">All categories</option>
        <option v-for="key in categories" :key="key" :value="key">{{ labels[key] || key }}</option>
      </select></label>
    </div>
    <label class="rpc-toggle"><input v-model="rootsOnly" type="checkbox"> New or changed in Roots</label>
    <p role="status">{{ matches.length }} {{ matches.length === 1 ? "command" : "commands" }}</p>
    <button v-if="!matches.length" type="button" @click="clear">Clear filters</button>
    <section v-for="group in groups" :key="group.label">
      <h2>{{ group.label }}</h2>
      <dl>
        <template v-for="command in group.commands" :key="command.name">
          <dt><a :href="withBase(command.url)">{{ command.name }}</a></dt>
          <dd>{{ rootsOnly ? command.change : command.summary }}</dd>
        </template>
      </dl>
    </section>
  </div>
</template>

<style scoped>
.rpc-fields { display: flex; gap: 1rem; flex-wrap: wrap; }
.rpc-fields label { flex: 1 1 12rem; font-weight: 600; }
.rpc-fields input, .rpc-fields select {
  display: block; width: 100%; margin-top: .25rem; padding: .55rem .75rem;
  border: 1px solid var(--vp-c-divider); border-radius: 6px; background: var(--vp-c-bg-soft);
  color: var(--vp-c-text-1); font: inherit; font-weight: 400;
}
.rpc-fields select { appearance: auto; }
.rpc-toggle { display: flex; align-items: center; gap: .5rem; margin: 1rem 0; }
.rpc-toggle input { appearance: auto; accent-color: var(--vp-c-brand-1); }
.rpc-index [role="status"], dd { color: var(--vp-c-text-2); }
dt { font-family: var(--vp-font-family-mono); overflow-wrap: anywhere; }
dd { margin: .1rem 0 .9rem; }
button { text-decoration: underline; color: var(--vp-c-brand-1); }
</style>
