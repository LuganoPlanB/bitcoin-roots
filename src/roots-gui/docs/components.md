# Shared wallet controls

`ui/components/controls.h` exposes retained edit/selection/dialog state and
production draw functions. `ui/components/gallery` composes the same functions
for public fixture inspection. The gallery is not an RPC, metadata database,
clipboard adapter or transaction approval service.

Every effect leaves the drawing layer through `Commands::Submit(Command)`.
The owning variant carries a full public identifier, exact `Money`, rational
`FeeRate`, label, stable row-key set or named action. No finance payload uses
floating-point values. A host that queues commands must copy the owning variant;
the reference is borrowed only for the call. The host supplies document/request
context, capability checks, actual persistence/clipboard work and error delivery.
The fixture acknowledgements establish in-memory application commits only.

## Edit and command contract

Amount, fee and label editors keep their bounded UTF-8 input buffers separate
from the acknowledged `saved` string. Enter and edited focus loss commit through
the same validator. `ParseAmount` delegates to exact `Money::ParseBtc` and checks
the nonnegative Bitcoin-supply bound. Fees delegate to exact `FeeRate::Parse`.
No scalar/float widget or conversion is involved. Labels allow clearing and
reject control characters; buffers accept at most 255 bytes.

Invalid input never reaches the host and remains editable. A rejected host write
leaves the saved value and draft intact with a visible error. Enter followed by
the same edit's blur cannot duplicate a request; a new draft or another explicit
Enter permits retry. Unchanged values do not write. The caller owns drafts when
an editor disappears and must explicitly commit/cancel when closing or replacing
its document; disappearance cannot manufacture an ImGui deactivation event.

`CopyableIdentifier` shows the full value in a read-only horizontally scrollable
input and tooltip, and submits the complete owning value. It does not silently
shorten clipboard data or set the OS clipboard itself.
The host supplies the borrowed licensed monospace face for its 13px technical
value; surrounding labels/actions retain Inter. The native gallery supplies the
already loaded embedded ProggyForever face. `ItemFacts.requested` distinguishes
a gesture from `activated`, which records host acceptance; a caller
can surface a rejected copy/action through its status model. Disabled controls
emit no command. Disabled primary actions use neutral control/text roles, retain
readable contrast and expose a dependency explanation.

## Selection and dialogs

Table rows use host-assigned nonzero 64-bit keys. Widget IDs derive from those
keys and a stable `###select` suffix, never a label or row index. Rename and
filter retain selection and widget identity. A separate public `TextUnformatted`
submission renders labels and disabled reasons literally, including `##` and
`###`; selectable markup never parses application text. Current keyboard focus is reported
separately and does not select a row; activation sends a typed selection request.
Ctrl toggles individual keys. Hidden selected keys remain selected until the host
explicitly removes them. Disabled rows remain readable, name their unavailable
reason and cannot select. Duplicate/zero keys fail closed; correcting the input
clears that validation error without making a selection request.

Dialog shells reuse ImKit `BeginDialog`/`EndDialog` and retained `DialogState`.
Their actions use the accepted GUI focus wrapper. Initial focus goes to Cancel;
acceptance emits one typed dialog action. Rejection keeps the modal/draft open.
Cancel/Escape close locally, emit a cancellation observation and restore focus
to the stable launcher on its next draw. A host cannot trap local cancellation
by rejecting that observation. None of these actions signs or broadcasts.

## Pinned API choices

| Need | Composition and reason |
| --- | --- |
| Form labels/hints/validation | ImKit `BeginFormField`/`EndFormField` and explicit `ComponentOptions` `ValidationMessage`; visible wrapped text plus validation outline |
| Amount/fee text | Public ImGui `InputText`; `InputScalarWithUnit` cannot preserve this exact decimal text/validation/commit contract |
| Actions | ImKit through the existing GUI `theme::ActionButton`; retains corrected scaled focus gap and keyboard/mouse distinction |
| Status/freshness | Public text/DrawList composition using exact wallet foreground/background pairs; pinned `PersistentBanner` forces a `.10` accent overlay and has no paired-background override |
| Selection table | Public ImGui table/selectables; pinned `DataProvider` has no row-enabled callback, and its retained focus index is not the stable model-key state required here |
| Dialog | ImKit framing/state, public focus-wrapped actions; `DialogLauncher`/`AlertDialog` call the fixed-offset painter and do not offer acknowledged typed actions |

The banner keeps cached/offline/unknown observations neutral, stale/incomplete
attention warning, and failures error, with explicit freshness and original
observation text. Components consume semantic roles rather than local palettes.
All borrowed draw strings survive the current submission; native accessibility
publication and its longer borrowed-string lifetime remain L2.7 work.

## Evidence layers

The `roots.components` Test Engine category has nine exact selected cases:
amount/fee Enter and blur, label acknowledgement/rejection/recovery, full-value
copy and disabled actions, focus versus selection with rename/filter and Ctrl,
dialog rejection/acceptance/Escape/return focus, disabled dialog acceptance,
independent command validation, malformed row-key recovery, and literal hash
labels through rename/filter with stable identity and selection. The latter
records ImGui's actual production rendered-text log using a private in-memory
clipboard callback, including the disabled reason; it never writes the OS clipboard. These call the
production controls. They do not prove native clipboard or durable metadata.

`roots_gui.theme.native_components` captures 132 actual native framebuffers:
1200x820/960x640, Light/Dark, 100/150/200% scale, with public, invalid, disabled,
dialog, empty, selected-table, hidden, reduced-motion and hidden+reduced-motion, hidden-review and explicitly revealed-review fixtures. The public scenes perform twelve real
XTest Copy gestures and verify twelve full-value host requests. Other fixtures
have no host effects. Invalid Apply is disabled; every scene checks primary
reachability and complete teardown. The accepted theme gallery's real focus,
wrong-target and rendered-gap regressions remain separate mandatory checks.
Manifests preserve commands/times, source/config/binary/font/pixel identities and
owned-process settlement outside Git. Physical monitors, IME, screen readers,
native OS dialogs/clipboard and other OS hosts retain their existing gates.
Fixture hosts select an explicit 1/60s presentation delta so core ImGui modal
dimming is deterministic as well as theme motion. Production defaults to native
time; process watchdogs and teardown deadlines always use real wall time. This
is an actual GLFW/OpenGL host option, not a null renderer or Test Engine hook.
The privacy extension uses the same controls with a centralized amount policy,
actual native toggles and amount annotation snapshots; see [amount-privacy.md](amount-privacy.md).
