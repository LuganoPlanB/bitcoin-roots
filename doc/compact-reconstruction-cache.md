# Compact reconstruction extra transactions

Bitcoin Roots retains Core v32.1's default of **100** extra transactions.
`-blockreconstructionextratxn=N` explicitly overrides the count. The historical
Roots default of 32768 was superseded in roots.5 and is not restored here.
`-blockreconstructionextratxnsize=N` independently limits charged transaction
dynamic storage, defaulting to **10 decimal MB** (10000000 bytes). Neither
setting changes mempool admission, transaction relay policy or block validity.

This folds the released cache contract characterization from
`bd5422a481aae345cf287bce1c8ab3dc296c20b7`, its lock-precondition correction
`2fe385d7f51a2a7e40a778fc3a5c6e580499fbb0`, and the default restoration
`79ebaa31225897f47403a95e510654f9531cdc45` from the roots.6 source.
Jaromil, Plan-B Foundation, devjoinedthechat and upstream Core/Knots lineage
are preserved. Historical v31 experiment results are not v32 measurements.

## Accounting and ownership

The charge is the sum of native `RecursiveDynamicUsage(*tx)` for occupied
slots: dynamic input/output vectors, scripts and witnesses. Serialized bytes
are a different quantity. The charge excludes transaction objects, shared
pointer control blocks, slot metadata, allocator bookkeeping and other node
resources. It is neither total cache allocation nor process RSS.

The number of allocated slots is at most the count limit and the byte limit
divided by the native slot size. That separate metadata bound does not deduct
slot bytes from the charged transaction budget. Slots are allocated lazily on
the first eligible insertion, then retained until the peer manager is destroyed.
The count default remains 100 even when the byte budget would permit more.

The cache overwrites the next slot and evicts older references until the new
charge fits. Eviction happens before addition to avoid unsigned overflow.
Transactions with a native dynamic charge at or above 100000 bytes, or above
the entire configured byte budget, are excluded through the common insertion
gate. This gate also protects the replacement producer. A rejected transaction
has the native preliminary size guard as well. Disabled/excluded calls may
still calculate native dynamic usage; disabling retention does not imply zero
processing cost.

Raw insertion can hold the same shared object in multiple slots and charges
each occupied slot. Network duplicate eligibility is separate from insertion.
Removing a slot drops its reference and charge; another owner, including the
orphanage, can keep the allocation alive. Destruction and allocator behavior
need not immediately reduce process RSS. No unique incremental RSS claim is
made from the charged-byte accounting.

Setting either limit to zero disables retention. The size option has four
decimal places in MB, giving 100-byte increments; negative values disable,
while malformed values retain the default, as in the released source. The
native count parser clamps to its unsigned range. These peer-resource parsers
are distinct from the strict Roots local-policy grammar.

## Regression coverage

`compact_extra_transactions_tests` calls the actual insertion routine and
reconstruction consumer through explicitly named test methods. It checks zero
count/bytes, no allocatable slots, one slot, an exact native charge and one byte
below, an extreme count bounded by slot storage, wraparound, sparse slots and
multi-entry eviction. Threshold tests construct the closest attainable native
charges below/at-or-above the per-entry limit. Duplicate-reference and weak
pointer assertions separate charging from allocation lifetime. These structural
fixtures do not claim consensus-valid transactions or actual replacement.

`p2p_compactblocks_extratxs.py` exercises default 100/101 behavior, explicit
counts, count/byte precedence, disabled/malformed/negative settings, actual
replacement, duplicate rejected offers and orphan resolution. Its paired policy
case proves local rejection and absent relay/mempool state, then accepts the
same consensus-valid block through cache-hit, disabled-cache and eviction
paths, supplying requested block transactions. The native compact-block suites
retain their own protocol, collision and malformed-input oracles.

The helper wrapping the replacement producer requires the peer, transaction
download and inventory locks to be unheld; it acquires the message-processing
and transaction-download locks in the native order before calling production
code. No additional production RPC, history collector or cache metric endpoint
is introduced.

## Current-source microbenchmarks

Build the normal CMake profile with `BUILD_BENCH=ON`, then build `bench_bitcoin`.
Use a disposable explicit `-testdatadir`. Bounded execution examples are
`-filter='CompactExtra.*' -sanity-check` and
`-filter='CompactExtra.*|BlockEncoding.*' -min-time=10`.

| Benchmark | Timed work and denominator |
| --- | --- |
| `CompactExtraInsertCount` | Real common insertion, default count/byte limits, warmed slots; 100 offered transactions per batch. |
| `CompactExtraInsertBytes` | Real common insertion under a three-small-transaction byte budget, alternating native script sizes; 100 offers per batch. |
| `CompactExtraInsertDisabled` | Real common insertion with count zero; 100 offers, zero stores. |
| `CompactExtraColdReplacementProducer` | A fresh manager per iteration, accepted-transaction download/broadcast bookkeeping, the production replacement insertion loop and manager destruction; three offered replacements, two cache-eligible and one above the per-entry bound. |
| `BlockEncodingStdExtra` | Native reconstruction with a populated mempool and the default 100 extra entries; one `InitData` operation. |

The first three include helper lock overhead. Their transaction construction,
initial population and charge/slot snapshots are outside timing. They measure
direct insertion of prebuilt structural fixtures, not network eligibility,
deserialization, mempool validation, orphan handling or full P2P latency.
The cold producer includes setup, allocation, bookkeeping, locks and teardown;
its fresh manager prevents growing broadcast backlogs between iterations. It
does not perform real mempool replacement or measure steady producer throughput.
Its denominator counts offered replacements, including the excluded one, and
must not be reported as successful stores. Functional tests cover real producers.

These benchmarks execute current production methods; no modeled ring or memory
bookkeeping substitutes for them. Reconstruction is a consumer benchmark, not
an insertion measurement. Timings depend on the machine and test harness;
smokes establish execution, not a throughput requirement, mainnet distribution,
memory saving or performance improvement. The old v31 exported/instrumented
paired experiment remains a historical reference in its source revision; its
artificial warming and allocator/RSS limitations cannot qualify current behavior.

Untimed `cache_resources` lines report actual native dynamic charges, configured
limits and snapshot slot/occupancy counts. `modeled_slot_payload_bytes` is only
slot count times native pair size; it does not observe vector capacity, allocator
rounding or RSS. The cold producer reports its separately checked preflight
state, since each timed manager is destroyed. These numeric resource observations
must remain separate from the timed per-offer denominators.
