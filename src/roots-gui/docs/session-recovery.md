# Public session recovery

File → Session Recovery explicitly enables local public-document persistence.
Document recovery defaults off and creates no database merely by being off.
Explicit label/freeze preferences can independently create local metadata. The local file is
`$XDG_DATA_HOME/roots-gui/metadata.sqlite`, or
`$HOME/.local/share/roots-gui/metadata.sqlite` when XDG data home is absent.
The application creates its own private directory; an existing nonprivate
directory/file is rejected. This file is GUI metadata, never a daemon wallet.
Metadata begins with owner-only filesystem access. Explicit
[local workspace protection](protected-sessions.md) encrypts the complete store
and provides privacy-lock epochs; native RPC credential storage remains separate.

When enabled, a successful edit frame checkpoints the current public workspace.
Save public workspace now and Save and Close also require an acknowledged local
write. Each atomic snapshot carries an independent CAS revision. Another
instance's change is reported as a conflict and cannot be overwritten by an
automatic retry. Failures retain the previous snapshot and current documents.
Retry reading recovery rereads the current record and offers a deliberate
recovery decision; it does not merge or replace work silently.

Restart offers Restore public workspace or Start empty; keep saved recovery.
Neither the startup read nor the prompt opens a document or issues an RPC.
Restore reconstructs only public editor state, marks every document Offline,
and grants no capability, approval, signature or live observation provenance.
The saved wallet binding identifies the prior daemon session/wallet/network
only; it is not proof of a current authenticated connection. Any later wallet
consumer must explicitly rebind to a freshly proven backend identity. There
are no synthetic accounts. Two wallets and two transaction documents retain
independent state even when wallet names match on different backends.

The bounded codec restores tab ordering, active tab, current wallet section,
literal names/icons, public note committed/draft buffers, selected model keys,
focus key, scroll/table positions, uint64 draft revisions, exact selected and
excluded outpoints, amount privacy and units. Scroll positions use thousandths
of a logical pixel; financial identifiers and revisions use canonical integer
text. Outpoints preserve their full txid and uint32 index and cannot occur in
both policies or twice within a policy. Any changed saved document advances
its revision without wrapping or changing another document's revision.

The version2 public codec additionally restores history sort/filter/selection,
parent expansion, unsaved label buffers and read-only transaction references.
Version1 snapshots remain readable. Storage acknowledgements, history ledger
observations, live query authority and export requests are never restored.

Clear public draft removes its notes, pending label drafts and coin policy only after storing a
recovery copy. Closing a view also checkpoints before removal and retains a
copy. Recover last Clear / close deliberately restores the most recent copy;
replacing a current draft retains that current version for the next recovery.
The last eight such copies are retained, with the oldest evicted at that bound.
At most32 open documents and524288 encoded bytes are accepted; limits and
invalid/locked records fail visibly and leave the previous snapshot intact.
Recovered closed document IDs remain reserved across restart.

Start empty suspends autosaving for that run and preserves the original saved
workspace. A later explicit Replace saved workspace; enable recovery replaces
it with the current public workspace. Disable and forget saved recovery
atomically clears the record. These operations never unload/delete a daemon
wallet, release daemon locks, fund, import, sign or broadcast. There is no
stored executable-command, approval, credential, private-key, opaque PSBT or
automatic retry field. Transaction parsing/construction and its future typed
public fields remain with their later work packages; this snapshot recovers
the editors currently implemented, not a signed transaction workflow.

Focused tests exercise isolated four-document round trips, exact coins and
large revisions, Clear/close/restart, opt-out, CAS conflicts, strict codec
admission and actual process interruption before/after the save acknowledgement.
The representative native960x640/150% journey uses the production drawing and
native mouse input for the restore/Clear/recovery decisions with zero host
effects. This is owned virtual-display evidence, not physical power-cut,
filesystem, Core runtime or another-platform qualification.

Version3 additionally recovers address-table presentation only: descriptor/index
selection and expansion, grouping/count/filter options, split and independent
pane positions. Version1/2 sessions remain readable. No live descriptor snapshot,
credential, capability, index allocation, reservation or command is restored.

Version4 additionally recovers UTXO presentation selection/focus/sort and local
output-label saved/draft buffers. Versions1/2/3 remain readable. Presentation
selection is separate from draft input/exclusion policy; restoration grants no
eligibility, metadata acknowledgement, RPC permission or executable action.

Version5 additionally restores public pinned-input drafts: exact source wallet
origin/network/name, outpoints, observed values/public locking scripts and original
reasons. Current observations, validation receipts, live reservation owners and
automatic requests are never recovered. Versions1–4 remain readable. Unlocking a
restarted protected store still requires a separate deliberate Restore decision.
Clear/close retain these public drafts through the existing recovery archive.

Version 8 also preserves Send recipient drafts, including the staged Pay to
field, applied address, local label, explicit amount unit, stable recipient
identities and one Max intent. Recovery checks the applied addresses against the
document's network and keeps the editors offline. It restores no funding
observation, reservation or execution authority. Versions 1–7 remain readable.

Version 9 also retains the public fee-rate editor text. Restoring it requires
a new deliberate funding preview after the wallet, policy and coins have been
verified. Earlier recipient snapshots still open offline.

Version 10 preserves a created unsigned PSBT's original bytes, source draft
revision, exact fee, full matched prevouts and any internal change descriptor.
Recovery opens the public inspection offline. Current spendability and change
allocation still require fresh evidence.

Clear Send confirms before resetting recipients, coin choices and fee settings.
Session Recovery can restore the last cleared Send at a new revision while
preserving other tabs and their edits. With recovery disabled, that copy stays
in memory until quit. Recovery restores public pins without a reservation,
funding preview or Create action.

Version 11 also preserves the Send fee target and manual override text.
Fee observations and recorded charts last for the current session and require
the matching verified node and wallet. Restoring a target requires a fresh
observation or a valid manual rate before requesting an unsigned preview.

Version 12 retains a specific change preference by public descriptor and
index. Recovery re-derives its address on the recorded network. A fresh
authenticated descriptor observation is required before the address can be
used in another preview. Clear Send resets the preference with the draft.
