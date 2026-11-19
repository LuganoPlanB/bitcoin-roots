# Platform adapters, native coverage and follow-on work

The GUI platform maintainers own these adapters and their outstanding native
validation. `NativeHost` remains the only ImGui/GLFW/context owner. Native
consumers receive owned semantic packets; actions return to its owner thread
through the bounded generation-checked exchange. Linux AT-SPI and the unmodified
Orca braille-monitor journey are documented in [linux-accessibility.md](linux-accessibility.md).
Physical audio and braille hardware are separate, unqualified capabilities.

The connected-wallet milestone adds current owner-thread semantic packets for
history/address/coin views, privacy lock and pinned-input inspection, with
guarded actions and cleanup covered by production Engine tests. Owned Roots
and Linux software-rendered native journeys qualify only their observed
read-only/local-metadata behavior. They do not establish Core compatibility,
hardware assistive support, funding/signing/broadcast services or another
platform's delivery. Parent acceptance uses one representative current native
journey plus explicit unchanged evidence; it does not rerun the historical
theme/scale/state cross-product or turn unavailable baseline contracts into passes.

| Boundary | Linux implementation | Residual implementation and qualification |
| --- | --- | --- |
| Clipboard | X11 selection ownership and asynchronous UTF-8/Latin-1/INCR reads; actual public InputText callback insertion | Non-X11 clipboard transport is unimplemented |
| Public file choice | Installed Zenity, argument-vector subprocess, asynchronous bounded output and reaping | Choice does not supply wallet import/export/persistence services |
| Composition | Pinned GLFW's installed Xlib XIM path; real dead-key compose/commit through the production editor and typed acknowledgement | External IME preedit/candidate integration, placement and cancellation remain unfinished, deferred and unqualified |
| Scale | Actual root RESOURCE_MANAGER/Xft.dpi notifications; host font scale, framebuffer and layout update | Per-monitor scale selection and true movement between different-DPI monitors remain unfinished, deferred and unqualified |
| Motion | Read-only GSettings enable-animations observation; unavailable setting conservatively reduces motion | Other desktop portals/settings providers are unimplemented |
| Accessibility | Application-owned AT-SPI2 service, owner-thread immutable packets/actions | Physical assistive devices remain unqualified |
| Local metadata protection | Installed libsodium Argon2id/secretstream, private atomic ciphertext and owner-thread privacy-lock epochs; representative native lock/unlock failure journey | OS RPC keychain provider remains unavailable; macOS/Windows private-file/keychain/native lock qualification remains a follow-on |

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

The [Linux distribution and packaging follow-ons](linux-package.md) describe
the private-prefix archive and the remaining macOS app-bundle and Windows
installer qualification.

## QR camera

The GUI platform maintainers own the portable `CameraDriver` and
`CameraService` interfaces, platform adapters, and native qualification. The
L6.11 source candidate is based on HEAD
`28d0e43fdbc74128f39700bc283afe136b7c4939`. The complete candidate and
evidence identities are in the milestone's immutable
`qr-camera/report.ledger-corrected.json` and
`qr-camera/delta-ledger-corrected.patch` evidence records.

| Platform | Status and evidence | Remaining work and evidence |
| --- | --- | --- |
| Linux | **Implemented and native-tested with controlled device input.** The real V4L2 adapter ran through a controlled syscall port in an owned X11 NativeHost. Independent decoding verified QR content in the actual preview framebuffer. The test host exposed no V4L2 device nodes, so no physical camera was exercised. | Qualify physical cameras, representative drivers and supported formats, permission changes, real disconnects, and QR readability across lighting conditions. Installed delivery remains unverified. |
| macOS | **Unavailable.** The portable interface is present. Native camera tests remain unexecuted. | Implement an AVFoundation adapter and camera usage description. Verify TCC grant, denial and revocation, device and format negotiation, capture lifetime and cleanup, and native preview and import. Installed delivery remains unverified. |
| Windows | **Unavailable.** The portable interface is present. Native camera tests remain unexecuted. | Implement a Media Foundation adapter. Verify system camera privacy grants, denial and revocation, device and format negotiation, capture lifetime and cleanup, and native preview and import. Installed delivery remains unverified. |

## External signer devices

GUI platform and wallet maintainers own the portable `DeviceSigners` RPC
boundary and the device views in Receive and signing review. The L6.12 source
candidate is based on HEAD `28d0e43fdbc74128f39700bc283afe136b7c4939`.
The immutable `device-signers/report.full.json` and
`device-signers/delta.patch` evidence records identify the exact candidate.
See the [capability contract](capabilities.md#daemon-external-signers) for
runtime prerequisites and airgapped exchange.

| Platform | Status and evidence | Remaining work and evidence |
| --- | --- | --- |
| Linux | **Implemented and native-tested with a controlled daemon signer.** The owned loopback emulator exercises discovery, signing and exact address results. Actual X11 NativeHost captures cover explicit selection, a pending device confirmation request, the exact daemon echo and rejection. Airgapped file and QR PSBT exchange is available. | Physical device use and certification remain unqualified. Before making a device claim, record the actual model, firmware, external signer/backend versions, configuration, display comparison, signing, cancellation/disconnect, and recovery evidence. Installed delivery remains unverified. |
| macOS | **Portable source only.** Native signer UI, daemon integration and physical devices remain unexecuted. | Qualify native device flows with an explicitly configured daemon. Record model, firmware and backend evidence before certification. Installed delivery remains unverified. |
| Windows | **Portable source only.** Native signer UI, daemon integration and physical devices remain unexecuted. | Qualify native device flows with an explicitly configured daemon. Record model, firmware and backend evidence before certification. Installed delivery remains unverified. |
