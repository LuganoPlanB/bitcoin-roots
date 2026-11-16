# Platform adapters, native coverage and follow-on work

The GUI platform maintainers own these adapters and their outstanding native
validation. `NativeHost` remains the only ImGui/GLFW/context owner. Native
consumers receive owned semantic packets; actions return to its owner thread
through the bounded generation-checked exchange. Linux AT-SPI and the unmodified
Orca braille-monitor journey are documented in [linux-accessibility.md](linux-accessibility.md).
Physical audio and braille hardware are separate, unqualified capabilities.

| Boundary | Linux implementation | Residual implementation and qualification |
| --- | --- | --- |
| Clipboard | X11 selection ownership and asynchronous UTF-8/Latin-1/INCR reads; actual public InputText callback insertion | Non-X11 clipboard transport is unimplemented |
| Public file choice | Installed Zenity, argument-vector subprocess, asynchronous bounded output and reaping | Choice does not supply wallet import/export/persistence services |
| Composition | Pinned GLFW's installed Xlib XIM path; real dead-key compose/commit through the production editor and typed acknowledgement | External IME preedit/candidate integration, placement and cancellation remain unfinished, deferred and unqualified |
| Scale | Actual root RESOURCE_MANAGER/Xft.dpi notifications; host font scale, framebuffer and layout update | Per-monitor scale selection and true movement between different-DPI monitors remain unfinished, deferred and unqualified |
| Motion | Read-only GSettings enable-animations observation; unavailable setting conservatively reduces motion | Other desktop portals/settings providers are unimplemented |
| Accessibility | Application-owned AT-SPI2 service, owner-thread immutable packets/actions | Physical assistive devices remain unqualified |

`NativeInputPort` is the UI boundary. Its per-frame installation is thread-local;
headless UI tests have no native service. Paste keeps only owned identity, view,
text and byte offsets, never an editor pointer. Insertion must still match the
active editor's value, cursor, selection, capacity and privacy. Selection-less
paste uses the current caret, rather than stale equal selection endpoints.
Changes of section/focus/privacy cancel pending work. Native reads have a two
second transfer deadline and a 64 KiB UTF-8 limit; fields retain their tighter
capacities. Large transaction/PSBT text needs a separately specified transport;
this clipboard adapter does not advertise unlimited transaction import.

Copy acknowledges acquired selection ownership. Hide Amounts retires our own
sensitive selection and cancels sensitive paste; it never clears another
application's clipboard. Exact money uses integer-domain formatting. Clipboard
workers own separate connections and never call GLFW/ImGui or access documents.
Checked XCB writes tolerate a disappearing consumer without a global Xlib error
handler. Native callbacks have a bounded per-frame pump. Close cancels transfers,
forces/reaps the owned chooser, and reports unsettled callbacks as teardown
failure rather than claiming success.

The chooser retains its request identity and original path/format. Cancellation,
errors, disappearance and stale results retain the current draft. The path is
returned literally, including spaces and `##`, without a shell or file read.
While its real modal child is active, both native parent input and semantic
parent actions are disabled; live dispatch rechecks that condition. Completion
returns focus to Browse. Normal application requests still require their actual
availability and acknowledgement. No daemon, signing, broadcast, custody,
private-key, deletion or import-persistence service is supplied here.

The current installed Xlib locale input method offers only preedit-none and
preedit-nothing styles. Stock pinned GLFW requests `XIMPreeditNothing` and exposes
no public input-context handle; the pinned Linux ImGui backend does not install
an IME positioning callback. Real dead-key composition therefore proves a native
commit path, not an external IME's preedit/candidate UI. The owned software X11
display exposes one RandR output/monitor with unknown physical dimensions.
Changing its global Xft resource proves an actual native system signal, not a
mixed-monitor move. Under the user-authorized verification revision of
2026-11-16, external IME preedit/candidate support and true mixed-monitor work
are explicit **nonblocking follow-on implementation and qualification** for this
plan, including parent acceptance and terminal review. The GUI platform
maintainers own adding preedit/candidate positioning and cancellation support,
then exercising a supported installed external IME. They also own per-monitor
scale selection and real different-DPI monitor moves; the current global Xft
adapter does not implement that behavior. Available native clipboard, file
choice, locale compose/commit, global scale/motion and accessibility evidence
does not qualify these unfinished capabilities. No synthetic replacement,
runtime installation or desktop-setting change is performed by the GUI/tests.

## Windows and macOS ownership

The GUI platform maintainers also own the following work. Platform-neutral
ImKit `AccessibilitySink`/`NativeActionSink`, stable roles/actions, immutable
`SemanticPacket` and queued receipt contracts are retained. Borrowed ImKit trees
are never safe native publication packets. The pinned source archive used by
this application contains the generic accessibility frame and queue; it contains
no UIA or NSAccessibility adapter implementation. Those native integrations
must consume the same owned packets and live guarded actions, reject stale or
retired objects, and marshal to the host owner. They must not bypass privacy,
locking, validation or acknowledgement. No UIA/NSAccessibility delivery is claimed.

| Platform | Retained source boundary | Owner's unfinished work and native gate |
| --- | --- | --- |
| Windows | Guarded pinned GLFW/OpenGL backend input and primary Ctrl mapping | Native host/assets delivery, UIA bridge, clipboard/dialog/IME/system adapters, builds and Narrator keyboard journey |
| macOS | Guarded pinned backend contracts and primary Command mapping | Metal/native host lifecycle, NSAccessibility bridge, clipboard/dialog/IME/system adapters, builds and VoiceOver keyboard journey |

Non-Linux `NativeInput` currently returns explicit unavailable file-choice/input
capabilities. Guarded dependencies are source contracts, not successful native
builds or release qualification. Narrator/VoiceOver, hardware GPU and physical
mixed-DPI work remain owned, unqualified follow-on obligations, nonblocking for
this plan under the same approved revision. Evidence is still required before
claiming those capabilities. Linux adapter results do not qualify another
operating system.
