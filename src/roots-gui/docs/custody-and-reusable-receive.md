# External custody and reusable Receive

The shipped RPC profile has no LocalSignerService, DeviceTransport or
ReusableReceiveService implementation. New Wallet keeps the daemon wallet
route. Import Wallet → Recovery words names the missing local signer and
offers a return to public watch-only import. It has no word, private-key or
mnemonic-passphrase input.

## LocalSignerService

The isolated signer owns mnemonic entry, display, ordering, passphrase
handling, backup verification and seed persistence. The GUI request contains
only the named format, word list, word count, grid dimensions and passphrase
entry mode. This contract describes BIP39 English grids for 12, 15, 18, 21 or
24 words. Availability requires the actual provider to qualify the exact
format, grid and policy. The provider renders the words and owns the entry
surface. The GUI may receive an exact public descriptor, a fingerprint with
the exact public descriptor commitment and provider identity, or a transient
provider-scoped handle. A short master-key fingerprint is only a key hint.
The user still needs to match the complete independently expected wallet
policy and network before relying on the signer.

The request binds backend, genesis, wallet, document revision, lock epoch,
applied session/profile, policy identity, operation generation and ticket.
An explicit action is required before opening a trusted workflow. On return,
the current owner rechecks operation, format, policy and provider. The
descriptor and its commitment must both match the independently expected policy
before adoption. Cancellation,
timeout, disconnect and changed scope retire the old result. Verification of
a backup has its own action and ticket. Its result attests only to the named
verification on the current format and policy. It supplies no signer handle
or transaction authority.

A future implementation needs isolated trusted display and input, encrypted
secret storage with its own explicit protection and unlock policy, retention
and in-memory cleanup rules, and authenticated provider identity. Crash,
timeout and cancel paths must preserve those boundaries. The GUI's preferences,
clipboard, accessibility values, logs, public recovery and encrypted metadata
backups have no mnemonic or key fields.

| Secret | Owning boundary |
| --- | --- |
| Mnemonic passphrase | Isolated local signer or its own trusted entry flow |
| Application protection password | GUI workspace metadata protection |
| Metadata archive password | Independent encrypted metadata file workflow |
| Daemon wallet passphrase | Explicit daemon unlock for the verified wallet |
| Device PIN and passphrase | Selected device or its isolated transport |

Application unlock recovers GUI state. Daemon wallet and signer-wallet recovery
use their corresponding verified backup workflows.

## DeviceTransport

An independently qualified transport owns device PIN, device passphrase and
card communication. An action identifies the selected device, policy, wallet,
network, generation and exact operation. PIN and passphrase entry happen on the
device or through isolated trusted input. Card status is public state, separate
from explicit policy confirmation. Neither operation grants transaction
approval. Cancellation, timeout or disconnect discards the retired local result.
An already accepted device action may still finish.

Connected signer discovery, display and transaction signing continue through
the separately validated daemon signer path. RPC advertisement cannot
qualify this optional transport or enable secret entry and card operations.

## Reusable Receive

The Reusable policies view is separate from ordinary address allocation and
offline reservation. It lists the public fields for each supported contract.
All are unavailable in the shipped profile.

| Policy family | Public fields |
| --- | --- |
| Fixed descriptor | Canonical public descriptor and its exact fixed address |
| Silent payments | Encoded receive identifier and scheme version |
| Payment code | Encoded public code and format version |

The reusable record binds exact provider, wallet, genesis, document revision,
policy identity, generation and lock epoch. A fixed descriptor must actually
derive the exact displayed address and may have no range. Other families
require their own qualified format, network and independent ownership proof.
No account mapping, code generation or payment derivation is supplied here.
These reusable records have no Next Address action or allocator index. An
advancing policy would require a separately qualified transition and
independently proved next output before exposing address advancement. Standard
Receive continues to use its existing verified RPC and reservation routes.

## Qualification before enablement

- Use canonical BIP39 entropy/mnemonic/seed vectors for each implemented
  language and word count, including text normalization, the empty and
  non-empty mnemonic passphrase cases, wrong checksum and wrong ordered words.
  Cross-check derived public keys and fingerprints against authoritative BIP32
  vectors. Use separate fresh synthetic fixtures for backup restore and for
  passphrase/policy mismatch, cancellation and restarted provider ownership.
- Qualify the exact transport, device model and firmware against the relevant
  vendor protocol vectors and a controlled end-to-end test. Exercise trusted
  entry, wrong PIN, passphrase-policy mismatch, card insertion and removal,
  disconnect and timeout while each operation is pending, and stale scope.
- Bind any future reusable family to authoritative vectors for its exact
  protocol and network versions, including wrong network, format and policy.
  Match independently generated public identifiers to the intended wallet.
  Keep reusable verification and any future derivation/advancement separate.

Current capability-on tests use only controlled public metadata. NativeHost
tests exercise the actual shipped unavailable views. No real secret owner,
hardware, seed recovery or reusable payment provider is qualified.
