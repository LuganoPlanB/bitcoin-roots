# Core and Roots compatibility review contract

## Comparison command

`compare.py` accepts explicit binary paths, source directories and full source
commit identities. Its process/RPC layer starts separate disposable regtest
nodes and always publishes a versioned JSON report, including partial failures.
Smoke executes all three required block cases. Full additionally executes sigop
and sub-dust boundaries, disconnect/reconnect and restart with control-mempool
persistence. A successful run with caller-supplied local binaries is exploratory
evidence; its `qualified` field is false.

```sh
python3 contrib/roots/compatibility/compare.py \
  --core-bitcoind /path/to/core-build/bin/bitcoind \
  --roots-bitcoind /path/to/roots-build/bin/bitcoind \
  --core-source /path/to/core --roots-source /path/to/roots \
  --core-commit "$CORE_SHA" --roots-commit "$ROOTS_SHA" \
  --core-build-config 'node-only; caller supplied' \
  --roots-build-config 'node-only; caller supplied' \
  --profile smoke --startup-timeout 30 --rpc-timeout 10 \
  --case-timeout 120 --output /path/to/run/report.json
```

Timeouts must be finite, positive and no greater than 3600 seconds. `--work-dir`
selects a parent directory for owned temporary data; it never selects an existing
node datadir. Nodes ignore external configuration/settings, disable wallets and
peer networking, bind RPC to loopback and authenticate with their own cookies.
The harness sends `stop`, then bounds graceful shutdown and escalates to signals
only for process groups it started. SIGINT/SIGTERM return exit 130 after cleanup;
other comparison failures return exit 1. Reports contain bounded sanitized
diagnostics, never the cookie file or RPC authorization header. Reports belong
outside Git.

Source HEAD and source cleanliness are separate fields. A matching immutable
HEAD does not imply a clean working tree or authenticate an executable. Local
binaries and caller build descriptions are recorded as **unverified**. The
`qualified` field stays false for those inputs. `--qualification` fails closed
on identical resolved binary paths/digests or absent verified pinned-source build
provenance. Merely supplying a configuration string cannot make it pass.

For strict qualification, `--build-from-source` exports each pinned Git commit,
builds it in a fresh owned out-of-source CMake directory, and runs those exact
binaries. Supply the source/commit arguments above, omit the binary arguments,
and add `--build-from-source --qualification`. `--build-jobs` is bounded to 1–4
(default 2); `--build-timeout` is the total per-source export/configure/build
budget (default 1800 seconds, maximum 3600). `--work-dir` also selects the parent
for these owned builds. The command removes its build snapshots/binaries at the
end; the JSON report carries their identity and evidence.

The fixed profile disables wallet, GUI, IPC, application tests, benchmarks and
fuzzing; it uses Release and disables compiler caches/launchers. Reports record
Git commit/tree, export SHA256, exact commands, CMake cache/configuration,
compiler versions, builder hash, binary digests and bounded command tails.
Serialization is imported from the same exported Roots snapshot. Uncommitted
or ignored files in the caller source checkout never enter these builds; their
observed checkout cleanliness remains a separate report field. A successful
all-executed in-run build comparison sets `qualified=true`. The report establishes
this pinned-source build relationship, not official-tag signer authentication.
There is no caller-provided manifest or flag that authenticates an existing
binary, and there are no reused compiled-binary directories/caches.

Infrastructure regression tests use actual fake-node processes and loopback
HTTP endpoints; they do not establish product compatibility:

```sh
python3 -m unittest discover -s ci/test -p 'test_roots_compatibility.py'
```

### Fixture interpretations

The harness creates 110 deterministic regtest funding blocks at fixed historical
timestamps, with deterministic Taproot `OP_TRUE` script-path outputs. It imports
the serialization framework only from the explicit `--roots-source` path and
records that source commit and imported file hashes. Each block is serialized
once and those exact bytes go to both nodes. Every accepted block is checked
against the expected tip/height and both nodes' normalized fixture-output UTXOs;
reports include block/transaction digests, admission results and state snapshots.

Both nodes initially use explicit `-acceptnonstdtxn=0` to exercise standard
policy, including on regtest. Expectations are pinned to Core 30.3:

| Case | Boundary and expected admission |
| --- | --- |
| Ordinary | Both admit the control transaction. |
| Datacarrier | Both admit one 83-byte carrier. Two 42-byte carriers exceed Roots' 83-byte aggregate: Roots rejects with `datacarrier`, Core admits. Roots' separate multi-OP_RETURN restriction is reached later; this fixture observes the aggregate rejection. |
| Legacy sigop | Both admit 2490 legacy input sigops and reject 2505. Core 30.3 shares the 2500 input-sigop policy; reject text is recorded separately. Both accept the rejected transaction's block. |
| Sub-dust | Under `subdust-standardness-exception`, both nodes use `-acceptnonstdtxn=1`; Roots also explicitly sets its default `-subdustfeepenalty=1`. This is one deviation from Roots defaults, permitting the output form while retaining its fee penalty. A zero-value P2TR output incurs 330 sats plus the relay minimum: Roots rejects one sat below and admits at the threshold; Core admits both. The below-threshold transaction goes into the paired valid block. |
| Invalid block | A coinbase overpaying the subsidy by one sat is rejected by both; the prior tip and fixture UTXOs remain unchanged. |

Admission is tested before block construction using `testmempoolaccept`; equality
of human-readable rejection strings is never required. The at/below transactions
spend the same confirmed input and are dry runs, avoiding mempool conflicts.
Full switches to the named sub-dust profile only after its standard-policy cases.
All profiles/configuration changes are recorded; required cases cannot be skipped.

Full then invalidates the selected datacarrier and sub-dust blocks individually,
checks the expected historical tip and fixture UTXOs, reconsiders them and checks
the original accepted tip/UTXOs. It records mempool contents independently at
each transition. Under `compatible-p2tr-control-persistence`, a high-fee ordinary
P2TR control is submitted to both nodes; explicit `-persistmempool=1` accompanies
both profiles. Both processes stop and restart using their same owned datadirs.
The accepted fixture state must remain identical and the control transaction
must appear in each reloaded mempool. Equality of other mempool entries is not
required. Reports include every transition and launch configuration.

Comparable local node-only builds can use separate clean pinned source worktrees
and separate out-of-source directories:

```sh
cmake -S "$CORE_SOURCE" -B "$CORE_BUILD" \
  -DCMAKE_BUILD_TYPE=Release -DENABLE_WALLET=OFF -DENABLE_IPC=OFF \
  -DBUILD_GUI=OFF -DBUILD_TESTS=OFF -DBUILD_BENCH=OFF -DBUILD_FUZZ_BINARY=OFF
cmake -S "$ROOTS_SOURCE" -B "$ROOTS_BUILD" \
  -DCMAKE_BUILD_TYPE=Release -DENABLE_WALLET=OFF -DENABLE_IPC=OFF \
  -DBUILD_GUI=OFF -DBUILD_TESTS=OFF -DBUILD_BENCH=OFF -DBUILD_FUZZ_BINARY=OFF
cmake --build "$CORE_BUILD" --target bitcoind -j 2
cmake --build "$ROOTS_BUILD" --target bitcoind -j 2
```

Record these commands, revisions, compiler/configuration and executable digests
with the run. These deliberately limited builds do not exercise wallet, GUI or
IPC functionality. Caller descriptions still do not authenticate a binary; use
trusted pinned-source build evidence for qualification.

This contract defines the evidence expected from external paired-node
qualification tooling. It is a specification for that tooling, not a report
that the profiles below have already run. Read the
[maintainer workflow](../README.md#editing-policy-safely) and
[contribution guide](../../../CONTRIBUTING.md) alongside it.

## Inputs and evidence ownership

For a Core port or a change affecting policy, validation or serialization,
record the matching Core base and Roots candidate as full 40-hex commit SHAs.
Identify the touched behavior, source disposition, fixture owner and owning
checks in commit messages and the port PR. State expected admission/relay
differences before running comparisons; shared Core policy may reject the same
transaction, so Core admission is not presumed successful.

Each run must identify the two source directories and their cleanliness, binary
paths, SHA256 digests and versions, build commands/configuration and provenance,
effective launch flags, fixture IDs, selected profile, timeouts and results.
A caller-supplied SHA or version string alone does not authenticate a binary.
Locally supplied binaries without trusted build evidence are unverified;
qualification builds must come from the pinned sources with comparable recorded
configuration. Identical resolved binary paths or digests cannot qualify a pair.
Commit pinning and official-tag signer authentication are separate claims.

New external fixtures and their helpers belong in `contrib/roots/compatibility/`;
their tooling tests belong in `ci/test/`. Keep existing Core-derived tests,
including `test/functional/test_framework/`, read-only for this external tooling
work. Reuse helpers through an explicit source path with recorded provenance
rather than copying the framework. Feature owners maintain their fixtures as
the corresponding behavior is ported; reports are CI/local run artifacts, never
a checked-in source inventory or test-results database.

## Required profiles

Both profiles run isolated regtest nodes with separate temporary datadirs,
loopback cookie-authenticated RPC, no peer discovery/listening, bounded startup,
RPC and case timeouts, and cleanup of owned processes only. Neither uses a real
wallet or datadir. A common deterministic mature funding chain permits the same
serialized transaction and block bytes to be supplied to both nodes.

| Case | Smoke | Full | Expected observation |
| --- | --- | --- | --- |
| Ordinary transaction/block control | Yes | Yes | Documented compatible admission; both accept the identical valid block. |
| Aggregate datacarrier boundary | Yes | Yes | Roots rejects the selected transaction locally; both accept its identical valid block. |
| Consensus-invalid block negative control | Yes | Yes | Both reject the same invalid block and retain the prior accepted state. |
| Legacy sigop policy boundary | No | Yes | Roots policy rejection and paired valid-block acceptance. |
| Sub-dust fee-penalty boundary | No | Yes | Below/at threshold admission under a named explicit Roots configuration; paired valid-block acceptance. |
| Disconnect/reconnect and restart | No | Yes | Invalidate/reconsider selected datacarrier and sub-dust blocks, restart using the same owned datadirs, then compare persisted state. |

After each accepted block, compare acceptance, best-block hash/height and
normalized `gettxout` snapshots for fixture outputs. Record mempool outcomes
separately; do not require equal reject strings or mempools where policies
intentionally differ. Assert Core admission only as specified by the fixture.
For sub-dust, document the smallest option deviation that permits the output
form while retaining the fee penalty; never silently disable the tested rule.
Control-transaction mempool persistence requires an explicitly compatible
configuration. Two smoke runs should agree semantically after normalizing
timing and temporary paths.

Initial specifications are
[`mempool_datacarrier.py`](../../../test/functional/mempool_datacarrier.py),
[`mempool_sigoplimit.py`](../../../test/functional/mempool_sigoplimit.py) and
[`mempool_subdust_fee_penalty.py`](../../../test/functional/mempool_subdust_fee_penalty.py).
The first two include block-acceptance examples. The sub-dust file specifies
admission thresholds; external paired block coverage must be added rather than
claimed from that existing test alone.

## Results, failures and coverage limits

The versioned machine-readable run report must account for every required
fixture, expected/observed admission, block/state outcome and lifecycle result,
with bounded diagnostics and durations. Startup, wrong-chain, identity,
provenance, timeout, malformed RPC, cancellation, missing fixture, skipped case
or divergent block/state results fail qualification. Preserve partial failure
reports and bounded logs without RPC credentials; clean up owned nodes on every
exit. Fix the cause and rerun affected checks, then renew complete profile
evidence before qualification. Mocks test tooling failures; they cannot replace
real paired-node evidence.

These profiles demonstrate identical serialized-block acceptance and matching
fixture chain state only for the named inputs/configurations. They do not prove
wallet or relay interoperability, exhaustive consensus equivalence, or the
absence of defects outside the finite corpus. Existing previous-release
compatibility jobs and feature tests retain their separate meanings.

CI integration should select smoke for affected high-risk changes and full for
nightly and release qualification, including any optional release rehearsal.
Evidence must identify the exact candidate SHA, Core base, configuration and
profile. If promotion reuses canonical CI, require fresh compatibility evidence
or equally strong evidence for that exact source/configuration; generic prior
CI success is insufficient. Explicit `ci:*` coverage requests still force
their requested fresh coverage. Source/configuration/profile changes invalidate
prior qualification. A missing or skipped required result fails closed.

This contract complements existing release gates: optional five-platform
rehearsal, canonical COPYING, source validation and patch replay, signed
eight-asset create-only draft handling and independent verification. Expanded
dependency notices remain deferred. Compatibility reports are CI artifacts,
not additional release assets; publication still requires separate authority.

## Proportional review examples

For a hypothetical datacarrier port, the PR names both source SHAs, the old
commit and adapted destination, its owner and changed APIs. It declares the
configured Roots admission rejection and the fixture's explicit Core admission
expectation, links smoke results comparing identical blocks/tips/UTXOs and the
invalid-block control, and supplies full evidence for release qualification.
Missing block acceptance or unverified binaries are gaps, not a passing port.

For a docs-only wording change, state the target branch, rationale and lack of
runtime impact; check links/examples and run applicable documentation/tooling
checks. Mark paired-node evidence inapplicable with that reason. Contributors
need not build nodes or manufacture runtime fixture results for such a change.
