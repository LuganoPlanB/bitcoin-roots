# History and unsigned construction extensions

The shipped profile uses authenticated Bitcoin RPC for the selected wallet and
the local protocol provider for supported unsigned PSBT workflows. The shipped
extension registry has no history index or optional construction client. Server
names the missing dependency for Private index and Public index, with their
connection fields and Test/Apply unavailable. Send identifies the missing
**Avoid partial spends** strategy. The existing RPC Efficiency path and public
offline construction remain available when their own requirements are met.

## HistoryIndexAdapter

The owner binds each read to a backend, exact Bitcoin genesis, wallet and
document, lock epoch, applied session/profile and query revision. The subject
is one exact output or one canonical public script. The owner supplies the
current verified tip and explicitly permits the query. Selecting or editing a
connection view does not initiate a read.

A linked index client owns verified endpoint identity, authenticated transport,
exact backend and genesis verification, request bounds, cancellation and a
stable implementation identifier. The session envelope is for local matching.
A client sends only the necessary public subject data and protocol parameters.
Private and public index operators can associate subjects, network information
and query timing. A future client needs its own explicit configuration and
permission journey and end-to-end verification before it enters the shipped
profile.

The consumer matches the exact owner, query, provider, network and observed
tip. It accepts observations within two minutes of the current clock and limits
each reply to 256 rows and 2 MiB of transaction proofs. It decodes each creation
transaction, verifies the exact output and script, and corroborates any reported
spend by checking the spending input. A partial range or continuation stays
partial. Providers can attest full coverage at their specified tip, with that
provenance attached. Wire relations alone do not establish active-chain
inclusion or index completeness. Unreported outputs and missing spends stay
Unknown. A retired query has no result for the current owner. Indexed results
are kept separate from wallet observations.

Wallet history covers available observations of the authenticated wallet.
Results can be incomplete, stale, pruned or offline. A daemon txindex flag, a
local wallet cache and retained getrawmempool fee samples do not provide
arbitrary address or output history. The fee panel names the sample source and
observation time.

## TransactionConstructionAdapter

The provider uses public preparation data. Its immutable intent binds the
selected wallet and network, document/draft revision, profile/session, policy
revision, exact tip, fee rate, eligible confirmed inputs and ordered fixed-amount
recipient scripts. The owner specifies a maximum absolute fee and independently
proved change, when used. An exact input selection stays exact. The extension
contract supports fixed recipients. Existing RPC Max keeps its preparation path.

**Avoid partial spends** selects all eligible outputs of a chosen script within
the verified inventory supplied for that draft. That can add inputs, raise fees
and publicly link those outputs. No strategy guarantees anonymity. Address
grouping in the UI changes presentation.

The consumer independently parses the exact unsigned PSBT and checks the
current scope, provider, network, tip and freshness. Each input must match a
confirmed eligible outpoint with independent previous-transaction and descriptor
proof. Recipient order, scripts and values must match intent. The exact
satoshi fee must reconcile and stay within the cap. Change must be at the
expected position, with independent public descriptor proof. Unsigned source
bytes and opaque maps remain intact. Validation confers no custody or signing
authority and does not establish the realized signed fee rate.

Staging requires the current owning scope. A changed strategy, source bytes,
proof or provider observation retires the previous review and approval.
The proposal returns to explicit review. Signing, finalization and submission
keep their separate guards. Provider material carries no recovered authority.

Capability-on tests inject isolated instances into the same registry and
validation entry points. The actual Linux NativeHost journey exercises the
shipped unavailable views. Live extension implementations require separate
qualification.
