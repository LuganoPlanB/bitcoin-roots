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

The v31.1-roots.3 candidate adds `exportaddresskey ADDRESS` for a known address
in the selected descriptor wallet:

```sh
bitcoin-cli -rpcwallet=WALLET exportaddresskey ADDRESS
```

The wallet must hold the private key locally, and an encrypted wallet must be
unlocked through the normal wallet authorization flow. Watch-only,
private-key-disabled and external-signer wallets are rejected. Addresses must
belong to the active node network. Only P2PKH (compressed or uncompressed) and
native P2WPKH (compressed) are supported; P2SH, P2WSH and P2TR are rejected.

The result has exactly two fields:

- `private_key_hex`: the 32-byte unsigned big-endian leaf scalar as exactly 64
  lowercase hex characters, retaining leading zeros. It has no `0x` prefix,
  WIF marker or checksum.
- `compressed`: a boolean describing the public-key serialization for the
  address; always `true` for native P2WPKH.

The result contains a spending secret. Use protected, authenticated local RPC.
For a consumer such as Quietproof, transfer the hex plus a newline through
protected stdin, keeping the secret out of command arguments, environment
variables and files. The Qt console filters the command's arguments from history
but displays the explicitly requested result. JSON, HTTP and client buffers may
retain copies; total memory erasure is not promised.

Only the selected leaf key is returned, without a seed, master key, xprv or
descriptor. Disclosure of a non-hardened child private key together with the
corresponding ancestor xpub can still compromise that ancestor's private
subtree. The RPC does not import keys, generate addresses, advance keypool
indices or change balances or spend state. Keep an independent secure backup.
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
