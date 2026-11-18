# Read-only descriptor address inventory

Addresses belongs to an explicitly tested/applied existing daemon wallet. The
inventory has its own bounded read-only worker and eight full-identity caches.
Backend/session, genesis, actual RPC wallet, document/query revision, lock epoch,
generation and sequence fence adoption. Offline stops transport; lock cancels
and drops caches/UI/semantic state. Failures retain dated acknowledged rows.
An error-bearing or empty result cannot replace acknowledged data.

The required public APIs are `listdescriptors(private=false)`,
`getdescriptorinfo`, `deriveaddresses` and `getaddressinfo`. This Roots build
also gates public descriptor listing behind an operator startup option. The GUI
reports unavailable capabilities; it never enables that option, changes a
service/profile, launches a daemon or substitutes a private listing. The native
fixture opts in only on its disposable test-owned daemon behind a read-only
proxy. Core runtime remains UNEXECUTED.

Descriptor strings must be bounded canonical public parser results with
`hasprivatekeys=false`. Active `listdescriptors.internal` is the authoritative
receive/change classification. **`getaddressinfo.ischange` is not a branch
identity**: current wallet source uses an address-book heuristic, including for
unused receive addresses. Each derived address must instead match the exact
public `parent_desc`, wallet ownership and public locking script evidence.
Inactive/unclassified, fixed, multipath and one-to-many derivations are explicit
unsupported cases; no branch or synthetic derivation index is invented.

Ranges and next-index aliases are checked as canonical integers. Range endpoints
are inclusive and at most2147483647; next may additionally equal end+1 when
exhausted. Default pool windows contain at most8 indexes per descriptor around
the observed next index. Up to16 descriptors and128 derived addresses fit a
batch; the shared read deadline is10 seconds. Full declared ranges, next indexes
and shown windows remain available in the bounds tooltip. Enumeration calls no
`getnewaddress`, `getrawchangeaddress`, import, rescan, allocation or reservation.
Look-ahead means an index at/after the **observed** next index, not a reserved or
guaranteed never-used address. Descriptor lists are re-read to detect pool changes
during the batch, and genesis/tip are checked again before publication.

Known transaction output addresses can supplement the bounded pool window.
For at most64 observed addresses per read, ownership and the exact parent
public descriptor are checked. A bounded unhardened index candidate from wallet
origin metadata is accepted only after a single-index `deriveaddresses` proof
returns that exact address within its declared range. Unsupported/missing origin
metadata, failed proof and bounded-out observations are counted as unmapped;
their identities/usage remain Unknown. They never become an inferred branch or
index. No arbitrary-chain discovery or unlimited address-index claim is made.

Association uses exact public scriptPubKey and txid/vout, never labels, address
prefixes, amount guesses or array positions. Confirmed decoded inputs establish
observed spends; pending inputs retain availability uncertainty. `listunspent`
provides scoped observed unspent evidence and its public prevout value/script.
Absent history, missing prevouts, missing decoded scripts and contradictory
script/outpoint observations remain Unknown. Transaction counts are distinct
parents, with an explicit lower-bound marker when history is incomplete. A
wallet-known address balance is shown only with complete covered history and
stable inventory. It is not final spendability, custody or a signing capability.
Acknowledged [local policy](coin-controls.md) annotates exact matching outputs;
missing policy/current evidence remains Unknown. It confers no funding authority.

The two stacked Receive/Change tables preserve derivation order, stable
public-descriptor/index selection, independent scroll positions and output
expansion. Header options group outputs, hide only proven empty used addresses,
and show transaction counts. Unknown rows and unused look-ahead are retained.
A keyboard/pointer/semantic resizer keeps both panes bounded and publishes its
actual numeric percentage and limits. Long identifiers remain exact in tooltips,
copy and semantic data. Labels are dated daemon observations, not editable fields
or unacknowledged local writes. Amount hiding conceals values, labels and counts
and disables financial filtering. The existing shell status remains unchanged.

Version3 public document recovery adds only these presentation options,
selection/expansion, split and pane positions. Versions1/2 remain readable.
The existing opt-in/encrypted/CAS recovery path owns persistence. No descriptor
snapshot, live capabilities, credentials, requests, reservation or transaction
command is recovered. Address actions save only exact public-script exclusions
through the coin-control owner; this screen confers no transaction execution authority.
