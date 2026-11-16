# Current semantic publication contract

The production shell and shared component composition build an ImKit
`AccessibilityFrame` on the ImGui owner thread. `PresentationFrame::Tree()` and
`Snapshot()` are current-frame diagnostic views, retained for the existing
same-frame document-mutation regressions. They are borrowed and must never be
passed to another thread or used as a native accessibility packet. In particular,
the diagnostic frame can retain background tab annotations while a dialog is
being drawn. The published packet contains only the active semantic layer.

`PresentationFrame::exchange.Published()` returns `shared_ptr<const SemanticPacket>`.
Every name, description, value, child list, role, state, rectangle and numeric
field is copied into that packet. Parent references are validated before child
lists are derived. Retaining a packet across subsequent frames, string changes,
reordering or document closure retains its original bytes. Native readers may
read that immutable packet without accessing ImGui or the document registry.
The ImKit pin and archive remain unchanged. Its public annotations provide item
geometry/focus; its borrowed `ActionQueue`/`NativeActionSink` do not supply owned
values, generations or synchronization, so the application supplies those
requirements in `SemanticExchange`.

Publication permits at most 256 nodes and 65,536 combined text/child bytes.
ImKit overflow, invalid identities/parents/children, cycles, invalid geometry or
nonfinite numeric fields produce an explicitly invalid, empty packet. A scope
transition also produces an invalid packet for that transition frame; the next
coherent frame publishes the new scope. No partial tree is advertised as valid.
The current shell publishes document/section groups, tab lists, live item
rectangles, public observation grids/rows/cells, menus, fields, actions, status,
alerts, progress and dialog controls. The shared gallery also publishes exact
amount/fee fields, identifiers, copy actions, chart text alternatives and public
review destinations. Current transaction documents contain the public fixture
observation grid; there is no invented transaction input/output workbench tree.

External consumers queue an owned sequence, published generation, stable item
identity, one supported action and optional owned value. The mutex protects the
published packet, queue and receipts. `Queue` is cross-thread; drain, ImGui focus,
publication and effect dispatch belong to the bound UI owner. A native adapter
must retain the generation it presented and use that generation when requesting
an action. Binding/publication from another thread fails. Values are limited to
4,096 bytes; the queue holds 32 requests. Duplicate/nonmonotonic sequence numbers,
unsupported/composite actions, unknown/disabled targets, stale generations and
queue/value overflow are rejected explicitly. Owner-thread Close invalidates the
current generation and rejects queued/future actions; application Quit and native
owner lifetime invoke that boundary while retained immutable packets stay readable. The last 128 terminal receipts
are diagnostic history; callers also receive immediate enqueue/rejection results.

Each target is bound when actually drawn, against its current availability,
privacy, document/section, lock and active layer. Binding stages an owned request
and an owner-thread handler; it never applies a semantic edit or calls the host.
`Seal` prepares the complete immutable packet after every annotation, including
annotations after an early control. It validates the entire node/text capacity,
graph, geometry and active layer before invoking any staged handler. Invalid or
overflowed frames reject the staged requests without effects. Scope checks are
additional stale-target guards, not a substitute for complete-frame validation.

During dispatch the public packet is invalid. Each admitted target/action is
checked against the prepared full packet and current scope. Immediately before
each effect, its live route predicate rechecks current fields, privacy, selection,
dialog lifecycle and host availability using the same predicate as native input.
An earlier rejected edit can therefore disable a later primary action without
blocking independent valid edits. Import radio requests retain the format identity,
never a stale index into a changed candidate list. The handler then invokes its
normal validation/acknowledgement boundary. Scope-changing effects retire the
rendered packet and reject remaining stale handlers. No completed host effect is
rolled back. Pointer and native keyboard actions keep their immediate handlers;
a simultaneous native activation is coalesced without a second host call.

Hosts passed to shared controls outlive the enclosing frame. A stack adapter must
override `Commands::Capture` and `CaptureAvailability` with owned endpoints; the shell context adapter
captures the persistent host and immutable document/section identity, never the
adapter object. Model references are used only after scope/identity validation.
External edits use the same exact integer parsing and typed acknowledgement as
Enter/focus loss. Invalid/rejected drafts persist; a request rejected for frame
overflow never changes even an active edit buffer. Public ImGui callbacks apply
accepted edits to the live input buffer on the next draw.

Focus, popup closing/opening and privacy changes use committed UI intents when a
public ImGui API needs the matching next-draw context. Intents are admitted only
after a valid complete frame. Focus realization checks identity/scope and expires
if the target disappears. Privacy is applied before the next view is assembled,
not after financial data has been submitted for that view. These UI intents are
not durable service acknowledgements. External rows, menus, tabs, context actions,
import paths/formats, rename icons, toggles, filters and dialogs use the same
local/host handlers as native input. Operational progress remains exact integer
per-mille metadata; it is not a financial numeric channel.

Hidden amounts are projected before annotation, copy, tooltip and chart text.
Hidden financial fields retain their semantic identity but are read-only and
disabled, with no exact value or numeric payload. Public destinations remain
available in deliberate review. A user must explicitly reveal amounts before
exact financial review; no approval action is supplied by this fixture shell.
Inactive document bodies and locked bodies are absent. Modal publication excludes
background controls. New document tabs and external tab selection deliberately
return focus to the stable tab. Dialog cancellation/acknowledgement returns focus; a removed return target falls
back to the active document, or to Import in the empty workspace.
Explicit Enter/external validation errors return focus to the invalid editor.
Focus-loss validation preserves the user's destination instead of stealing clicks.

The focused engine and actual native matrices cover current application semantics
and actions only. They are not AT-SPI2/Orca, IME, physical GPU, mixed-DPI, Windows or
macOS qualification. The [Linux adapter](linux-accessibility.md) consumes these
packets; its real-bus and required Orca gates are recorded separately. Complete
Linux input/system adapters belong to L2.9. Fixture handlers are explicit
and effect-free. No signing, broadcasting, custody, daemon launch or wallet-file
deletion capability is provided here.
