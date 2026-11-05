# Bitcoin Roots v32.1 preservation contracts

This is the unpublished `v32.1-roots.1` candidate, based directly on Core
v32.1 (`f0de87f040621468b956f01f526a43a010310501`). The
[port review](roots-v32-port-review.md) accounts for the full released v31
series through roots.6, main-only differences, source credits and verification
limits. A version string or local test result does not establish publication,
deployment or hosted/platform qualification.

Bitcoin Roots preserves Bitcoin Core consensus. Admission, relay and block
template controls are local policy: consensus-valid blocks remain acceptable
when they contain locally rejected transactions. No RDTS/BIP110 enforcement is
introduced. Bitcoin Knots is historical feature provenance, not the direct
base or a dependency. Upstream and Jaromil/devjoinedthechat contributions and
Plan-B Foundation attribution remain distinct.

## Availability and migration

The policy, mining, peer, wallet, Qt6 and runtime implementations have their
owning-theme evidence in the port review. The complete candidate, differential
compatibility and integration gates remain separate. The supported GUI uses
Qt >=6.2; Qt5 and libevent support are not restored. v32 uses native HTTP,
descriptor wallets with SQLite, native IPC and cluster/graph algorithms.
Optional dependencies and platform execution gaps are recorded in the
[build guide](roots-build-portability.md) and [CI policy](../ci/README.md).

Keep native v32 interfaces and defaults unless a documented Roots contract
changes them. Wallet sends use PSBT v2 by default and recognize loaded PSBT
v0/v2 signaling. `settxfee`/`paytxfee` are removed; sweep uses native estimation
and configured fallback. Native mempool graph/package invariants, mining IPC,
monotonic uptime and RAM/cache/index defaults remain in force. Do not copy old
v31 resource or cache defaults into a new configuration.

## Stable contracts

These IDs remain the preservation vocabulary across Core ports. Tests linked
from the port review prove their exercised cases, not every configuration or
platform. Consult built `bitcoind -help` and `-help-debug` for current options;
the generated public manual/example configuration omits debug-only options.

| Stable ID | Current behavior and boundaries |
| --- | --- |
| `policy-parsing` | Roots-scoped whole-token, decimal/range/overflow and boolean parsing, quarter-vbyte rounding and redacted errors; inherited Core options retain their native parser. See [grammar](feature-contracts/policy-parsing.md). |
| `effective-policy` | Initialized `getmempoolinfo.policy` settings, units and enforcement gates; no reparsed guess at applied policy. See [RPC contract](feature-contracts/effective-policy.md). |
| `policy-standardness` | Local script/input/witness/sigop and bare-key/multisig/CAT21/token/ephemeral controls retain exact rejection boundaries. Defaults include 1,650-byte script policy, 20 bytes per sigop and 2,500 legacy sigops. |
| `policy-carrier` | Enabled by default with 83 aggregate bytes, full recognized input/witness counting, one OP_RETURN output and 1 vbyte per detected data byte. Nonstandard and bare-only carriers are independently controlled; extra weight only raises policy size. Multiple OP_RETURN outputs retain `multi-op-return` rejection. |
| `policy-dust` | 3,000 sat/kvB dust relay floor, dynamic mode off by default, initialized scheduler ownership, safe shutdown/retry and floor fallback when an estimate is unavailable. |
| `policy-subdust` | Enabled deficit penalty changes modified fees, not actual fees or consensus; independent arithmetic and carrier-composition oracles are retained. See [subdust contract](feature-contracts/subdust.md). |
| `policy-replacement` | Default full RBF (`fee,-optin`), optional Never/OptIn policies and native v3/TRUC/package/graph invariants; configured policy is reflected by the additive reporting contract. |
| `policy-coinage` | Relay maturity/coin-block-age defaults disabled; configured next-height/reorg-aware age arithmetic remains overflow-safe. |
| `policy-reuse` | `spkreuse=allow` default; configured conflicts are tracked across admission, replacement and mempool changes. |
| `mining-template` | Default 1,200,000-weight priority area, configured size/weight/fee/resource limits, dependency ordering and priority-selected ancestor accounting; residual selection uses native v32 chunks. GBT and IPC use the same policy. |
| `peer-controls` | Permissions, current transport/privacy, bounded address/cache/network resources and manual block fetching. Compact reconstruction retains Core's 100-entry default plus a 10 decimal MB charged dynamic-storage cap; either zero limit disables that cache. Slot/object overhead and shared ownership are separate from the byte charge. |
| `peer-health` | Current-connection facts, reset/unavailable-state clearing and GUI selection refresh. No deprecated `misbehavior_score` field or stale historical peer score is promised. |
| `wallet-foundation` | Descriptor/SQLite boundaries, database-directory/write-access/symlink guards, external-signer descriptor closure, local unlock and anti-CAT21 locktime handling. No Berkeley DB build option is restored. |
| `wallet-rbf` | Wallet-derived per-send RBF preference and actual prepared transaction/PSBT signaling; signaling does not promise replacement acceptance. |
| `wallet-sweep` | Transient WIF input, confirmed P2PKH/compressed-key P2WPKH/wrapped-P2WPKH scanning, locally signable selected-wallet destination, preview default, exact-txid final authorization and cancellation/submission boundary. |
| `wallet-coin-control` | Wallet-derived amount/effective value, locked/reused/grouping/signing facts and notification invalidation. Locked outputs remain visible but cannot be selected; unsafe/immature outputs are not added solely for display. |
| `runtime-config` | Forced → CLI → read/write config → JSON settings → network/default precedence, native scalar/negation rules and complete-read/EOF/comment/atomic-write/reset behavior. |
| `operator-stats` | Native monotonic `uptime` is available. Retained model/schema source is dormant: no sampling and no `getmempoolstats` RPC registration. |
| `runtime-resources` | Allocation/extension preserves existing bytes; FD saturation/closure and GiB conversions remain safe. Native RAM/cache/index defaults are preserved. |
| `build-portability` | Embedded-library defaults and explicit system LevelDB/secp256k1 checks, native compiler/filesystem/crypto behavior, Qt6/QR/IPC/signer and platform conditions. Missing optional libraries fail or are declared reductions, not successful coverage. |
| `identity-desktop` | Bitcoin Roots product name, candidate version and Plan-B Foundation copyright with separate Core historical credit; Roots assets, font-aware QR-PNG export and conditional Windows taskbar. Wire identity remains `Satoshi`; numeric protocol/version identifiers are unchanged. |
| `maintenance-ci` | Additive classifiers, affected fast regressions, bounded smoke and full diagnostic transport; long sanitizer/corpus/platform work retains nightly/manual cadence. A local fixture pass is not hosted assurance. |
| `maintenance-release` | Git-native exact-base source/tag validation, portable export excluding `.github/**`, signed local replay/parity and immutable new-draft guards. Five-platform builds, release signing and publication require their own current evidence and authority. |
| `maintenance-docs` | Current catalog/privacy/build/guidance/notes and help generated from current binaries. Historical release records remain historical; version labels do not imply release approval. |
| `maintenance-native` | Behavior-preserving native maintenance, safe empty writes and test-only assertion hooks remain in their owning targets; no blanket overlay or new replay framework. |

## Wallet key disclosure

`exportaddresskey`, `listdescriptors`, `gethdkeys` and `derivehdkey` are disabled
by default, including public-only modes. They require the explicit startup
command-line option `--allow-dangerous-rpc` (or `=1`); configuration files,
JSON settings and runtime settings cannot enable them. Omitting the option or
using `=0` disables them. Authentication, wallet authorization and unlock
requirements still apply. See the [wallet manual](wallet-privacy.md) before
enabling methods that can disclose private keys.

The complete roots.3-through-roots.6 `exportaddresskey` contract is retained:
recognized P2PKH/P2WPKH and P2SH/P2WSH wrappers, multisig/Miniscript, Taproot
internal/leaf/direct-output and MuSig2 participant keys. It requires the selected
local-key wallet and normal unlock. The sorted `keys` array is canonical;
original P2PKH/native-P2WPKH aliases remain compatible. `taproot_output` applies
the exact known tweak when an effective output key is available.

An exported scalar can be an HD master key or control other outputs. Partial
ownership does not promise a spend without scripts, cosigners or timelock
conditions. Export is not backup, does not expand sweep's script coverage and
does not establish additional Quietproof proof support. See the complete
[wallet manual](wallet-privacy.md#export-a-spending-key-for-one-address) for
authorization, response semantics and secret-handling risks.

## Deliberate non-features and limits

No RDTS/BIP110, CoinJoin/payjoin/automatic consolidation, Network Watch,
privacy score or anonymity guarantee is added. A Tor launcher is not supplied;
native proxy/control support does not imply subprocess launching. MiniUPnPc
discovery is available where configured, but no UPnP runtime/depends integration
is claimed. Statistics remain dormant. Optional-platform, rendered, long fuzz
and sanitizer qualification must be assessed from their actual current runs.

Core's source/tag identities and ancestry are pinned in the port review.
The user waived upstream Core-tag signature verification for this port; it is
not recorded as passed. Roots commits and local patch replays still require
the specified signing key. Old released tags/assets and historical credits
remain immutable; a later Core base starts a new folded semantic series.
