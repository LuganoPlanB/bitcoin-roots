# Wallet history observations

Wallet RPC covers the selected wallet. Retained mempool samples describe
observed fee conditions. Arbitrary script or output coverage requires a
separate [history index client](history-and-construction-extensions.md).

The existing read-only refresh worker consumes `listsinceblock` and enriches
each distinct transaction ID with one `gettransaction` call. Cache identity
includes backend session, genesis, actual daemon-wallet name, document/revision,
connection session and lock epoch. One transaction has one parent per wallet;
its send/receive outputs remain separate children. A transaction shared by two
wallets has independent accounting in each wallet. No account grouping exists. Genesis is verified again after the batch;
a backend that changes networks cannot publish rows under the old identity.

`gettransaction.amount` excludes the fee. The wallet delta is that amount plus
its optional negative fee **once**, even when several send details repeat the
fee. A self-transfer therefore changes the wallet only by its fee. Immature
coinbase credit is zero in the top-level result; its detail contains the reward.
The all-funds history delta retains that reward and tracks maturity separately.
Amounts use checked int64 satoshis; original number/string decimal lexemes are
retained, including fee lexemes. No binary floating conversion is involved.

The RPC anchor preserves `getbalances.mine.trusted`, `untrusted_pending`,
`immature`, optional `used`, and signed `nonmempool` separately. Trusted includes
some unconfirmed wallet-created outputs and is not labelled confirmed. In the
observed Roots32.1 source/runtime, `nonmempool` can be negative after a deep
reorg: it is a provisional adjustment, not another available-balance bucket.
It is never blindly added to the total. A nonzero value prevents a claimed
complete running balance while wallet/nonmempool observations remain uncertain.

An optional bounded `listunspent` observation includes immature coinbases when
that argument is proven. Confirmed, mempool and immature coin amounts are
disjoint, and become available only when the inventory reconciles exactly to
the RPC anchor. RPC fundability means mature, safe and solvable outputs, with
reused outputs excluded when the wallet's avoid-reuse evidence applies. The
deprecated `spendable` field is ignored; Roots returns it as true even for public
descriptor wallets. Fundability does not prove signing-key custody.

Final local spendability additionally requires an explicit scoped snapshot of
frozen/reserved outpoints. Unknown policy stays Unknown; it never silently means
no exclusions. `SetHistoryPolicy` accepts such evidence without transport and
rejects mismatching identities or privacy locks. The coin-control owner supplies the
production local-policy snapshot. The coin-control service now supplies one only
from acknowledged metadata, current scoped inventory and explicit daemon-lock
observations. Missing/malformed evidence keeps local eligibility Unknown. Typed
and owned-runtime tests are distinguished in [coin controls](coin-controls.md).

Running balances are chronological by wallet observation time, with txid as a
stable tie-breaker; each value remains attached to its transaction when a later
view sorts differently. Their axis is total wallet-known funds including
immature rewards. An opening zero is used only after an unbounded-by-cursor
wallet-known query fits the declared limits, is stable and not scanning/pruned,
and all active transaction deltas reconcile exactly to the RPC anchor. This
proves the reconciled wallet-known ledger, not arbitrary address/chain history.
A suffix, scan limit, RPC disagreement, active scan, pruned observation or mixed
chain/balance observation leaves running balances Unknown. Authoritative dated
RPC amounts can still be shown with their exact category names.

Removed IDs are re-observed rather than deleted: RPC `removed` can contain a
transaction already re-added to the active chain. Orphan, replaced, conflicted
and abandoned rows remain inspectable and contribute no active balance.
A positive current confirmation wins over historical replacement metadata. Both
confirmed and mempool conflicts are tracked. Pending/immature and unsettled
replacement/conflict observations are revisited; known confirmation/conflict
heights update depths as the chain advances. Removal-event provenance survives
re-addition of the same parent. A failed refresh retains old rows and their
observation date, with Stale/Offline freshness from the refresh service.

Defaults bound enrichment to256 transactions,8192 entries/coins and4096 cached
parents, with a shared10-second refresh deadline and existing HTTP/JSON limits.
The cursor advances only after transactional adoption. A limited batch retains
the unacknowledged delta and cannot advance through the explicit ACK API.
Offline schedules no transport. Lock drops history caches, requests cancellation
and discards old observations even after rebinding a fresh epoch. This service
never signs, broadcasts, allocates addresses or launches a daemon. History is
currently an in-memory observation; it does not overwrite local user metadata
or persist command/approval state. Scan controls follow the bounds below.

Verification uses the existing explicitly selected Roots32.1 daemon in owned
private regtest data directories. Fixture funding/signing/broadcast/reorg RPCs
are performed by the test controller; a recording proxy permits only read-only
calls from the production client. Two daemon wallets, receives, self-transfers,
multiple recipients, replacement/conflict, immature/mature rewards, deep reorg,
explicit fixture rebroadcast, branch reconsideration and offline/lock are
compared to authoritative wallet RPC. Core runtime remains UNEXECUTED. Typed
loopback/model fixtures do not create a Core runtime or arbitrary-chain-history
qualification.

## Transactions view

Test and Apply an existing named or default daemon wallet in Server settings to
register a wallet tab. Registration performs no wallet creation/import and
recovery never reconnects automatically. Each tab binds the proven public
endpoint, genesis and exact RPC wallet name. Empty/long names retain that exact
name alongside a bounded opaque ID. Endpoint IDs use the installed libsodium
SHA-256 primitive over public origin fields only; they grant no authentication.
Credentials and proxy routes are not included in metadata names.

The upper summary and chart occupy equal columns. Confirmed, mempool, immature
and RPC fundability remain distinct; locally eligible values need the scoped
coin-control proof and do not establish custody or funding approval.
The Date/Label/Value/Balance tree sorts and filters parents without recalculating
their chronological balances. Selection belongs to the scoped wallet/txid and
survives refresh and display sorting. Click a chart point or use its keyboard
arrows/Previous/Next controls to select and reveal the corresponding row.
Incomplete histories have no fabricated chart. The chart converts to floating
point for geometry only; its tooltip and text alternative use exact Money.

Expand a parent for input/output observations. Proven `gettransaction.verbose`
provides public decoded references and exact output values; input amounts are
Unknown rather than inferred from wallet deltas. Raw transaction bytes,
unlocking scripts, signatures and commands are not retained. Public locking
scriptPubKey bytes are retained only as observation evidence for exact scoped
address/outpoint association; they confer no transaction execution authority.
Without proven verbose support, wallet output details remain inspectable and
are explicitly labelled as wallet observations. The fee is still counted once
in the parent. Open transaction focuses one read-only scoped reference document;
it does not import, sign or broadcast. At32 open documents an existing reference
can still focus. A new reference stays closed and shows an actionable close-a-tab
message; current work is retained, and retry succeeds after a tab is closed.
The static limit message remains readable with amounts hidden. Other wallet
workflows remain unavailable.

Select a parent to edit its local label in the table. Enter or moving focus
requests a scoped CAS write. Selection/filter changes also retire a disappearing
editor. The activation retains its document/query/wallet/metadata revision and
lock epoch; a changed scope or privacy lock retains the draft without a write.
Local metadata can still be edited in a dated offline/cache view.
The saved value/revision advances only after acknowledgement. On
conflict or write failure the draft stays editable and the error remains visible.
A failed attempt is not retried on every frame or by simply rebasing metadata.
Reload saved labels preserves the draft; edit again or press Enter deliberately
to retry.
Local labels override daemon observation labels without issuing `setlabel` RPC.
Reading absent labels does not create a store; an explicit label save creates
local metadata independently of the document-recovery opt-in. Up to32 pending
editor records are retained per document; clean records can be evicted to admit
another editor. Saved labels remain in the metadata store.

Export CSV snapshots the current filtered/sorted parents with exact BTC decimal
text and chronological balances, or Unknown. Cells use RFC4180 quoting and text
formula prefixes are escaped. A native save chooser creates a new owner-only
file exclusively; existing files are never overwritten. A failed/uncertain write
is reported and may leave a partial file at the chosen path. Retry with a new
filename. A changed identity/revision/date/privacy state discards a pending
export. Exports are never recovered as commands. Hide Amounts masks financial
text and free-text labels, suppresses chart geometry, and disables financial copy
and export in pixels, tooltips and accessibility.

The pinned ImKit data provider has a void edit callback and string-only cells.
This view composes its theme/action controls with public ImGui table, input and
draw-list APIs so acknowledged inline writes, exact readouts and per-cell native
accessibility remain application-owned. It uses no private ImGui API. Visible
rows are clipped with stable IDs and the existing bounded semantic frame.

## Scan and chain states

Loading never assumes an opening zero. A partial scan retains known rows and
Unknown running balances. **Scan earlier** is a deliberate read of wallet-known
history, not a daemon rescan or an arbitrary-address history service. For a
pending limited delta it re-reads the same range with a doubled transaction
budget, up to4096; it never acknowledges or advances that pending cursor. The original limited
delta remains retained until its replacement is adopted; transport failure or
cancellation cannot convert the request into an automatic history retry.
Entries and coins remain bounded at8192 and the shared deadline remains10s.
If a larger attempt adds no parents, further attempts stop with an explicit
bound. At the hard transaction limit the action also stops. A pruned source or
active daemon rescan explains why earlier history is unavailable instead of
requesting a rescan or silently skipping history. Incomplete observations without
a pending delta can explicitly request a fresh full wallet-known query.

Failed, offline and cached observations keep their original date and exact
amounts. Row tooltips and accessibility distinguish the last observed state from
**current chain state Unknown** whenever the chain/history pair is not a current,
stable, complete-range observation. Successful observations show current depths,
replacement/conflict facts and known unconfirmed parents. An unobserved ancestor
is not inferred from an input reference. CSV likewise exports Unknown current
state/depth for dated views. Historical chart/amount readouts remain dated;
no stale observation grants fee-action authority.

Explorer opens require a new **Allow once** decision disclosing the exact public
transaction URL and browser/IP privacy consequence. Selection, document, query,
backend/network/wallet, epoch, refresh generation or observation sequence changes
retire that pending permission; it is never saved/recovered. Hide Amounts, lock
and offline mode suppress the action. Defaults map exact Bitcoin mainnet and
testnet4 genesis identities to mempool.space; unknown forks, custom signets and
regtest have no public default. Provider failure does not affect wallet history.
The host receives a typed scoped public transaction request, never a supplied
arbitrary URL or an executable command. Browser invocation requests are distinct
from verified browser delivery. Tests substitute an owned boundary and make no
real external explorer request.

Fee actions dispatch typed replacement-fee or child-fee **draft requests** only
for the current public selection and verified observation. The draft service is
not connected at this milestone: both return a truthful unavailable receipt and
perform no fee bump, signing, sending or broadcast RPC. Permission and draft
requests are transient and confer no restored execution authority. The existing
View loading-log/Activity drawer remains optional and contains public operational
observations, never raw history/RPC payloads or financial logs.
