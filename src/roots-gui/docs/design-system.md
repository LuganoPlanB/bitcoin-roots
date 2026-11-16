# Civic Digital Commons wallet system

This native Operate surface follows the supplied v1.0 brief. Published brand
JSON and exact identity SVGs live in `assets/brand/`; source hashes/rights are
preserved there. `ui/theme/tokens` is the application semantic layer. Components
consume roles, never a local hex palette. Incoming/outgoing use explicit signs
with ordinary text; offline/unconfirmed are neutral, stale/incomplete warning,
failed/conflicted error. Dark coral is interaction, distinct from error pink.
Gold/orange is identity only; allow at most one exceptional identity per view.

`Wallet(dark)` contains every specified light/dark role, including foreground
and background for four semantic statuses, UTXO and script inspection roles.
The two immutable identity gradients preserve the specified stops. Color is
accompanied by status text. Action focus uses Violet, a 2-design-pixel ring and
at least 2 design pixels of clear background separation, scaled with the UI.
Body/secondary/status/action text targets 4.5:1; control
boundaries and focus target 3:1 against adjacent surfaces. `Composite` uses
source-over sRGB alpha over the actual backdrop before WCAG luminance. A token
pair measured on an opaque substitute cannot certify a translucent layer.

`Make` always derives a new ImKit Theme from immutable base metrics. `Apply`
is called before NewFrame; `Scope` balances temporary projections and restores
the entire preceding ImGui style/metrics. The pinned ImKit standard projection
softens borders and mixes action states, so the host projects opaque input
boundaries and a small public Skin for exact primary/secondary states. The skin
does not submit items, change IDs, labels, navigation or action results. Other
variants retain ImKit drawing. Every component receives the host theme.
The pinned ImKit action focus painter uses a fixed 2px expansion with scaled
stroke, collapsing the gap at larger scales, and forces cursor visibility even
for mouse focus. A GUI-owned `theme::ActionButton` retains the component's
input/IDs/results, locally suppresses that decoration, preserves the host's
navigation visibility and draws a separated ring with public ImGui APIs.
Expansion includes one framebuffer-pixel antialias guard. Exact native left-edge
measurements at 100/150/200% show 2/4/4 clear background pixels and 2/3/4 effective
stroke pixels. No dependency source, archive, pin or license is changed.
Disabled content keeps readable text and a dependency explanation; it is never
made unreadable with global alpha. Selected rows retain normal financial text.

Inter Regular 14px and SemiBold 20px are required, loaded from executable-relative
licensed assets; missing files fail visibly. Tables use ordinary Inter tabular
digits. Technical descriptors/hex use the embedded scalable ProggyForever at
13px. This minimal technical face covers ASCII/Latin technical identifiers;
body labels use Inter's bundled coverage. Missing general-script glyphs/complex
shaping remain explicit localization qualification work; no system-font or
unverified universal-glyph fallback is claimed. Typography/layout scale once
from base values; framebuffer density remains independent.

The production bootstrap exposes Light/Dark/System, starts Light, and atomically
saves its stable lowercase ID to `$XDG_CONFIG_HOME/roots-gui/appearance` or
`$HOME/.config/roots-gui/appearance`. A write failure preserves the prior choice
and offers retry; invalid saved data is visibly reported. System dynamically
observes the optional Linux GNOME `color-scheme` setting through read-only GIO,
draining bounded ready desktop events on the host thread. No setting is changed.
When that desktop capability/schema is unavailable, System visibly falls back
to Light while preserving the saved System ID. Other desktop providers and
macOS/Windows appearance integration remain follow-on work, not native passes.

`DrawThemeGallery` is production composition: real input, selection, readable
disabled state, explicit success/warning/error/offline text, technical font and
an always-reachable primary action outside a scrollable inspection area.
Native framebuffer qualification covers both widths/themes at 100/150/200% and
real hover/pressed/focus. Null-backend contract checks separately prove nested
restoration, repeated DPI, exact token values and durable appearance round trips.
This gallery supplies public fixture data, with no wallet/RPC/signing effects.
Its inspection child uses public `NavFlattened` so Tab traverses from the label
input through rows to the footer action. Pointer hover explicitly excludes
ImGui's navigation-hover override. Normal, hover and pressed captures retain
their observed pointer/focus modes; focus captures require the primary itself
focused with visible keyboard navigation after actual native Tab traversal.
