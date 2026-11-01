# Advanced wallet privacy tools (v32.1 candidate)

This unpublished Core v32.1 port preserves released Roots wallet contracts.
See [the port review](roots-v32-port-review.md) for source lineage and current
qualification. The Qt6 sweep, per-send RBF and expanded coin-control controls
are carried in this candidate. Rendered and platform qualification is recorded
separately from backend and GUI test coverage.

## Sweep an external private key

`sweepprivkeys` scans confirmed UTXOs controlled by supplied Base58 WIF private
keys and creates one signed transaction paying a destination in the selected
wallet. It is intended for an explicit, operator-controlled recovery of an
external key, not routine wallet import or consolidation.

- P2PKH is supported for valid compressed and uncompressed WIF keys. For a
  compressed WIF, native P2WPKH and wrapped P2SH-P2WPKH are also supported.
  Taproot and any other script form are not scanned by this command.
- The destination must be a spendable address controlled by the wallet handling
  the RPC request. A destination in another loaded wallet is rejected. The
  supported configuration is a descriptor wallet with local private keys;
  watch-only, private-key-disabled, and external-signer configurations are not
  eligible destinations. Legacy wallets must be migrated to descriptor wallets;
  this port does not restore a Berkeley DB build option.
  Local keys must satisfy a native signing path: an incomplete 2-of-2 multisig
  destination is rejected, while 2-of-3 with two local keys is eligible. This
  check also recognizes encrypted keys without unlocking or exporting them.
  Signing capability does not guarantee present timelock maturity or future
  network acceptance.
- `broadcast=false` is the default and returns a signed preview with its input
  total, fee, input count, transaction ID, and hex. It does not submit a
  transaction. Review the destination, net amount, and fee; decode the preview
  when inspecting its fee rate and RBF state before using `broadcast=true`.
- Fees use the current wallet fee settings and chain state. Core removed
  `settxfee`/`paytxfee`; sweep uses native fee estimation and its configured
  fallback, with one locked fee snapshot per transaction. If estimation is unavailable and fallback is disabled, sweep returns an error before preview or submission. A fresh broadcast
  can fail because an input was spent, relay or mempool policy changed, or the
  fee is no longer sufficient. Recreate and review a preview after such a
  failure; do not assume a previous preview is still valid.
- Supplied keys are used transiently for scanning and signing. They are not
  imported as wallet keys, descriptors, labels, or settings. The Qt RPC console
  filters sweep arguments from history. This is not key backup or recovery: retain an independent,
  secure backup until the sweep confirms.

The Qt **Sweep Private Key** dialog masks the key and disables copy, cut,
dragging and context-menu actions. Preview scans and signs on a worker without
broadcasting. Review the selected wallet, destination, amount, fee, signaling
and transaction ID. **Broadcast sweep** opens a confirmation whose default is
Cancel. Submission must match the exact preview transaction ID; wallet/tip
notifications and edits invalidate the preview, including while confirmation
is open. A changed fee or input requires another preview.

Cancellation can stop a pending scan or preview. After the final submission
boundary it cannot undo broadcast, and the dialog waits for the result. Closing
or invalidating a request clears its transient key cache; worker cleanup also
covers discarded queued requests. Temporary encoded input is cleansed and the
input control is overwritten then cleared. Qt and the host may retain other
copies, so these measures do not promise complete memory erasure. Revealing a
key can expose it on screen.

## Inspect and select wallet inputs

The Qt coin-control dialog shows wallet-reported amount/effective value,
confirmations, lock, watch-only, reuse and grouping facts, and input-size
estimates. Effective value uses the current wallet fee choice. Filtering changes
visibility without changing selected inputs. The **Safe local-key outputs**
total includes safe, unlocked, mature outputs with local keys; watch-only and
external-signer outputs are excluded from that total even when selectable for
PSBT preparation. Local keys do not guarantee complete signing for every
partially owned construction.

The view starts with the wallet's normal available-output list. Locked
outputs remain visible but cannot be selected. Unsafe and immature outputs are
not added merely to show a status; this is not an inventory of unavailable
outputs. Watch-only coins can be selected for PSBT preparation without promising
that this wallet can finish signing.

Wallet notifications clear selected inputs even when the balance stays the same.
Review current inputs after a competing spend or reorganization. Grouping and
reuse describe wallet facts, not a privacy score or anonymity guarantee.

Send starts its RBF checkbox from the wallet's `walletrbf` preference and honors
an explicit choice for that send. Confirmation reports the prepared transaction's
actual signaling, inputs, fee rate and change. Loaded PSBT0 and PSBT2 proposals
also report signaling from their reconstructed transaction; native v32 sends
create PSBT2 by default. Signaling does not guarantee replacement acceptance.
Review custom change carefully because it can affect address linkage. These
controls do not add CoinJoin, payjoin or automatic consolidation.

## Export a spending key for one address

This v32.1 port preserves the complete released roots.3-through-roots.6
`exportaddresskey` contract for recognized addresses and scripts in the selected
descriptor wallet:

```sh
bitcoin-cli -rpcwallet=WALLET exportaddresskey ADDRESS
bitcoin-cli -rpcwallet=WALLET exportaddresskey ADDRESS taproot_output
```

`exportaddresskey`, `listdescriptors`, `gethdkeys` and `derivehdkey` are all
disabled by default, including their public-only modes. To enable these four
RPC methods, start the node with an explicit command-line option:

```sh
bitcoind --allow-dangerous-rpc
```

This permission is captured at startup. It cannot be enabled through
`bitcoin.conf`, read/write configuration, JSON settings, runtime settings or an
RPC request. Omit the flag or use `--allow-dangerous-rpc=0` to disable it;
`--allow-dangerous-rpc=1` also enables it. Values other than 0/1 and negated
forms are rejected. The same startup requirement applies to the Qt RPC console.

Enabling the methods does not bypass RPC authentication or wallet authorization.
The wallet must hold private keys locally, and an encrypted wallet must be
unlocked through the normal wallet authorization flow. Watch-only,
private-key-disabled and external-signer wallets are rejected. Addresses must
belong to the active node network. Supported constructions include P2PKH,
native and wrapped P2WPKH, P2SH, P2WSH, their recognized wrappers,
multisig/Miniscript, and Taproot `tr()`/`rawtr()` internal, leaf, direct output
and MuSig2 participant keys. P2PKH can retain compressed or uncompressed keys;
native P2WPKH requires compressed keys.

Export uses existing wallet metadata and known descriptor indices. It does not
search unknown derivation paths, recover missing redeem/witness scripts or
Taproot commitments, or treat arbitrary script data as keys. Missing metadata,
unsupported constructions and inconsistent key/script data have distinct
errors. Partial ownership returns the available local keys even if cosigners,
preimages or timelocks are still required to spend.

The optional `key_type` argument selects one of two modes; omitting it or using
JSON null selects `wallet`:

- `wallet` returns original stored/derived scalars and their full public-key
  identities. Roles identify script keys, Taproot internal/leaf/direct output
  keys and MuSig2 participants. A participant secret remains a participant
  secret even when every participant is local; it is not an aggregate secret.
- `taproot_output` requires a Taproot address and returns effective key-path
  keys. It applies the exact known TapTweak, including tree-less `tr()`, to
  locally held internal keys. Direct output/`rawtr()` keys are not tweaked
  again. Script-only or participant-only ownership can succeed in `wallet`
  mode while this mode fails because an output private key is unavailable.

The canonical result is a nonempty `keys` array sorted by full serialized
public key. Each entry contains:

- `pubkey`: the full SEC public-key serialization corresponding to the scalar;
- `private_key_hex`: a 32-byte unsigned big-endian scalar as exactly 64 lowercase
  hex characters, retaining leading zeros, without `0x`, WIF marker or checksum;
- `compressed`: a boolean describing that public-key serialization; and
- `roles`: sorted unique strings from `script`, `taproot_internal`,
  `taproot_script`, `taproot_output` and `musig2_participant`.

Identical full public keys have their roles merged. Keys with different full
identities remain distinct; export does not negate a stored key to match an
address. The top-level `private_key_hex` and `compressed` fields remain as
compatibility aliases for the original P2PKH/native P2WPKH exports in `wallet`
mode. Those results now also contain `keys`; other constructions and
`taproot_output` results have only the canonical array.

The result contains a spending secret. Use protected, authenticated local RPC.
For a consumer such as Quietproof, transfer the hex plus a newline through
protected stdin, keeping the secret out of command arguments, environment
variables and files. The Qt console filters export arguments from history; an explicitly
requested console result contains the secret. JSON, HTTP and client buffers may
retain copies; total memory erasure is not promised.

An exported key need not be exclusive to this address or be a leaf key. A
direct address for a stored descriptor root can reveal that root's private
scalar, **including an HD master private key**. Reused keys and MuSig2
participants before post-aggregation derivation can control many outputs.
Disclosure of a non-hardened child private key together with the corresponding
ancestor xpub can also compromise that ancestor's private subtree.

Exported scalars are not a wallet backup: scripts, control blocks, cosigners,
preimages or timelocks may still be needed. The RPC does not import keys,
generate addresses, advance keypool indices or change balances or spend state.
Keep an independent secure backup. Broader export support does not expand the
script forms scanned by `sweepprivkeys`.
The reviewed Quietproof integration assumes native P2WPKH; broader export does
not establish additional proof support or an anonymity guarantee.

## Candidate verification

The complete `test/functional/wallet_exportaddresskey.py` runs in RPC and CLI
modes, with `wallet_sweepprivkeys.py`, native wallet/key/descriptor/RPC suites,
and native key-operation/OpenRPC regressions. Current execution and remaining
prerequisites are recorded in the port review. Before operating on funds,
compare the intended command with the actual built binary:

```sh
bitcoin-cli --version
bitcoin-cli help exportaddresskey
bitcoin-cli help sweepprivkeys
```

This source manual does not establish a release, a deployment, full platform
qualification, or erasure of host/client copies of secrets.
