# Pinned RPC capability contract

Roots baseline: `12a50ee10a359da22f6bb37798da8ac6b2b99977`.
Core baseline: official v32.1, `f0de87f040621468b956f01f526a43a010310501`.
`tests/fixtures/rpc-contracts.json` records argument defaults, effects and
unavailable fixtures. `tests/rpc/source_contracts.py` checks actual definitions,
registrations and the transaction-version constant chain in both pinned trees.
It is a supplementary source audit requiring those local Git objects, not a
runtime build dependency. The normal offline protocol suite needs only vendored
inputs. Live isolated Roots probes capture actual help and behavior; a live Core
binary has not been qualified at this boundary. Later compatibility tests must
accept explicitly supplied exact binaries and retain their identities.

| Operation | Roots baseline | Core v32.1 source | Consumer rule |
| --- | --- | --- | --- |
| `walletprocesspsbt` | sign=true, finalize=true, bip32derivs=true | Same | Enrich false/false; explicitly approved sign true/false; finalization separate |
| `walletcreatefundedpsbt` | transaction version 2, PSBT 2; add_inputs false with supplied inputs, true otherwise | Same | Explicit version/input/change/fee policy; verify resulting recipients, input eligibility, fee and change independently |
| `createpsbt` | transaction version 2, PSBT 2, replaceable=true, locktime=0 | Same | Unsigned construction; set version/sequence/locktime deliberately |
| `descriptorprocesspsbt` | finalize=true; no sign boolean | Same | Public descriptors only for enrichment; finalize=false; never invent a sign=false parameter |
| `combinepsbt` | Registered | Registered | Independent full identity/prevout/policy comparison before combination |
| `finalizepsbt` | extract=true | Same | Public finalization/extraction; never broadcast automatically |
| `psbtbumpfee` | replaceable=true, PSBT 2; prepares PSBT | Same | RBF preparation; never substitute `bumpfee` for the staged flow |
| `createwallet` | descriptors=true; disable_private_keys=false, blank=false, external_signer=false | Same | Choose daemon custody/watch-only explicitly; creation/import/address allocation need uncertainty reconciliation |
| `restorewallet`, `importdescriptors` | Registered | Registered | Daemon-host paths, explicit rescan/import and no blind retry |
| `getaddressinfo`, `getnewaddress`, `deriveaddresses` | Registered | Registered | Observation vs stateful index allocation vs public non-consuming derivation are distinct |
| `listdescriptors(private=false)` | Blocked by default startup dangerous-RPC policy | Public mode available in source without that Roots gate | Probe exact policy; report unavailable and guide deliberate daemon-side choice; never enable policy silently |
| `listunspent` | minconf=1, maxconf=9999999, include_unsafe=true | Same | Eligibility is independent of list membership; preserve frozen/reserved/excluded constraints |
| `listsinceblock`, `gettransaction` | Registered | Registered | Incremental wallet history/reorg facts; no arbitrary complete-chain claim |
| `lockunspent` | Registered | Registered | Explicit stateful daemon locks with ownership; no unlock of foreign/unknown locks |
| `testmempoolaccept`, `sendrawtransaction` | Registered | Registered | Policy preflight is distinct from submission; timeout means uncertainty for the same transaction |
| `enumeratesigners`, `walletdisplayaddress` | Build/configuration dependent | Same source build gate | Require ENABLE_EXTERNAL_SIGNER and configured daemon signer; no GUI USB/HID transport |
| `sweepprivkeys` | Takes WIF keys, signs even with `broadcast=false`; `true` also submits | Absent | The GUI shows the unsigned-sweep dependency and does not call this RPC |
| `exportaddresskey` | Roots extension; export gated | Absent | Keep secret export outside automatic GUI workflows |

The scalar source audit retains symbolic `DEFAULT_WALLET_TX_VERSION` and
`DEFAULT_RAWTX_VERSION` expressions and checks their `CTransaction::CURRENT_VERSION`
declaration resolves to 2. Shared `CreateTxDoc` arguments and `bumpfee_helper`
are inspected directly; the audit does not pretend to be a general C++ evaluator.
Funding `fee_rate` is sat/vB while legacy `feeRate` is BTC/kvB. Exact JSON number
tokens and decimal strings remain exact through the domain adapter. Do not use
`sendtoaddress`, `sendmany` or `bumpfee` to bypass draft/review/sign/broadcast.

## Failure and capability fixtures

Owned Roots regtest probes use disposable explicit datadirs, no peers/listening,
pruning enabled, an empty public-only descriptor wallet and cookie authentication.
No mining, signing, broadcasting, private-key import or production funds occur.
Both normal and `-disablewallet` processes are stopped only by their owner and
waited to exit before cleanup. Logs contain public synthetic data, not cookies
or authorization headers. Device results do not claim hardware was exercised.

| Observation | Meaning |
| --- | --- |
| -32601 method not found | Unsupported or disabled method. A generic remote endpoint does not reveal which cause; the disabled-wallet fixture has explicit startup evidence |
| -8 unknown named parameter | Unsupported argument; never silently drop a required constraint |
| -18 no wallet loaded | Wallet unavailable, not an empty balance or fabricated wallet |
| getblockchaininfo.pruned=true | Historical data may be unavailable; no complete-history assertion |
| -1 restart with -signer | External signer configuration absent; no connected-device success |
| -4 dangerous RPC disabled | Roots startup policy blocks even public descriptor enumeration |
| -5 mainnet xpub on regtest | Network-specific extended-key encoding mismatch; explicit public tpub conversion preserves derivation, without private key access |

Live descriptor oracle checks cover static/nested segwit, multisig, Taproot key
and tree descriptors and ranged extended-public-key children. Script/network
ownership still requires explicit wallet policy evidence; an imported change
flag, label or key fingerprint is never sufficient. Cached data retains source,
age, backend/network/wallet identity and missing-data explanations offline.
