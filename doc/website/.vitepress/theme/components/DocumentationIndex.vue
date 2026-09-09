<script setup lang="ts">
import { computed, ref } from "vue";
import { withBase } from "vitepress";
import { documentationCount, documentationGroups } from "../../generated/catalog.mjs";

const query = ref("");

const filteredGroups = computed(() => {
  const search = query.value.trim().toLocaleLowerCase();
  if (!search) return documentationGroups;

  return documentationGroups.map((group) => ({
    ...group,
    documents: group.documents.filter((document) =>
      `${document.title} ${document.path}`.toLocaleLowerCase().includes(search),
    ),
  })).filter((group) => group.documents.length > 0);
});

const visibleCount = computed(() => filteredGroups.value.reduce(
  (total, group) => total + group.documents.length,
  0,
));
</script>

<template>
  <main class="roots-atlas" id="main-content">
    <header class="roots-atlas__header">
      <div>
        <h1>Every guide. One clear map.</h1>
        <p>
          Browse the Markdown documentation that ships with Bitcoin Roots, kept in
          its original source location and presented here without rewritten content.
        </p>
      </div>
      <p class="roots-atlas__count">
        <strong>{{ documentationCount }}</strong>
        <span>source documents</span>
      </p>
    </header>

    <label class="roots-atlas__search">
      <span>Filter the documentation atlas</span>
      <input v-model="query" type="search" placeholder="Try wallet, traffic, RPC…">
    </label>

    <p class="roots-atlas__status" aria-live="polite">
      Showing {{ visibleCount }} {{ visibleCount === 1 ? "document" : "documents" }}
    </p>

    <div v-if="filteredGroups.length" class="roots-atlas__groups">
      <section v-for="group in filteredGroups" :key="group.id" class="roots-atlas__group">
        <details :open="query.length > 0 || group.id !== 'releases'">
          <summary>
            <h2>{{ group.label }}</h2>
            <span>{{ group.documents.length }}</span>
          </summary>
          <ul>
            <li v-for="document in group.documents" :key="document.path">
              <a :href="withBase(document.link)">
                <span>{{ document.title }}</span>
                <small>{{ document.path }}</small>
              </a>
            </li>
          </ul>
        </details>
      </section>
    </div>

    <div v-else class="roots-atlas__empty">
      <h2>No matching guide</h2>
      <p>Try a broader term, or clear the filter to return to all documentation.</p>
      <button type="button" @click="query = ''">Clear filter</button>
    </div>
  </main>
</template>
