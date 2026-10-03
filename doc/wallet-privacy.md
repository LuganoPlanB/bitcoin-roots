# Advanced wallet privacy tools (`v30.3-roots.1` candidate)

This guide describes the advanced wallet tools intended for the
`v30.3-roots.1` candidate. This is candidate documentation, not a release
announcement; the tools are not part of the published `v29.4-roots.2` release.
Bitcoin Core v30.3 is the direct upstream base; selected Bitcoin Knots
history is lineage only, not an upstream dependency or product specification.

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
- `broadcast=false` is the default and returns a signed preview with its input
  total, fee, input count, transaction ID, and hex. It does not submit a
  transaction. Review the destination, net amount, and fee; decode the preview
  when inspecting its fee rate and RBF state before using `broadcast=true`.
- Fees use the current wallet fee settings and chain state. A fresh broadcast
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
  inventory of unavailable outputs.
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
private-key export, a transaction-history product, or any anonymity guarantee.

## Candidate verification

The candidate behavior is exercised by
[`wallet_sweepprivkeys.py`](/test/functional/wallet_sweepprivkeys.py),
[`wallet_send.py`](/test/functional/wallet_send.py), wallet unit tests, and
[`src/qt/test/wallettests.cpp`](/src/qt/test/wallettests.cpp). Before operating
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
`v30.3-roots.1` candidate label in this guide does not replace that check.
