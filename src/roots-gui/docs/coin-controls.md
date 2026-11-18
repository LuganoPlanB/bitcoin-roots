# Scoped coin controls and pinned drafts

The application coin-control owner separates three states:

- Local address/outpoint freeze preferences are durable GUI metadata. They use
  public origin/genesis/actual-wallet scope, acknowledged CAS and the existing
  encrypted-store adapter when workspace protection is enabled.
- Temporary reservations belong only to a live pinned draft in this GUI service
  instance. They are not persisted ownership, daemon locks or cross-process
  exclusion. A collision never steals another draft's reservation.
- `listlockunspent` is read-only external daemon-lock evidence. This GUI never
  calls `lockunspent` to acquire/release a lock, regardless of presumed owner.

Local-policy authority additionally binds the current authenticated backend,
actual wallet, genesis, document/query revision, lock epoch, generation and
inventory sequence. Durable presentation identity never substitutes for that
live scope. Missing current inventory, lock observations or malformed public
locking scripts remain Unknown. Rule construction uses bounded identity sets;
it does not scan every saved target for every coin. Values remain checked int64
satoshis, and chart coordinates confer no transaction authority.

Outpoint context actions and Coin actions save Freeze/Unfreeze explicitly.
Failures keep previous acknowledged preferences, report the write/CAS error and
require Reload policy followed by a deliberate retry. No redraw retries a write.
Address context/actions require a current public-descriptor snapshot with exact
parent/ownership, descriptor hash/index, genesis/wallet and tip proof. Saved
address references include descriptor/index plus the canonical public-script
hash and exact address. Matching excludes exact public script identities in
that wallet scope, without branch/prefix/label inference. Unknown legacy target
formats are preserved and fail closed rather than silently discarded or guessed.

The UTXO footer Select All includes observed/retained rows and preserves existing
missing selected identities. Its4096 limit rejects the whole enlargement instead
of choosing a prefix. Clear always removes the presentation selection, including
unavailable/missing/hidden selections. It does not rewrite a separate pinned
draft. Context actions and exact clipboard/CSV/label behavior keep their existing
privacy, scope and acknowledgement rules. Frozen/immature/unavailable rows remain
inspectable; cached current state is never promoted into fresh spendability.

Send Selected creates a bounded transaction draft containing **every** selected
outpoint, observed value/public script when available, original reason and exact
source wallet identity. It never drops an immature, frozen, reserved, spent,
locked or Unknown input and never substitutes another coin. The ordinary32-tab
limit returns a recoverable result and retains selection. Pins remain public
editor data; merely pinning/restoring them grants no reservation or execution.

Validate pinned inputs explicitly requests a fresh read-only wallet/lock refresh.
It binds the requested generation/identity/draft revision and requires an advanced
history sequence, current chain state and fresh metadata reload. Cancelled,
failed, offline, changed-epoch/profile/document or timed-out results cannot
acquire ownership or trigger retries. Every pin gets its current reason. Exact
pinned value/script disagreement, disappearance, external lock, unproven policy
or another draft's reservation denies the entire acquisition. All identities
stay pinned for inspection before any future funding boundary.

Only a fully eligible exact set gets an owned local reservation. The owner token
is private live service state, bound to the source context and draft revision;
wrong-owner releases fail. Explicit Release, Clear draft, close, changed source
scope/revision, offline retirement and privacy lock release only this service's
own local leases. They never unlock daemon coins. Public drafts survive release.
Restart/recovery starts without an owner; a new explicit fresh validation is
required. Clear pinned draft archives public data when recovery is enabled and
changes it only after acknowledgement. A failed Clear keeps pins and the owned
reservation with a scoped visible error and Recovery options, without a retry
loop. Startup unlock does not skip the deliberate public Restore choice.

History and address/coin views consume the scoped policy after acknowledgement.
Local eligibility, daemon safety/locks, reservation ownership, signing custody
and eventual funding approval remain separate. Funding/construction/signing/
broadcast services are later work: this typed draft/preflight seam reports their
unavailability and contains no executable/restored transaction commands.

Focused fixtures cover protected restart freeze, CAS failure, address/outpoint
scope, canonical script identity, malformed evidence denial, reservation
collision/ownership/release/clear/close/recovery/privacy/cancellation and exact
unavailable pins. Engine uses production context/selection/draft/label/recovery
surfaces. The owned Roots native960x640/150% journey independently exercises
native freeze/unfreeze, two exact selections, reservation/release and denial
after an explicit test controller externally locks one pin and spends another.
Controller signing/broadcast setup is not an application signing/broadcast
service qualification. Application RPC remains read-only, the external lock is
retained, and full target-wallet descriptor listings are equal before/after.
Core runtime remains UNEXECUTED. Physical assistive devices/GPU/IME/mixed-DPI,
CJK/general-script font gaps, other platforms/keychains, real portal/browser
and parent full-suite/performance qualification retain their separate gates.
