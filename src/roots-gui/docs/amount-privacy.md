# Amount presentation and motion

`ui/components/presentation` is the shared pre-presentation boundary. The host
passes `AmountPrivacy` explicitly to amount/fee fields, table amount cells,
copyable amounts and summaries. The gallery passes one policy to every shared
surface. Hosts can derive it from `PublicPreferences::hide_balances`; the gallery
checkbox is an immediate fixture preference, not a persistence or OS adapter.

Hidden mode returns constant `Hidden` text for values, drafts, tooltips and
semantic values; copy is absent. It does not format the underlying Money.
Amount/fee editors submit a separate disabled marker widget instead of supplying
the sensitive buffer to InputText. Retained drafts and acknowledged domain values
are untouched, including invalid drafts hidden while editing. Hiding does not
commit a draft or trigger a host write. Revealing restores it for deliberate edit.
Labels, status explanations, stable keys and selection remain available.

Charts receive no samples while hidden: the chart widget is omitted and the
exact text alternative becomes `Amount chart hidden`. The shared total uses
checked integer Money addition; missing/overflow totals are unavailable, never
silently rounded. The gallery's total explicitly includes frozen/unavailable
observations and is not advertised as spendable. Visible chart percentages are
non-authoritative floating geometry required by pinned ImKit BarChart. Exact
text alternatives, copying, editing and approval contracts remain integer Money
or rational FeeRate. No financial values are animated or parsed as floats.

`CopyableAmount` emits an owning typed `CopyAmount` request only while visible;
it does not write the OS clipboard. `CopyableIdentifier` continues to show and
copy full destinations/public identifiers, independently of balance visibility.
Free-text labels are not guessed to be amounts. Callers must classify financial
content by role rather than placing private amounts in arbitrary status/label
strings. Hidden balances are a presentation preference, not a privacy lock,
metadata encryption, memory erasure or daemon walletlock.

`ReviewDetails`/`DeliberateReview` require the current explicit AmountPrivacy.
Hidden review returns the same constant marker without formatting the amount,
with no amount copy payload. Its text and annotations keep the full destination
available in both modes. Entering review never reveals a hidden amount. The
component asks the user to turn off Hide amounts to review the exact value;
only that explicit show-amounts choice reveals it. The review amount's stable
annotation ID is unchanged by hide/reveal.

These components provide no approval/sign/broadcast action. A future transaction
workflow must require clear exact financial review after explicit reveal; it
must not manufacture approval from concealed values. The current optional
public review fixture exercises this presentation contract only. The gallery's
label dialog remains a separate effect-free public fixture.

## Annotation lifetime

`PresentationFrame` owns copied annotation values through the end of publication
and exposes the pinned public AccessibilityFrame tree. It records the actual
samples submitted by AmountSummary for snapshot verification. Begin advances a
frame generation and invalidates borrowed views from the prior frame. Stable
IDs derive from widget/model identity, never from the amount or label. Hidden
nodes contain no numeric amount payload; frozen/disabled cells retain readable
text and disabled state. Status annotations retain freshness/failure words and
the original observation time in both visibility modes. Validate/overflow facts
are checked in the current production gallery and tests.

This is the amount annotation/snapshot contract. Complete roles/actions,
stale-generation queues, inactive/locked-node removal, immutable native bridge
publication and full accessibility interaction are L2.7–L2.9. No current OS
screen-reader claim follows from a valid annotation tree.

## Motion and hierarchy

Only the small neutral privacy-state indicator transitions, with host-owned
ImKit AnimationState, ease-out and a 120ms duration. Its space is reserved;
content is already visible, and privacy takes effect before any amount draw in
the same frame. Reduced motion snaps the indicator immediately to its useful
resting state. Repeated toggles/interruptions are tested. There is no entrance
choreography, looping decoration, animated financial value or approval backdrop.
The host supplies the reduced-motion preference; Linux system preference
integration remains L2.9. The existing theme's other decorative motion stays off.

ViewPolish grants at most one exceptional identity accent per view. The gallery
uses one small identity mark beside the heading, using the exact Gold/Orange
identity token for its theme. It communicates no financial status. Charts use a
neutral role; failures/freshness use the existing semantic pairs and text.
Disabled/frozen text keeps the accepted full-opacity readable projection.

## Evidence

Five `roots.privacy` Test Engine cases cover model projection/overflow, actual
mouse and keyboard hide/show/hover/copy/edit behavior, invalid active-draft
preservation, deliberate review and motion interruption/reduction/accent budget.
The actual production rendered-text log uses a private in-memory clipboard
callback. Visible positive controls must contain the known fixture amounts;
hidden text/tooltips, actual chart payloads and annotations must exclude them.
Twenty independent leak-checker cases deliberately inject violations.

The native component matrix has 132 captures: the previous six scenarios plus
hidden, reduced-motion, hidden+reduced-motion, hidden review and explicitly
revealed review, each at 1200x820/960x640,
Light/Dark and 100/150/200%. The new scenes click the real controls via XTest,
show the observation rows, retain original literal hash-label coverage, and
save production text/annotation snapshots with the actual OpenGL framebuffer.
The wrapper checks all snapshots, including 36 hidden ones, and teardown. Pixel
repeat, source/config/binary/font identities and raw process evidence remain
outside Git. Explicit fixed presentation time is fixture-only; production and
watchdog clocks retain their existing defaults. Physical/GPU/mixed-DPI,
Orca/AT-SPI, IME and macOS/Windows qualifications remain outstanding.
