# Linux accessibility adapter

NativeHost associates the production workspace SemanticExchange with an
application-owned AT-SPI2 exporter. It uses the installed GIO SDK (minimum 2.66;
current evidence uses 2.84.4) and public GNOME AT-SPI2 2.56.2 D-Bus signatures.
The application does not require libatspi development headers or modify ImKit.
Protocol declarations live in platform/linux/atspi_interfaces.h.

The private GLib main context is pumped on the native owner thread with bounded
work per render iteration. Registry discovery and embedding are asynchronous.
Consumers retain owned immutable packets; they never access ImGui or document
state. Action calls enqueue immutable identities and return success only after
existing owner-thread validation and host acknowledgement produce Applied.
Pending replies are bounded at 32; timed-out requests are cancelled before
future dispatch. Unsupported actions and stale generations fail explicitly.

Accessible, Application, Cache, Component, Action, Text, EditableText, Selection,
Table, TableCell and operational Value interfaces are exported as applicable.
Financial amounts have no numeric Value interface. Text positions are Unicode
characters; InsertText length is bytes. Caret and selection are observed from
the actual input callback. Caret/selection mutation, clipboard, resizing and
scroll methods are unavailable. Complete input/IME/system integration is later.

Stable model identities survive renames and ordering changes. Scope changes,
closure, locking, invalid publication and overflow retire native objects; their
paths are never reused. Cache updates retain parent/order information. Hidden
values are removed before export; deletion events never carry old financial
text. Invalid UTF-8 and unsafe coordinates fail closed. Bus failure exposes a
diagnostic while the workspace remains usable. Shutdown cancels requests,
unregisters objects, closes the private connection and drains callback ownership
before ImGui teardown.

The independent real-bus transport test is distinct from screen-reader evidence.
The production fixture uses actual native input and a separate libatspi client.
The Orca test runs the installed reader with its supported braille monitor,
isolated preferences, speech disabled and hardware braille disabled. It checks
actual reader-formatted BRAILLE/VISIBLE output, native object-navigation commands,
dialog/status/privacy/lock transitions and captures the real GTK monitor. Native
monitor images receive bounded visual inspection; the monitor's own Gtk button
names expose decorative dots and cannot substitute for rendered cell text.
Installed Orca alone is insufficient. Physical audio and braille hardware remain
unqualified by this monitor journey. Virtual
X11 evidence does not qualify physical monitors, IME, mixed DPI, macOS or Windows.

Run roots_gui.accessibility.native, .transport and .orca CTest cases through the
managed scratch wrapper. Each owns its authenticated display and bounded process
lifetimes, with a new immutable output directory. Tests never install packages
or change the user's desktop or speech configuration.

Table rows own actual literal-label and amount cells (plus availability in the
shell); GetAccessibleAt returns cells with matching TableCell positions. Status
and alert native names include already-projected current visible content, while
retaining their roles and identities. Operational numeric changes emit double
value events; financial text never uses the Value interface. A newly locked
active document returns focus once to its stable tab; later frames and unlock
preserve the user's next focus target.

Orca's supported debug-file option uses an owned PTY with a transparent byte
collector to obtain live line-buffered diagnostics. Orca is unmodified. The
collector closes and joins after the reader exits. Authenticated displays use
an atomic managed-scratch reservation held until Xvfb exits; a forced concurrent
startup regression exercises the former cookie collision without weakening
authentication or retrying failures.

## Interface language and exact values

Roots reads `LC_ALL`, then the applicable `LC_MESSAGES`, `LC_NUMERIC` or
`LC_TIME`, then `LANG` at startup. English is the default. The German catalog
covers the wallet rail, core Send fields and actions, signing and submission
actions, and common amount and date states. Entries outside the catalog and
unsupported language settings fall back to English. The catalog does not
require a generated German system locale. The `qps-ploc` profile expands
selected labels for layout testing.

German BTC presentation uses a decimal comma and dates use day-month-year
order. Dates continue to represent UTC. The Send BTC editor accepts one comma
or period as the decimal separator and rejects digit grouping. For example,
`1,000` represents one BTC. Satoshi entry accepts integers. Amount copies,
CSV, payment URLs, descriptors, addresses, transaction IDs, PSBTs and JSON
retain their canonical formats. The shipped renderer and fonts have no
qualified right-to-left shaping or bidirectional layout.

The broadcast fixture runner's `--reader` mode pairs the native wallet with an
independent AT-SPI client and installed Orca. The client requests keyboard
focus and sends X11 keys for payment entry and each deliberate action. It
enters the payment intent, opens the matching independently prepared public
PSBT, records and approves its review, validates independently signed
clipboard material, finalizes, checks policy, sends to owned regtest and
starts and stops monitoring. The journey uses Orca's braille monitor and
retains its formatted output. The input fixture separately exercises X11
dead-key composition and commit plus native reduced-motion updates.
