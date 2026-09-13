# Offline Roots replay engine

`contrib/devtools/roots-replay.py` is deliberately an offline evidence tool.
The initial `verify` and `plan` commands do not fetch, check out, apply a
patch, create a worktree, invoke hooks, run candidate code, or change a
maintainer repository. They require a full SHA-1 commit ID and its expected
tree, reject dirty worktrees, grafts, alternates, relative paths, bare
repositories, abbreviated revisions, and replacement-object influence.

Run `verify` against a clean, already populated checkout:

```bash
python3 contrib/devtools/roots-replay.py verify \
  --repository /absolute/path/to/core \
  --revision 0123456789012345678901234567890123456789 \
  --expected-tree 0123456789012345678901234567890123456789
```

`plan` performs the same verification and writes one new, mode-0600
`replay-plan.json` into an explicit pre-existing output directory. It refuses
to overwrite output. The resulting plan is evidence only: `replay_state` is
`not-created`, so no candidate or staging branch is implied.

Later handlers must preserve this boundary: network fetch is explicit and
separate, hooks/configuration remain neutralized, all work happens in a tool-
owned disposable worktree, and staging is separately guarded. A clean patch
is never a semantic acceptance decision.

The command surface is fixed as `verify`, `plan`, `replay`, `resume`,
`inspect`, `abandon`, `report`, `export-patches`, and `stage-candidate`.
`replay` accepts the canonical `contrib/roots/adaptation-manifest-29.3.json`,
validates it with the L3 validator, and derives its ordered execution view from
that manifest only. The materials envelope joins each logical application
reference to exactly one immutable handler record; it cannot introduce IDs,
ordering, dependencies, lifecycle, or a different mechanism. Missing, extra,
duplicate, stale, incomplete, or mechanism-mismatched material fails before a
candidate exists. It first verifies the explicit
source revision/tree lock, and creates an independent `owned-candidate` clone
under the supplied state directory only with explicit `--apply`; without it,
replay is a non-mutating preflight. It never adds a linked worktree to the
caller repository. `resume` repeats source verification and re-derives the
units/configuration digest before opening that owned candidate. `report` and
`export-patches` consume only a valid state boundary and fail closed on drift.
`stage-candidate` is not a shortcut for publishing: it requires the exact
non-protected integration branch, current HEAD, clean tracked state, candidate
commit/tree, and literal confirmation. It refuses linked worktrees and can
only advance the already checked-out integration branch to that existing commit;
it never fetches, commits, merges, pushes, or tags.

Application handlers are deliberately tree-locked. The patch handler verifies
the patch SHA256, clean locked input tree, `git apply --index` preflight, and
the exact resulting tree; the data handler applies the same content and tree
locks to one non-symlink payload and repository-relative destination. Either
handler resets its owned worktree on a failed postcondition. Manual units emit
only a bounded review boundary and never alter a candidate.

Generated output is not a shell escape hatch. The initial generator allowlist
contains only the in-process `normalize-lf` transform: it pins input bytes,
output bytes, and both trees. `apply_typed_unit` accepts only exact typed
records for this generator or a manual boundary; incomplete fields and unknown
mechanisms stop before changing a worktree.

The public command accepts no competing replay-units authority: all dispatch
records are derived from the manifest/material join. It dispatches only exact
manual, patch, commit, module/data, or allowlisted generator records. Duplicate
keys, oversized input, unknown fields, and unknown mechanisms fail before
application; callers must not reuse a regular maintainer checkout as the
disposable worktree.

The candidate root is made mode 0700 before materialization; run-state,
ownership-marker, review, and export files are created new-only at mode 0600.
The caller-supplied state directory is never chmodded. While an owned candidate
is active, narrowly scoped SIGINT and SIGTERM handlers remove only the exact
validated candidate and marker, then restore the previous handlers. A normal
resume validation failure retains its immutable state and candidate for safe
inspection; an interrupt removes the owned clone while retaining already
published state boundaries.

Each completed replay boundary is represented by a new mode-0600
`replay-state-000N.json` in an explicit external directory. Its canonical
digest binds schema and tool versions, the normalized `C` locale/UTC/umask
environment, verified input lock, manifest digest, ordered IDs, completed typed
outcomes, and current candidate tree. `resume` revalidates the same canonical
manifest/material inputs and requires an unchanged candidate tree. It refuses
overwritten state, duplicate keys, oversized state,
input, manifest, configuration, tool/environment, or tree drift. A drifted run
must start fresh; it is never repaired with rerere, a merge strategy, or an
implicit reset of another checkout.

`inspect_run` returns a deterministic path-redacted view of a valid state.
`abandon_run` does not delete evidence or reset a repository: it emits one new
mode-0600 `replay-abandoned.json` marker that binds the inspected state digest
and candidate tree. New-only state and marker creation makes concurrent writers
deterministic: exactly one wins a target filename and the other fails closed.

The CLI exposes those operations as `inspect --state`, `abandon --state
--output`, and `resume --repository --units-file --state --state-directory`.
All state and output paths are absolute and must already have a real directory
parent. The tool does not use caller locale, timezone, Git configuration,
hooks, aliases, or replacement objects when deriving evidence.

`report --state --output-directory` writes new-only `replay-review.json` and
`replay-review.txt` files. They are canonical, path-free review bundles listing
the immutable input IDs, manifest/order digests, outcomes, candidate tree,
risk gate, and next review commands. `export-patches --repository --state
--output` writes a new-only `replay-generated-series.patch`: a deterministic
`git format-patch --stdout` compatible mbox, explicitly labelled generated in
its `ROOTS-REPLAY-GENERATED` subject prefix.
Neither report nor export records host paths, timestamps, credentials, or Git
configuration. Export first verifies that the supplied candidate tree still
matches the recorded state.
