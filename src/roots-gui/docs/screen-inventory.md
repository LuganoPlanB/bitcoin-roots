# Screen, command and fixture contract

The canonical register is `../tests/fixtures/contracts.json` schema 2. Its
`action_groups` own service, support classification, effect, transition and
acceptance IDs. `action_details` adds each action's screen, specification
sections, disabled explanation, focus rule and dialog/state-fixture routes.
Resolve these two records by action name; details cannot override ownership.
`screens`, `navigation`, `menus`, `shortcuts`, `dialogs` and `state_fixtures`
complete that same register. These are planned observable contracts, not
implemented services, rendered frames or successful acceptance journeys.
Screen `owner_kind=service` identifies the implementing service, independently
of section/test milestone ownership. The shell belongs to `workspace` (the
shell-layout/documents/commands implementation), not the completed dependency
selection task. Child action services may differ from their containing screen.

Opening a menu, tool or confirmation has no RPC or external side effect. A
group's effect describes its eventual deliberately confirmed operation, not
the act of opening its screen. Selection-sensitive commands expose the actual
missing prerequisite in a tooltip and semantic description. Explanations are
reachable with the keyboard even when the operation cannot run. No generic
"not supported" success placeholder may stand in for required functionality.

## Shell and wallet identity

Use the native frame with File/View/Tools/Help, 34–38px document tabs, content,
and a 36px status region. The six rail items, in order, are Transactions, Send,
Receive, Addresses, UTXOs and Settings. The rail is 180px at reference size and
may compact to 96px; retain labels/tooltips and semantic names. Use 24px content
padding, 28–32px rows and 30–34px controls, Inter 14 and mono 13. Reference is
1200×820; compact is 960×640. Never clip the primary action at compact size or
large fonts. The Plan B semantic theme in the shared design contract governs
colors and density; Sparrow is evidence for hierarchy and interaction.

Each wallet tab binds one daemon wallet identity plus backend/network/session,
not a display label or local filename. Reordering, closing, moving to another
window and changing labels preserve that identity. Transaction/PSBT documents
are independent tabs and retain provenance. Closing prompts for unsaved edits;
it does not unload a daemon wallet. Unload Wallet is a distinct confirmed RPC
operation. Delete Wallet removes explicitly selected local workspace metadata;
it never deletes remote daemon disk data. Rename Wallet changes a local display
name. The account strip is hidden. Add Account explains the missing
`future-account-service`; opening another daemon wallet is not creating an
account. Extended payment codes/payjoin/local custody/privacy/indexing services
remain separate, visibly unavailable dependencies in this RPC profile.

## Screen fields and secondary routes

| Sections | Screen and controls | Secondary interaction |
| --- | --- | --- |
| 1–4,17,20 | Shell/welcome, wallet/document tabs, rail, status, search, summaries, units/theme/privacy | Tab context: move left/right, close others/all, separate window; Go To uses the same six routes. Empty welcome offers New/Open/Import. |
| 5,19 | Wallet setup: name, backend/network, descriptor/watch-only/external-signer/daemon-wallet source, restore/import range and birth/rescan policy | Public descriptor/file/QR import, validation, explicit create/load/import/restore confirmation, progress and uncertain-operation reconciliation. No mnemonic or local private-key storage. |
| 6 | Transactions: balance breakdown, chart, filter/search and sortable history table | Stable txid selection; inspect/open, label, copy/export CSV, RBF/CPFP prepare only when eligible; conflict/reorg/unconfirmed/loading log and pruned/unknown detail states. |
| 7 | Send: address/label/exact amount recipient rows/tabs, add/remove/Max, fee source/rate/absolute fee, selected/excluded coins, change policy, unsigned diagram | Batch grid import/edit; Clear edits revokes approval; Prepare produces unsigned PSBT. Explicit validation reports row/field. Privacy Optimization/Payjoin explain future dependencies. |
| 8 | Receive: address, label, derivation/index, used state, public descriptor/script, QR | Copy, Next reservation/allocation, device Verify and QR detail; label commit distinct from allocation. Unavailable signer never masquerades as verified. |
| 9 | Addresses: receive/change tables, index/path, label, used/balance, grouping/hide-empty filters | Stable address row selection; copy/address QR/descriptor, label, transaction/UTXO drilldown, CSV. Optional indexed spending history unavailable explicitly. |
| 10 | UTXOs: summary/chart, selectable outpoint table, confirmations/value/label/frozen status | Select all/clear selection, freeze/unfreeze, label, copy/export, Send Selected pins outpoints in a new draft; frozen coins stay excluded. |
| 11 | Wallet Settings: policy/script, m-of-n, public descriptor and signer fingerprints/origins | Advanced birthday/range/gap settings; descriptor edit/QR/export; Apply validated service request versus Revert local edits. Add Account future dependency. |
| 12,13,16 | Workbench: Headers/Inputs/Outputs tree, inspector, optional hex split, full destination/value/fee/warnings | Review -> Approve exact identity -> explicit Sign/import/merge -> independently validate -> Finalize -> explicit Broadcast. Per-input completion, conflicts, unsupported scripts and broadcast uncertainty remain visible. |
| 14 | Preferences General/Server: display/provider choices, endpoint/network, cookie or credentials, proxy, timeout | Test distinct from Apply; switch profile creates backend session. Cookie/file picker denotes GUI-host path only. Remote daemon datadir/restore path is daemon-side text, never a local file-picker assertion. No daemon launch/config-file editing. |
| 15 | Tools: Sign/Verify Message, Send To Many, Sweep, Verify Download, hardware setup, power lease | Message address/scheme/content/result; explicit daemon consent; transient sweep capability gate; public trust/hash/signature result; no installer or direct GUI USB/HID. Restart applies GUI network/data-dir profile, never restarts daemon. |
| 18,19 | Privacy lock and recovery operation list | Local Unlock is separate from daemon wallet unlock; lock redacts charts, diagrams, tooltips and semantic text, increments epoch and fences late results. Uncertain requests reconcile by identity, never blind retry. |
| 20 | Introduction, Documentation, Log File, Support, Report Issue, About | Local redacted logs; external links require deliberate consent; diagnostic export omits credentials. |

Field edits, sorting, selection and inspection are local presentation/edit
operations under the screen owner. They cannot dispatch signing/submission.
Every edit affecting transaction identity revokes approval. Max is a draft
calculation, never a send. CSV export and clipboard are explicit local public
data disclosure. QR file/static exchange is required; camera has an actual
platform-permission/unavailable state and no fabricated camera evidence.
Copy Transaction ID belongs to the clipboard service and explicitly discloses
to the clipboard. Show Address QR and Inspect Address belong to address
presentation and never write the clipboard. Send Selected Coins belongs to the
draft service: it pins outpoints and focuses Send, without changing metadata.
CSV export belongs to the export service and discloses to a selected local file.

### RPC funding and offline construction

Prepare Transaction (including RBF/CPFP preparation) is the RPC funding route.
It requires both observed `online` and `capability` facts for the matching
backend, plus valid exact context and an unlocked workspace. Cached data alone
cannot enable this command. Its output records the backend observation
snapshot; it cannot promise that inputs remain unspent at later broadcast.

Construct Offline Transaction is a separate required workbench route, owned by
`public-construction-service`. It requires complete authenticated public
prevouts (outpoints, scripts and amounts), matching network, exact recipient/
fee/change policy and a supported construction policy. Missing public data or
unsupported policy disables construction with the specific missing condition.
It does not require online RPC capability and never claims current spendability.
The result is an independently checked unsigned PSBT with `spendability` marked
`not-observed`; it neither signs nor submits. Import/export, review and supported
signature verification/finalization remain separate public-data workflows.
There is no GUI private-key/seed custody in either construction route. This
inventory declares required behavior without implementing either service.

## Menus, keys and modal behavior

The complete four menu lists and all named row/tab actions are in the register.
Primary means Ctrl on Linux/Windows and Command on macOS. Primary+N/O opens
wallet routes; F/I/T/U opens transaction file/ID/text/QR; S saves the active raw
transaction or PSBT by document kind; comma opens Preferences; W closes the
active tab. Primary+1…6 navigates the rail, R refreshes, B toggles BTC/sats,
Shift+H hides amounts, L/Shift+L locks current/all, Shift+S searches current,
Shift+Alt+S searches all, M opens the Sign/Verify Message tool. Sign is the
initial pane; switching to Verify has no signing effect. Shift+F10 opens the
focused context menu. Escape dismisses a cancellable dialog. Literal Ctrl+Tab
exits the batch grid to the enclosing focus scope on every platform.

Shortcuts dispatch only inside the active scope; composing IME input and text
editing win over application shortcuts. PSBT Save shadows raw Save rather than
running both. Disabled shortcuts expose the same reason as the corresponding
menu command. Tab/Shift+Tab follow visual reading order; table arrows change
stable selection, Enter inspects and Space toggles eligible selection. Modal
focus is trapped, default destructive/sign/broadcast focus is Cancel, and
dismissal returns to the invoking stable control or nearest surviving tab.
After irreversible dispatch Escape detaches presentation and retains owned
operation/reconciliation; it cannot promise that the daemon canceled work.
Validation focuses the first invalid field without destroying entered data.
Loading does not steal focus; completed async work only restores focus if the
same tab, document revision and epoch still own it.

Dialog routes include wallet source/import/export/delete/unload, metadata edit,
transaction file/ID/text/QR/import/merge, save/CSV, QR display, unsaved edits,
advanced settings, device verification/setup, signing/sweep consent, broadcast,
search, local unlock, camera permission and missing-service explanation.
Native file dialogs select GUI-host files; remote operations require explicit
daemon-side path semantics and confirmation. A disconnected wallet can still
inspect/export public documents, but cannot pretend that cached spendability,
signer availability or broadcast status is current.

## Fixtures and evidence obligations

Each screen has all eight normal/empty/loading/error/stale/offline/locked/
unavailable scenarios in the register, with observable presentation and allowed
command outcomes. Action details reference the screen scenario, while inherited
group fixtures retain acceptance ownership. Screen-specific data must be added
by its implementation owner: fixed public values, dates, source/backend/network,
stable row IDs and pending request identities. The current validator checks
completeness, negative malformed inventories, focus routing and side-effect
separation; it does not exercise a GUI dispatcher or a daemon service.

Linux production evidence is required at both sizes, Light/Dark, 100/150% fonts
and 200% stress, with reachable primary controls, keyboard/IME, native dialogs,
screen reader semantics, camera/device capability and mixed DPI as applicable.
Unavailable native capabilities need actual observations and honest limitations.
Windows/macOS native frame, Command conventions, dialogs and DPI/Metal evidence
are follow-on obligations and cannot count as Linux passes. Extended profile
requires its own services, fixtures and release evidence. No native rendering
or platform qualification has been performed by this inventory milestone.

## Read-only Sparrow evidence and licensing

Reference: sparrowwallet/sparrow at
`ddb486dd37ddfa4071e95a81e5c78298433073b6`. `app.fxml` supplies the native
menu/tab hierarchy and transaction-open accelerators. Under
`src/main/resources/com/sparrowwallet/sparrow/`, `wallet/wallet.fxml` has the
rail/stack; `transactions.fxml`, `send.fxml`, `receive.fxml`, `addresses.fxml`,
`utxos.fxml`, `settings.fxml` and `advanced.fxml` supply the control hierarchy
listed above. `transaction/transaction.fxml` supplies tree/inspector/hex splits;
`settings/{settings,general,server}.fxml` supplies preferences organization.
Controllers under the corresponding Java path confirm behavior:
`wallet/WalletController.java:80–150` selects/lazily loads a section and gives
Settings an editable form; `SendController.java:1195` opens a created PSBT;
`ReceiveController.java:325–370` separates address refresh from device display;
`SettingsController.java:655–686` separates export from Add Account.

These observations inform hierarchy and workflow, not permission to adopt
Sparrow's local custody, Electrum transport, HWI or account model. Sparrow is
Apache-2.0 licensed; its LICENSE was inspected. No code, FXML, image, font or
other asset is copied here, and no Java dependency/build is introduced. Any
future licensed copying must preserve its applicable attribution/notices.
