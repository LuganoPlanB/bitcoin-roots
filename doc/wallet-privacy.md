# Advanced wallet privacy tools (v31 candidate)

The local v31 candidate implements this contract. It is based on Bitcoin Core
v31.1 and the pinned Roots 30.3 source; this is candidate documentation, not a
release announcement. See [the port review](roots-v31-port-review.md) for
implementation and verification provenance. Selected Bitcoin Knots history is
lineage only, not a product specification.

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
- Fees use the current wallet fee settings and chain state. v31 removed
  `settxfee`/`paytxfee`; sweep uses native fee estimation and its configured
  fallback, with one locked fee snapshot per transaction. A fresh broadcast
  can fail because an input was spent, relay or mempool policy changed, or the
  fee is no longer sufficient. Recreate and review a preview after such a
  failure; do not assume a previous preview is still valid.
- Supplied keys are used transiently for scanning and signing. They are not
  imported as wallet keys, descriptors, labels, or settings, and are redacted
  from the RPC console and normal logs. This is not key backup or recovery:
  retain an independent, secure backup until the sweep confirms.

The Qt **Sweep Private Key** dialog applies the same contract. Its key field is
masked and disables copy, cut, and context-menu actions. The dialog uses a
transient secure key cache, cleans its temporary encoded input, and overwrites
then clears its input control when it can invalidate a request. A cancellation
can stop an outstanding scan or preview; once broadcast submission passes its
final authorization boundary, it cannot be cancelled and the dialog waits for
the result. These measures reduce the lifetime of the entered secret, but do
not promise to erase every copy retained by the GUI toolkit or host. Showing
the key still exposes it on screen; use that control only when the display is
safe. Broadcasting is irreversible and may fail after a chain or fee change.

## Inspect and select wallet inputs

The Send Coins coin-control dialog exposes wallet-reported facts about UTXOs:
amount and effective value, confirmations, lock and spendability state,
watch-only status, reuse, grouping, input-size estimates, and fee/change/RBF
consequences. It does not infer identity, rate privacy, or make a transaction
anonymous.

- A **Reused address** indicator and grouping describe observable wallet facts.
  Selecting a group combines its inputs and can reveal an on-chain relationship;
  neither label is a privacy recommendation or score.
- The dialog starts with the wallet's normal available-output list. Locked
  outputs remain visible so their lock state can be inspected, but cannot be
  selected. Unsafe and immature outputs are not added merely to show a status;
  outputs excluded by those normal availability filters are absent until their
  state changes. “Not spendable” is metadata for a displayed output, not an
  inventory of unavailable outputs. Descriptor-owned watch-only coins can be
  selected for PSBT preparation; their status describes missing signing material,
  and does not promise that the wallet can complete every required signature.
- Explicit input selections are honored for the prepared send. A material wallet
  change, including a competing spend or reorganization, clears the selection
  so the operator must review current inputs again.
- The confirmation shows the effective inputs, fee, change, and whether the
  transaction signals Replace-By-Fee. These are transaction facts, not a
  promise that a replacement will be accepted by every peer or policy.
- A custom change destination is conspicuous and validated. If it is empty or
  invalid, the wallet uses a newly generated change address. Review custom
  change carefully because it can affect address linkage.

These tools do not add CoinJoin, payjoin, silent automatic consolidation,
a transaction-history product, or any anonymity guarantee.

## Export a spending key for one address

The v31.1-roots.6 candidate extends `exportaddresskey`, introduced in
v31.1-roots.3, to recognized addresses and scripts in the selected descriptor wallet:

```sh
bitcoin-cli -rpcwallet=WALLET exportaddresskey ADDRESS
bitcoin-cli -rpcwallet=WALLET exportaddresskey ADDRESS taproot_output
```

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
variables and files. The Qt console filters the command's arguments from history
but displays the explicitly requested result. JSON, HTTP and client buffers may
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
Quietproof's current normal proof workflow supports native P2WPKH only;
exporting a P2PKH key does not add P2PKH proof support or an anonymity guarantee.

## Candidate verification

The candidate behavior is exercised by
`wallet_exportaddresskey.py` (`test/functional/wallet_exportaddresskey.py`),
`wallet_sweepprivkeys.py` (`test/functional/wallet_sweepprivkeys.py`),
`wallet_send.py` (`test/functional/wallet_send.py`), wallet unit tests, and
`src/qt/test/wallettests.cpp` (`src/qt/test/wallettests.cpp`). Before operating
on funds, compare the intended command with the built binary and inspect a
non-broadcast preview:

```sh
bitcoin-cli help sweepprivkeys
build/test/functional/test_runner.py wallet_sweepprivkeys.py
python3 ci/test/test_wallet_privacy_contract.py
```

`bitcoin-cli help sweepprivkeys` and the final transaction preview are the
authority for a particular binary and transaction. Verify the executable's
identity separately with `bitcoin-cli --version`; the intended
v31 candidate label in this guide does not replace that check.
