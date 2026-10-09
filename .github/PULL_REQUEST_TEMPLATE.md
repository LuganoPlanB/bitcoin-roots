<!-- Keep answers proportional to the change. For docs-only work, identify the
scope and checks, and mark runtime compatibility evidence not applicable with
a reason; building nodes is unnecessary. See CONTRIBUTING.md and the review
contract in contrib/roots/compatibility/README.md. Remove unused prompts. -->

## Purpose and scope

- Journey: Roots feature/fix, new-Core port, or main-only repository work.
- Target branch, affected supported lines, behavior changed and rationale:
- Policy/consensus impact and expected policy differences (or why inapplicable):

## Port provenance

<!-- For ports/imports: name full source commit IDs and destination IDs. Account
for every source change as kept, adapted, adopted upstream, or deliberately
dropped; explain splits, omissions and API adaptations. Link the range-diff and
owning checks. An inseparable cross-cutting unit needs a dependency rationale. -->

## Verification

- Exact commands, results and tested configuration; remaining gaps:
- Help/defaults, documentation and release-note impact:

<!-- For affected policy/validation/serialization behavior and Core ports,
complete the compatibility evidence below. A finite fixture run does not prove
general consensus equivalence. Existing required CI and release checks remain. -->

## Compatibility evidence (when applicable)

- Matching Core base and Roots candidate full commit SHAs; build provenance:
- Profile (smoke/full), fixture IDs, owner and expected admission differences:
- Identical serialized-block acceptance, tip/height and fixture UTXO results:
- Negative control and, for full, reconnect/restart outcomes:
- Report/run link tied to these sources and configuration; failures or gaps:

<!-- New external fixtures belong in contrib/roots/compatibility/, with tooling
tests in ci/test/. Existing Core-derived tests are reference inputs for this
external qualification work; do not edit them as part of that work. -->
