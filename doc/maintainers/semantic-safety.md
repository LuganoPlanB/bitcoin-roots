# Semantic safety gates for portability replays

`contrib/devtools/roots-semantic-safety.py` is an offline fail-closed validator.
It only validates evidence: it does not apply a patch, create a candidate,
promote an adaptation, or make a release decision.

Each classified change has paths, symbols, targets, behavior, rename/move and
shared-header provenance, transitive dependencies, and successful docs/tests/
branding/tooling checks. The nine critical zones are manual-only:
consensus/validation, chain parameters, serialization/script, P2P wire state,
wallet/database, secrets, policy-versus-consensus boundaries, release signing,
and manifest self-modification. Unknown input, malformed/deep/cyclic transitive
graphs, and failed applicable content checks are rejected. Low-risk docs,
tests, branding, and isolated tooling retain explicit noncritical review.

The manual ledger binds every entry to prior and newer manifest revision and
digest, candidate and scoped-tree digests, immutable base/input/result/artifact
hashes, rationale, alternatives, policy analysis, a promotion mechanism, and
zones. Conflict hunks are typed unique records with base, input, and range
digests; typed resolution records carry coverage, result, and range digests.
The resolution range must equal its conflict range, its coverage digest is the
SHA256 canonical JSON digest of that conflict record, and its result must be an
entry artifact. Conflict base/input digests must be declared base/input blobs.
Their ID sets must match exactly. Reviewer and test evidence are also typed,
unique, candidate/scoped-tree/current-manifest-bound result records at or after
the candidate finalization timestamp. Critical zones need expert markers.
Embargoed public records contain only an entry ID, zone, and `redacted: true`
metadata.

Candidate evidence has immutable manifest, materials, and plan inputs plus
candidate/scoped-tree/manifest bindings and a
verified enclosing digest. It has finalization and production timestamps,
complete unique candidate-bound unit outcomes and approvals, exactly seven
unique invariant results, all required platforms (or an approved limitation),
generated-output hashes, compatibility/migration, reproducibility, performance
baseline/result/explanation, limitations, security review, and separate false
release approval. Invariant results carry expectation scope, command/test ID,
fixture/artifact digests, responsible adaptation, and freshness. Locked
Core/Knots differential evidence is required only for shared differential
invariants, and policy-rejection/block-acceptance evidence only for its policy
boundary invariant; both are forbidden elsewhere. Results before candidate
finalization, stale bindings, omitted/reused IDs, or manual outcomes fail.

Lifecycle evidence is proposal-only. It requires unique complete fixtures for
exact cherry-pick, rebased equivalent, partial, reverted, renamed, obsolete API,
rejected consensus change, same-subject/different-behavior, and negative
decision cases. Each has candidate and manifest bindings; the only accepted
decision value is `manual-required` for `absorbed`, `obsolete`, `rejected`, or
`rewritten` proposals.

The governance table is machine-readable input with one unique entry for each
scenario below. P0 stops work; P1 holds for subsystem review; P2/P3 retain
evidence routes. Time pressure never produces `continue`.

| Scenario | Severity/outcome | Authority | Disclosure |
| --- | --- | --- | --- |
| consensus regression | P0 / stop | `consensus-review` | `stop-work` |
| policy-validity leak | P0 / stop | `policy-consensus-review` | `stop-work` |
| serialization | P0 / stop | `serialization-review` | `stop-work` |
| wallet durable corruption | P0 / stop | `wallet-review` | `stop-work` |
| secrets | P0 / stop | `security-response` | `security` |
| provenance | P0 / stop | `provenance-review` | `stop-work` |
| incomplete delta | P0 / stop | `release-review` | `stop-work` |
| remote crash | P1 / hold | `security-review` | `security-sensitive` |
| benign static analysis | P3 / continue | `ordinary-review` | `ordinary-review` |
| time-pressured release | P1 / hold | `release-review` | `ordinary-review` |
