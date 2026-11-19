# Capability and unavailable-state contract

Capability is observed per method/argument/wallet/network, never inferred from
the daemon's product name. Available has no disabled reason; unavailable has a
typed reason and a user-facing explanation. Unknown is distinct from zero,
unspent, complete or available. The [capability ledger](capability-ledger.json) binds current entries to
implementation symbols and registered checks.

| Capability | Release classification | Owner / unavailable explanation |
| --- | --- | --- |
| Chain, node, balances, history, addresses, coins, fees | Required RPC | RPC observations; method/argument unsupported, wallet disabled/unloaded, pruned data, authentication, stale/offline each remain distinct |
| Create/load/restore/import wallet | Required when daemon supports corresponding operation | Wallet service; remote-path distinction and stateful cancellation reconciliation |
| Wallet history rescan | Required with proven named height arguments | One wallet per operation; inspect pruned bounds and review the explicit block range |
| Receiving offline | Required bounded public-data route | Receive service; only a previously reserved pool with visible provenance, otherwise no reserved offline address |
| Unsigned offline construction, PSBT0/2 inspect/merge/export/finalize | Required for supported policies | Standalone protocol adapter; exact policy/provider support, never Core-internal fallback |
| Signing | Required daemon/external PSBT routes | Explicit approval; daemon walletprocesspsbt true/false or validated external import; no local private custody |
| Hardware | Airgapped file/QR + supported daemon signer RPC | Signer service; missing daemon signer differs from disconnected device; direct GUI USB/HID outside profile |
| Accounts | Extension | MissingAccountService; hide account strip and explain Add Account |
| Indexed address/output history | Extension, unavailable | Requires a linked HistoryIndexAdapter. Indexed coverage has its own provenance. |
| Avoid partial spends | Extension, unavailable | Requires a linked TransactionConstructionAdapter. Efficiency remains required RPC behavior. |
| Isolated local software wallet | Extension, unavailable | Requires a qualified LocalSignerService and its own protected secret store. |
| Direct device PIN/passphrase and card operations | Extension, unavailable | Requires a qualified DeviceTransport with trusted entry and confirmation. |
| Reusable receive policies | Extension, unavailable | Requires a qualified ReusableReceiveService for the exact public format and network. |
| Payjoin and payment-code contacts | Extension, unavailable | Require qualified PayjoinModule and PaymentCodeContacts providers. An admitted changed proposal requires fresh review. |
| Fiat rates and release update checks | Extension, unavailable | Require a qualified FiatRateProvider or ReleaseUpdateProvider and scoped opt-in. |
| Explorer links | Optional opt-in | Exact confirmed URL on the verified network and direct profile. Opening the browser is explicit. |
| Native platform | Linux required; macOS/Windows follow-on | Distinguish source-only, compiled, native-tested, unavailable; no cross-platform claim from Linux |

The model's typed reasons include unsupported method/argument, wallet disabled,
pruned data, offline, privacy locked, missing protocol/account service, no direct
device transport and unknown. The [connection service](server-connection.md)
maps transport/authentication errors separately without leaking credentials
in URLs, logs, captures or session files.
Inspection/enrichment always supplies explicit sign=false/finalize=false;
signing uses true/false. This repository's dangerous-public-key RPC gate must be
probed: public descriptor/key inventory is not assumed available by default.
No capability probe may allocate addresses, import wallets, sign or broadcast.

The [history and construction contracts](history-and-construction-extensions.md)
define the registry, exact provider boundaries and RPC limits.
The [custody and reusable Receive contracts](custody-and-reusable-receive.md)
state the owners, public-only data shapes and separate qualification gates.
The [payment protocol contracts](payment-protocol-extensions.md) define explicit
disclosure, independent unsigned adoption and fresh review.

### Daemon external signers

Roots exposes `enumeratesigners` and `walletdisplayaddress` in builds with
`ENABLE_EXTERNAL_SIGNER`. The selected daemon must already have its external
signer command configured and the selected wallet must report external signing
with private keys disabled. Roots GUI requires authenticated RPC capabilities
for node genesis, wallet identity, public `listdescriptors private=false`, and
the named arguments for the selected operation. If that public RPC is blocked
by the daemon, choose file or QR PSBT exchange.

Receive and signing review offer explicit Rescan and selection. Discovery admits
at most 16 distinct signer records with eight-digit hexadecimal fingerprints and
labels limited to 96 printable ASCII bytes. The current daemon routes address
display and signing to the sole enumerated signer, with no wallet RPC argument
to choose a fingerprint. Roots GUI accepts one reported signer matching the
active ranged single-signature descriptor branches, and checks the wallet,
descriptor policy and signer again before dispatch and after a successful
response. Multi-device, mixed or unsupported policies stay on the file or QR
route. Device selections are transient and expire after two minutes. The RPCs
do not attest to a device serial number or eliminate the interval between the
GUI check and the daemon's own enumeration.

Verify on device submits the exact selected receiving address after matching
its script and derivation against the active public policy. The daemon's echo
is an observation. The user still compares the address on the device. Sign on
device submits the approved PSBT with `sign=true, finalize=false`, verifies
the returned signature material independently and retains the original PSBT.
The PSBT must contain the public derivation metadata the signer needs. Enrich
PSBT is a separate read-only action. Device PIN and passphrase prompts belong
to the external signer or device. Some daemon reply paths collapse signer
errors into a generic failure, so only explicit supported error reports show
locked or passphrase-required states. Cancellation retires local authority
and ignores late results. The daemon may finish work it already received.

Linux RPC transcripts and actual Linux NativeHost views exercise a controlled
emulator. Physical model, firmware and backend qualification require separate
evidence. The GUI uses the daemon RPC boundary for device actions. File and
supported QR PSBT exchange use the independent local verification path.

The connected-wallet workspace now implements authenticated connection testing,
bounded read-only refresh, exact history, public descriptor-derived addresses,
coin inventory and local public metadata/recovery. Public descriptor enumeration
never allocates a receive/change index. Local protection/GUI privacy lock, daemon
encryption and external daemon coin locks are separate capabilities. Scoped
Freeze and temporary draft reservation observations require acknowledged
metadata plus current wallet/chain/lock evidence; missing evidence stays Unknown.

Send Selected pins every exact outpoint and reason into an inspectable draft.
Explicit fresh preflight may acquire an owned GUI-instance reservation; it is
not daemon exclusion or funding approval. Wallet creation and opening require
a verified connection and explicit wallet selection or name. Public descriptor
or extended-key import opens a verified watch-only wallet. Compatible daemon
wallet restore and backup use explicitly confirmed paths on the daemon host.
Receive can explicitly allocate the next address or build a bounded offline
pool when the verified daemon supports allocation. The offline pool consumes each
reserved entry once from the same wallet scope. Advanced Settings verifies and
rescans an explicit block range for the selected daemon wallet. Wallet birth
time, keypool counts, and descriptor derivation ranges are read-only daemon
observations. Stopping the local wait leaves an unconfirmed scan record. A fresh
check and explicit acknowledgment are required before choosing another rescan.
Offline drafts use exact pinned public prevouts, cached policy evidence and
explicit fees to prepare unsigned PSBTs. The local service checks externally
supplied signatures against the actual transaction and can finalize supported
satisfied policies. Approved daemon signing checks the compatible live wallet
and unfinalized request arguments. Wallets with only public keys can enrich a
PSBT. The daemon signing route needs locally held daemon keys and an exact
supported policy. Finalize transaction assembles the exact approved collection
locally after verifying every input. It measures the final bytes and derives
the fee rate from independently verified previous outputs. The final wire bytes
can be saved or inspected offline. Daemon unlock, signature collection,
finalization and submission are distinct actions.

Check node policy tests the exact currently approved final bytes against the
selected online node. Broadcast is available after a fresh successful check and
repeats that check before submission. Both calls use a 100 sat/vB ceiling.
Submission also sets the node's burn limit to zero. It requires proven bounded
RPC arguments, current wallet/input policy and a durable local attempt record.
An acknowledgment identifies the submitted transaction. Confirmation and
payment status remain unknown.

Submission Attempts reopens durable records for inspection and a deliberate
read-only Check Status. Wallet, mempool and chain answers are matched against
the exact recorded bytes. A repeat still requires live approval, the same
final transaction and an explicit Rebroadcast same bytes action.

Monitor transaction checks the selected record on the verified node while its
view is open. It distinguishes exact mempool and active-chain observations,
historical confirmations removed by a reorg, and independently verified
competing spends. Offline and stale observations are marked as cached.
Explorer access
requires deliberate permission; no refresh/restored draft replays an action.

### Increase Fee

Select an outgoing payment in current wallet history and open Fee actions.
Enter a fee rate in sat/vB (up to 100, with three decimal places), then choose
Increase Fee. The GUI uses the wallet's schema-verified `psbtbumpfee`
operation to prepare a separate unsigned draft. The original payment and its
history remain available. Review and approve the new draft before requesting
signatures, finalization or submission.

Increase Fee requires the exact original wire in the current node's mempool,
explicit replaceability signals, and current descriptor-wallet history,
coin and lock observations. If the node withholds public descriptor RPCs,
Increase Fee is unavailable. Transactions with descendants, competing spends,
or unsupported previous-output or change proofs are refused. Recipient scripts,
amounts and relative order are preserved. The original inputs are retained.
Additional inputs must be independently matched confirmed wallet outputs that
pass the current local coin policy. The GUI shows exact original and proposed
fees and change. Signed size and the realized fee rate become known after
finalization.

Preparation can allocate a public change address in the daemon. If a reply
is lost or no longer current, the GUI leaves the original intact and discards
the proposed draft. Reconnect, refresh and public recovery never replay the
fee request. Recovered PSBT bytes still require independent proof and confer
no signing authority.

### Attempt cancellation

Fee actions includes Attempt cancellation for an eligible outgoing payment.
Read the warning and choose Create competing self-payment. Preparation uses
an existing wallet address established by independent public descriptor
derivation. It builds an unsigned PSBT locally from all original inputs, with
the entire remaining value in one self-payment after the fee. It makes no
wallet changes. Inputs needing separate unproved redeem or witness scripts
are refused, as are payments outside the supported replacement policy.

The original can confirm first. Node policy can also refuse the competing
transaction at submission time. The draft preserves the original payment and
shows the new destination and amounts. Review, approve, sign, finalize and
broadcast remain separate choices. Approval, daemon signing and submission
verify that the exact original is still eligible and the destination still
belongs to the current wallet. The original can still
change after a check. A competing draft, policy acceptance or submitted
transaction does not guarantee cancellation.


### Increase effective fee

Open Fee actions for an unconfirmed parent, select Increase effective fee and
choose one owned output. The rate entered in the fee panel targets the whole
known package. The app prepares a separate unsigned child paying the remaining
value, after the exact child fee, to the selected output's existing verified
wallet address. Preparation does not allocate an address or ask the wallet to
sign. Native Segwit and key-path Taproot single-key inputs are supported when
the exact descriptor, coin rules, parent bytes and current output can be proved.

The app verifies up to 16 unconfirmed transactions including the parent, at
most eight levels and 96 input links. It counts each ancestor once, even where
branches share one. It matches raw witness identities and reconciles base fees
with verified inputs and outputs. Missing ancestry or node policy leaves the
package unavailable. The display uses base fees and BIP141 virtual sizes for
effective rates. Node cluster limits use separately reported policy-adjusted
sizes. The unsigned child and combined sizes are estimates. Finalization uses
the exact child bytes and recomputes the effective rate against the verified
ancestor snapshot.

Approval, signing and submission recheck the parent output, ownership and
package. A confirmed, spent, replaced or otherwise changed package can retire
approval while preserving the draft. The own-transaction fee cap still applies.
The final node check covers signed transaction and package policy, including
dust and admission. Only a separate Broadcast action can submit.
