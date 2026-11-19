# Payment protocol extensions

The shipped RPC profile has no PayjoinModule, PaymentCodeContacts provider,
or private reusable receive provider. Tools → Payment-code Contacts opens the
local capability explanation. It accepts no endpoint or contact data. A
payment URI carrying `pj`, `pjos` or `req-pj` stops at the local payment
parser. To make an ordinary payment, obtain a separate plain Bitcoin request.
There is no supported Payjoin wire version in this profile.

## Service permission and negotiation

A future PayjoinModule would need a qualified interoperable implementation
and independently validated protocol vectors for its exact version, network
and peer. Its approval must identify the selected endpoint and recipient, the
exact data to be shared and the time-limited operation ticket. The GUI's
local owner also binds the exact original PSBT source digest, proved ordered
payment and fee, designated recipient and change, maximum fee, tip, wallet,
backend, genesis, document and draft revision, policy, profile, session,
generation and lock epoch. A URI, tab, preference, restored document or prior
signature grants no permission. A different endpoint, profile, owner, intent
or data class needs its own deliberate action and permission.

The outbound data projection contains the unsigned transaction, previous
transaction/UTXO records and sighash choices. Previous transactions disclose
transaction links and can expose unrelated outputs of those transactions.
Opaque records, global xpubs, origin and change metadata, contacts, labels,
credentials and secret material stay local. A future transport must separately
prove its peer, exact endpoint and privacy/proxy route without following
redirects or falling back to direct DNS or connections. Client code must
consume permission once through the owning operation. The current gate
prepares a local payload only. There is no transport, resolver, network
retry, browser launch or configured endpoint in the shipped application.

The tested local adoption boundary is deliberately narrow. It accepts
unsigned PSBT 0 with fixed native key-hash inputs and SIGHASH_ALL. A proposal
may append one independently proved counterparty input and increase only the
amount at the original recipient script with that input's value. The change
script and amount, every other output, exact sender inputs, ordered positions,
version and locktime must remain identical. The added input cannot change
replaceability, activate a relative lock or enable a previously inactive
absolute lock. The total fee must meet the
original amount and remain under the separate authorized bound. The sender
makes no extra fee contribution. This is a local safety check for public
fixtures, with no interoperability or anonymity claim.

The previous output is checked against independent proof bound to the current
network, owner and tip. The reply cannot supply its own independent evidence.
Signed or finalized input fields, sighash changes, unsupported fields, old
tickets, stale or substituted proofs and unapproved output changes are refused.
The exact original and received bytes remain separately inspectable. The
working copy has the exact admitted transaction and all preexisting local
maps, including opaque data; no remote metadata enters those maps. The new
input may supply only the permitted public UTXO and sighash fields. A successful
handoff invalidates the previous review and approval, and yields unsigned work
for a separate explicit review. No collected signatures, finalization, send
authority or automatic retry crosses this boundary.

## Public contacts and reusable private addresses

A qualified PaymentCodeContacts provider would establish the exact public code,
its format version, network, authenticated contact identity, and own the
policy-specific derivation and notification requirements. Public code
verification is local to qualified provider logic. Any network notification
needs separate permission bound to the exact endpoint, contact, code, data
class, owner, policy and ticket. A notification can link public codes with
transactions. It must be independently verified and return to transaction
review before signing. No account synthesis, contact discovery, notification
transport or address derivation is implemented here.

Private reusable Receive depends on the corresponding qualified
[ReusableReceiveService](custody-and-reusable-receive.md). Silent-payment
receive identifiers and payment codes have different formats and independent
validation requirements. The fixed descriptor route also has its own exact
public proof. None can allocate a standard next address or draw from the
reserved address pool. Secret ownership stays with the appropriately qualified
signer or wallet. Enabling a family needs authoritative format vectors, owner
and network binding, tested receive/scanning and derivation, and end-to-end
privacy permission and failure evidence for the real client.

FiatRateProvider and ReleaseUpdateProvider are separately qualified by
currency and release channel respectively. Both are off in the RPC profile.
Opt-in can only enable an actual provider. Viewing project releases in the
browser and locally verifying a downloaded file are independent actions.
Neither checks for updates, provides fiat rates, downloads nor installs files.

The hand-maintained [capability ledger](capability-ledger.json) distinguishes
compiled surface, shipped profile, runtime prerequisites and coverage. Its
CTest validator checks actual optional registry pointers, implementation
symbols and configured test and case/journey bindings. Controlled model or
native evidence does not qualify a missing provider or installed delivery.
