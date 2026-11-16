# Workspace shell foundation

The production `platform::Run` composes `ui::shell::DrawWorkspace` inside the
existing decorated GLFW/OpenGL host. It starts with an empty registry. Public
wallet/transaction fixtures are explicit gallery/test inputs, never automatically
opened daemon wallets. No command in this foundation starts a daemon, holds
private keys, signs, broadcasts, opens an external page or invokes an RPC.
The default NativeHost bootstrap composition remains available to its existing
lifecycle/appearance tests; the actual application Run entry uses the shell.

## Regions and identity

The native frame owns window controls. The client body has File/View/Tools/Help,
a 36 design-pixel document strip, a navigation/content body and a 36-pixel
status strip. The optional account region is represented by a zero rectangle
and is not submitted. Content starts exactly at the actual rail's right edge:
no slot, spacer, label or invented account is allocated. A future account
service must supply its own multiple-account state before that region exists.

At 1200x820 the rail is 180 design pixels; below 1100 logical window pixels it
is 96. All design metrics scale once with the host font/DPI scale. Content has
24-pixel horizontal padding; controls retain the shared 32-pixel metric and
table rows have a 28-pixel minimum. Tabs occupy 36 pixels with 34-pixel hit areas. The existing focus wrapper
uses an optional foreground draw layer to keep its unchanged ring/gap geometry
visible beyond the tab child clip. Ordinary actions retain their original layer. Rail items have equal height and
a minimum; overflow scrolls at stress scale. Labels remain visible (including
compact label sizing), with six original stroke icons above them. The exact
order is Transactions, Send, Receive, Addresses, UTXOs, Settings.

The pinned ImKit NavigationRail delegates to the inline StepNavigator and cannot
express this icon-above-label layout. WorkspaceTabs collapses overflow to a
combo and parses label markup. Public ImGui children/buttons, literal DrawList
text and the existing focus-wrapped ImKit actions preserve the required tab
strip, literal names and full-name wrapping tooltips instead. No vendor/pin
change or private ImGui implementation is used. Shared forms/status/actions
remain ImKit compositions; fixed-width native tables preserve horizontal
scrolling, literal `##` text, keyboard selection and exact amount projection.

DocumentRegistry assigns monotonic nonzero presentation identities. A wallet
binding includes the daemon session, WalletId and network identity; opening an
existing binding selects its existing document. Another daemon's same wallet
name is a separate document. Each wallet has six independent retained states:
current section, acknowledged note/draft, selected model keys, parent scroll
and table X/Y scroll. Transaction documents own separate state and do not
redirect wallet navigation. State is retained in memory, not a restart/session
persistence claim. No accounts are manufactured from descriptors or wallet names.

New incomplete wallets start Settings. Other sections remain navigable but show
the policy/adapter dependency and disable their primary action without drawing
invented balances. Locked fixture state removes its content and keeps an unlock
dependency explanation. That fixture is not a demonstrated security lock/epoch
boundary; real locking remains later domain/adapter work.

## Commands, presentation and effects

The complete declared File/View/Tools/Help inventory is registered with primary
modifier labels (Ctrl on Linux/Windows, Command placement retained for follow-on
macOS work). Ctrl+1..6 routes sections; tabs work with keyboard activation and
left/right movement. Hide Amounts and the BTC/sats/automatic unit cycle are
immediate presentation changes. Appearance choices use the host's actual saved
Light/Dark/System path, acknowledging save failure; production time stays native.

Other menu routes query typed host availability with document/section context;
registered handlers receive typed requests.
Unavailable services return persistent visible dependency explanations and
leave document state intact. Unavailable routes remain keyboard-inspectable and explain their dependency;
no missing handler receives a fabricated request or acknowledgement. A menu item is a route, not a claim
that its requested service exists. Fixture note/selection acknowledgements are
in-memory only. Shared Money/FeeRate contracts are unchanged; the shell table
formats BTC or integer sats only after privacy permits it. Hidden sats and BTC
text/tooltips/annotations are checked. Frozen rows remain fully opaque with an
explicit textual availability state. No financial value is interpolated.

A small fixed heading summary keeps loading/empty/error/stale/offline/locked/
incomplete status visible while inspection content scrolls. The status strip
retains activity on the left and device/proxy/update/network/connection text on
the right. Activity/status drawers are described below; native accessibility publication
remains a later child gate; amount annotations do not imply a working screen reader.

## Verification and remaining work

Five production Test Engine journeys cover binding deduplication and incomplete
creation, all-section mouse state retention, exact independent parent/table
positions and selection, wallet/transaction keyboard isolation, menu/shortcut
requests and empty-runtime dependency behavior. Fourteen geometry/privacy
checker probes reject account gaps, wrong widths/heights, offscreen primary
controls, wrong targets and hidden BTC/sats leaks.

The native selection is explicit: six section cases plus loading, empty
observations, error, stale, locked, hidden sats, a long translated literal wallet
name, incomplete Settings, incomplete Send, a transaction document and the empty
workspace. Each runs at 1200x820 and 960x640, Light/Dark, 100/150/200%: 204
scenarios. Real native rail clicks and the hide shortcut exercise production
input; table wheel gestures verify an actual horizontal position change. The
wrong-target probe clicks a different real section and must fail exactly its
target assertion. Framebuffers, geometry, snapshots, fonts/source/binary
identities native transaction-tab keyboard focus and process teardown are retained outside Git.

The shell layout child establishes spatial operability; document operations and
command/status behavior are described below. Native macOS menu placement remains
a follow-on validation gate. Screen-specific wallet/transaction workflows remain their
later milestones. Native Orca/AT-SPI/IME/mixed-monitor/physical-GPU and macOS/
Windows qualification gaps remain explicit. No partial shell is a release claim.

## Host appearance status

NativeHost publishes its actual current appearance choice, invalid-load status,
save rejection and unavailable-System status in every callback frame. This is
independent of a color/font rebuild: losing desktop capability while already
Light must still update the visible warning. DrawShellFrame is the shared
production Run/native-test composition, so appearance requests and their actual
save acknowledgements use the same path. The shell presents warnings visibly
with owned status annotations, including the empty runtime workspace. Menu
checkmarks reflect the saved host choice, including System during Light fallback.

Invalid preference data remains untouched until an explicit successful choice;
unavailable System uses Light and retains the saved System ID. A rejected save
retains the previous choice and displays retry instructions. Successful retry
clears only the host appearance errors; unrelated command explanations remain.
The read-only desktop adapter remains the production default. A scoped optional
capability reader lets native tests exercise missing and live-changing desktop
capability without OS-setting changes or manufactured UI error flags.

The native shell-appearance regression covers six actual hosts and thirteen
phase captures: invalid-load/replacement, unavailable System/explicit Dark/
System again, live known-Light/lost-Light/known-Dark/lost-Dark capability,
blocked pending write/retry, and explicit Light/Dark with unavailable desktop
capability. It validates actual files, current public host state, production
rendered text/annotations, complete warnings and primary reachability, including
960x640 at200%. The original failing invalid-load executable/transcript remains
in execution evidence; the older three desktop appearance scenarios are retained.

## Document operations and empty workspace

Tab identity is the registry's nonzero ID, independent of position, display name,
icon and binding. Right click a tab, or focus it and press Shift+F10, for explicit
Move Left/Right, Close/Others/All, Rename/Icon, Reset Name/Icon, Lock, Delete and
Separate Window routes. Arrow keys select adjacent tabs; move commands reorder
the same model objects and preserve every section draft, selection and scroll.
Full literal names (including `##`/`###`) are painted independently of widget
identity and remain in wrapping pointer/keyboard tooltips and owned tab
annotations. These annotations are not a qualified native accessibility bridge.

Rename/icon and reset are local metadata requests carrying stable document ID,
name and icon. They apply only after a host acknowledgement. Failed requests
retain the old metadata and the dialog draft. Production has no durable metadata
adapter yet. Test fixtures acknowledge their explicit in-memory service; a
fixture acknowledgement is not evidence of a saved database or directory rename.

Close removes views only. Clean views close immediately; any dirty note across
all six sections or transaction state opens cancellable recovery for the whole
requested set. Cancel/Escape returns focus and retains documents, drafts and
selection. Save and Close requires an acknowledgement for each dirty document;
failure leaves every view open. Explicit Discard closes only the selected views
and their in-memory drafts. Delete has a separate confirmation and lifecycle
request; this fixture shell never removes daemon wallet files or pretends that
view closure, privacy locking and deletion are interchangeable. Lock still names
its missing privacy/daemon adapter; it does not silently manufacture a lock epoch.

The empty workspace centers a dashed 450x230 design-pixel target, bounded by the
available client area at stress scale, with New/Open/Import actions. Import
stages a public source path without reading the file. Extension hints classify
PSBT, raw transaction and public descriptor candidates; JSON/text require an
explicit choice where ambiguous. Unsupported hints remain disabled. A typed
request includes the exact owned source and selected format; a future import
service must validate contents/network and return actual document data. No
acknowledgement alone manufactures an imported wallet. New/Open currently name
the missing daemon-wallet service. No private-key/seed input is parsed or held.

GLFW's native drop callback copies bounded borrowed paths on the owning thread.
Production drains that queue into the same chooser only for one file dropped
inside the displayed empty target while no modal is open. Outside drops are
ignored and multi-file drops explain the one-file limit. Public ImGui path
payloads use the same chooser. Tests exercise the actual registered callback
with copied/mutated input bytes; an OS drag gesture and native file chooser are
explicitly unqualified. A scrolling dialog body and fixed action footer keep
Cancel and primary choices reachable at 960x640/200%.

Separate wallet windows are optional and explicitly unavailable. The current
native backend enforces one GLFW/ImGui owner on its original thread; the route
explains that limit and leaves IDs, drafts and selection in the current window.
No second native context, copied registry, transfer claim or fake window is
advertised. Native captures record one owner and teardown of each actual host.

Five additional production Test Engine journeys cover fixture New/Open
transaction commands, explicit reorder using keyboard context actions, literal
rename/icon/reset and rejected acknowledgement, dirty-close cancel/failed-save/
discard, separate delete/lock/window routes, close others/all, empty import
ambiguity and invalid identity/close-set negatives. Native document selection
covers empty, normal tabs, full translated names, true hover/held press/target
focus, close/rename/delete/import dialogs, rejected metadata, copied native drop,
locked and hidden states: 14 variants x 12 size/theme/scale combinations =168.
Loading/error/stale/offline remain in the renewed shell204 matrix. The target
focus case uses actual click/right/left native input; a deliberate wrong-target
probe must fail exactly its target assertion. Fixtures never perform RPC,
signing, broadcast, daemon launch, actual import or durable wallet deletion.


Current-frame tab publication owns both names and values, independently of the
mutable document registry. Each tab annotation follows its actual button item
and matches that exact rectangle, including the first tab. A same-frame rename,
close or reorder cannot change the already submitted frame's identity text.
Six publication regressions retain long allocated literal names through those
mutations and through an actual acknowledged dialog rename before Snapshot;
they compare exact names/values, stable node IDs and each actual tab rectangle.
Context-driven close/reorder also submit each displayed identity once even when
the registry changes mid-traversal. These checks do not establish later
immutable native-tree/action-queue bridge gates.


## Menu routing, activity and service observations

All55 File/View/Tools/Help routes remain keyboard inspectable with all23 declared
primary shortcuts exposed beside their labels. Ctrl is used on Linux/Windows;
Command chord/label mapping is separately checked, while actual macOS native
application-menu placement is unqualified. Context requirements are explicit:
wallet-only and transaction-only routes reject the wrong document, and a modal
must finish/cancel first. Host CanSubmit is a capability query, distinct from
Submit acknowledgement. Missing handlers return their actual dependency without
sending a command; rejected acknowledgements retain the workspace. Local view,
section, appearance and document/dialog actions retain their existing ownership.
Production currently registers only its actual Quit handler plus local UI routes;
fixture handlers are explicit and effect-free, never real RPC/signing/deletion.

Status no longer invents proxy/update/network facts. Each frame observes typed
service states for devices/proxy/updates/network/connection, optional backend
height, last success and error, and a separate activity feed with stable task IDs,
optional checked progress-per-mille and public operational log entries. Defaults
are Unavailable, not assumed Offline/Ready/disabled services. Status uses semantic
color plus state text. Activity exposes current progress/count and opens its log;
Status opens all service details. Missing, loading, error, stale, offline, locked
and known-empty observations remain distinct. No operational port accepts Money
or raw RPC/financial payloads; host free text in status/tasks/logs is concealed
before visible/tooltip/annotation presentation while Hide Amounts is active.
Progress conversion is display geometry only and never monetary edit parsing.

The drawer overlays a bounded part of content and retains tabs, navigation,
workspace data and status actions. Close and Escape return focus to Status;
scrollable body content cannot move Close outside the viewport. ImKit's pinned
DiagnosticsDrawer is an inline child without the needed overlay/return target;
public ImGui window/child composition reuses ImKit TaskProgressCard/theme actions.
The optional second native wallet window remains unavailable; this drawer is
inside the existing GUI context, not another native window. Ctrl+Tab is routed
only from the inspection grid, returns to the current stable tab and explicitly
reveals its keyboard cursor through the public API. The shared button wrapper
preserves current public cursor state rather than restoring the cached
start-of-frame NavVisible output, so later buttons retain that handoff. An
actual pointer activation still supersedes the prior field cue and retains
pointer mode. It does not steal note input
or alter drafts/selection. Tab Shift+F10 and document Escape contracts remain.

Five command Test Engine journeys exercise all55 routes with keyboard activation,
all23 real Ctrl chords and portable Command mapping, capability/acknowledgement
negatives, live observed status, retained background state, privacy and grid
escape. Native menu/status/activity selection covers all four menus (including
keyboard End to their final route), selected privacy state, missing/
loading/error/stale/offline/locked/long service details, loading/empty/error/stale/
hidden activity, real hover/press/focus, grid exit and capability/ack failures.
All run at both sizes/themes and100/150/200%; actual native input, bounded real
deadlines and teardown are required. No future OS accessibility/IME/mixed-DPI/
physical GPU/macOS/Windows qualification is inferred from these fixtures.


Status disclosure actions retain32px hit targets within the36px strip. Their
compact visible frame is inset so the full2px focus stroke and minimum2px canvas
gap fit at every viewport edge. The pinned action has one painted/hit rectangle,
so this case uses public InvisibleButton navigation plus semantic-frame paint;
its built-in cursor paint is locally transparent before drawing the shared cue.
Actual pixel checks cover left/right/top/bottom, full hit/strip metrics and real
target focus. The pre-correction clipped cue and intermediate duplicate native
cursor are retained as failing evidence; no gap/stroke threshold is weakened.
