# Offline compact reconstruction cache experiment

This Linux-only harness compares the exact Core v31.1 base
`9be056a8a72b624dae9623b2f7bded92c2a21c91` with the current committed Roots
source. It uses disposable local regtest nodes and Python P2P peers, with no
public network traffic or real wallets. It changes no production default.
Python 3.10+, CMake and the repository's normal build dependencies must already
be available; nothing is installed. Use a quiet host with sufficient disk space
for two source/build trees and retained datadirs. Run from the repository root.

Build the ordinary functional-test configuration first, if absent, following
the contributor guide (`build-issue56/test/config.ini` is used by this harness).
The prepared binaries themselves use identical out-of-source RelWithDebInfo
profiles, compiler and system allocator, wallet/GUI/tests/IPC disabled. CMake's
existing ccache integration remains enabled when available. Builds are sequential
and use fixed `-j 2`; never rebuild while a run uses those binaries.

```sh
python3 contrib/roots/cache_bench.py prepare --work /absolute/disposable/bench-builds
python3 contrib/roots/test_cache_bench.py
python3 contrib/roots/cache_bench.py run --work /absolute/disposable/bench-builds \
  --name smoke --smoke --count 4
```

`prepare` requires a fresh destination. It exports Git objects, adds temporary
scope hooks and the identical `cache_bench_metrics.h` to the exports, saves both
complete instrumentation patches, and builds only bitcoind. The active source
tree is untouched. Build command exits, timestamps, archive/patch/binary/cache
digests, source commits, compiler and host are saved in `builds.json`; logs remain
next to it. An interrupted/failed owned build can be recovered with `prepare
--resume` and the same `--work`; the existing logs remain. After changing the
instrumentation, rebuild both variants before running. A build receipt is local
provenance, not release authentication or a cross-platform claim.

After the smoke gate and review of the experimental design, the following is
one command for a complete, predeclared experiment:

```sh
python3 contrib/roots/cache_bench.py run --work /absolute/disposable/bench-builds \
  --name paired-128 --count 128 --seed 560031 --warmup 1 --repetitions 5
```

The design, variant flags, counts, seed, duration rule, timeouts and harness/build
digests are persisted **before** starting warmup. Non-smoke runs require at least
one warmup and five paired measured repetitions. Variants are shuffled in each
pair with a fixed seed, and each receives the same workload seed. Each variant
runs sequentially with a 900-second timeout; failed and partial samples remain
in `runs.json`, stdout logs and datadirs. The command exits unsuccessfully if any
variant fails. `summarize RESULTS_DIR` can regenerate derived statistics from
the retained samples; it never discards failed samples or replaces raw data.
Use a new `--name` for each experiment. Cleanup is explicit: remove only the
owned disposable work directory after retaining evidence; failed runs remain
available for diagnosis. A timeout terminates its owned process group.

## Controls and cells

The matrix is declared in `VARIANTS` in `cache_bench.py`:

| Variant | Cache and policy |
| --- | --- |
| Core default | exact-base count 100, Core policy |
| Roots default | count 32768, 10,000,000 charged dynamic bytes, Roots policy |
| Roots count100 | count 100; unchanged 10,000,000-byte budget and policy |
| Roots disabled | count 0; unchanged policy |
| Roots count1 | one slot; unchanged byte budget and policy |
| Roots bytes10k | default count, 10,000 charged-byte budget |
| Roots count8-bytes1k | count 8, 1,000 charged-byte budget |
| Roots CAT21 allowed | default cache, only `rejectparasites=0` changed |

All nodes use `acceptnonstdtxn=0`, `persistmempool=0`, `dbcache=64` and one
unprivileged v1-transport peer. No fee/policy floor is normalized between Core
and Roots. Accepted fixtures pay well above both floors. The CAT21 control is
an A7 policy pair; it is not evidence for other inventory rows. Use `--variants`
with a comma-separated subset for diagnosis, recording that reduced coverage.

Each variant has seven workload classes: ordinary accepted transactions,
CAT21-marked local rejects, real fee-bumped replacements, missing-parent orphans,
alternating small/large output scripts, sustained four-offer-per-input churn,
and unique CAT21/carrier rejects. Churn/adversarial alternatives change sequence
and spend the same confirmed input; this is a fixed finite adversarial workload,
not a Poisson stream or a steady-state production trace. The mixed workload's
larger carrier also reaches a different standardness gate on Roots; actual
preflight reasons and mempool counts are retained. Core accepts CAT21 markers,
and some later same-input offers can become economic-replacement rejects.
Its cache and policy effects must therefore be reported separately.

Every class gets a full-offer forward probe. Independent CAT21 transactions
also get zero/half/full offered overlap in forward and reverse insertion order
(six total overlap/order cells). Cache capacity may reduce achieved overlap;
actual cache and mempool hits are measured separately. Each cell restarts the
node to clear cache, filters, orphanage and mempool; retains the same funded
chain; uses fixed mocktime, transactions and compact-block nonce; then accepts
a valid block, supplying all requested `blocktxn` data. Orphan parents are
provided only in the block, preserving shared orphan/cache references at the
measurement point. Ordinary transactions and replacements have asserted resident
sets; orphan presence, Roots CAT21 rejection reason, zero-hit/disabled misses
and unconstrained default-cache full hits are checked. The zero-cache cases
must still finish block validation.

Serialized fixture digests are compared across variants by case/repetition.
A mismatch makes that pair unsuitable for causal claims. Offered counts,
preflight outcomes, resident/orphan populations, block transactions, insertion
calls and missing indices have distinct denominators. Preflight RPCs are outside
load timing and test initial-state eligibility; they do not establish the final
outcome of a later conflicting offer. This small workload does not cover all
policy modes, large mainnet mempools, collisions or network latency distributions.
Unmeasured inventory rows must retain explicit unanswered questions.

## Metric contract and limitations

Every raw row retains the following units and scope:

| Fields | Meaning |
| --- | --- |
| `charged_dynamic_bytes` | Sum of native `RecursiveDynamicUsage(*tx)` over occupied extra slots; Core's equivalent accounting estimate is calculated with the same expression. Excludes transaction objects/control blocks and slot memory. |
| `slot_bytes`, `slots`, `occupied` | Native vector capacity × native pair size, allocated slot count and nonempty slots. Allocator bookkeeping is excluded. |
| `unique_dynamic_bytes`, `distinct` | Deduplicated transaction object pointers in the extra vector and their native dynamic charge; not unique incremental process memory. |
| `external_refs`, `shared_objects` | Snapshot of shared-pointer reference counts beyond cache-held references; owners are counted, not identified. Concurrent owner lifetimes can change. |
| `baseline_rss_bytes`, `steady_rss_bytes` | `/proc/PID/status` VmRSS before offers and immediately before block receipt. Entire node scope, including chain, mempool and allocator. |
| `sampled_peak_rss_bytes`, `rss_samples` | Maximum VmRSS observed every 5 ms during load/reconstruction; an observed lower bound on the true phase peak. |
| `lifetime_hwm_bytes` | Linux VmHWM since this node restart, including startup; not a cache-only peak. |
| `insert_cpu_ns`, `insert_wall_ns`, `insert_calls` | Sum of thread CPU and monotonic wall nanoseconds inside the extra insertion function, including excluded/disabled attempts that reach that function. Clock overhead included; not all received rejects reach insertion. |
| `stored_calls`, `stored_cpu_ns`, `stored_wall_ns` | Calls that actually assigned a cache slot, and their summed timings; evicting/overwriting stores still count. This is distinct from unique transaction count or net growth. |
| `insertion_cpu_ns_per_call`, `insertion_cpu_ns_per_offer` | Same total divided by actual function calls or peer TX offers; undefined denominator is null. |
| `insertion_cpu_ns_per_store`, `insertion_wall_ns_per_store` | Successful-store timings divided by store count; null when disabled/excluded. |
| `init_cpu_ns`, `init_wall_ns` | Thread CPU and monotonic wall nanoseconds inside InitData; no block validation or P2P roundtrip. |
| `load_process_cpu_ns`, `block_process_cpu_ns` | Process user+system CPU delta from `/proc/PID/stat`, quantized by host clock ticks, including background work. Zero does not prove zero CPU. |
| `load_wall_ns`, `block_accept_wall_ns` | Sender-side monotonic elapsed time through ping synchronization or accepted-tip observation. Includes local scheduling, Python and RPC polling. |
| `cache_hits`, `mempool_hits`, `requested_missing`, `roundtrips` | Actual extra/mempool matches, requested transactions and getblocktxn messages, not inferred from nominal offered overlap. |
| `*_wire_bytes` | Serialized v1 message payload plus 24-byte message headers for offers, compact block, request and response; excludes TCP/IP/ACKs, pings and unrelated messages. `block_wire_bytes` sums compact+request+response. |
| `full_block_wire_bytes` | Reference full-block payload plus one v1 header; a hypothetical alternative, not observed full-block traffic. |

Snapshots traverse the whole extra vector **before** the InitData timer. This
adds unmeasured work and warms the vector/cache; both implementations receive
identical instrumentation, but allocation-size-dependent warming can bias the
comparison. The functional framework's default debug logging remains enabled
equally for both builds. Timers, logging and the whole-vector snapshot affect
end-to-end latency; the snapshot's cost itself scales with allocated slots.
There is no uninstrumented performance control yet, so even a large end-to-end
delta cannot establish a production latency difference. InitData timings are
measurements after artificial warming. These results must not support a default
change without an uninstrumented control and review. RSS and shared-pointer observations cannot
establish unique cache RSS or allocator savings from eviction.

Statistics retain every sample and report n, median, min/max and sample standard
deviation. p95 is deliberately null below 20 samples; five runs do not establish
a tail distribution. Warmup is retained separately. No synthetic traffic rate,
cache-hit rate or timing is described as representative of mainnet. Failures,
fixture mismatches, unknown metrics and unmatched populations prevent stronger
claims; a legitimate outcome is to retain defaults and report uncertainty.
