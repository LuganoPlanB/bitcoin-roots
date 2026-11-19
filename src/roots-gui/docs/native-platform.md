# Native bootstrap and platform evidence

Default `roots-gui` startup creates a decorated OS window and the production
empty workspace draw entry. It shows the product name, offline status and a
keyboard/mouse reachable Close action. Wallet opening, navigation and services
remain later work; this screen makes no connection and starts no daemon.

`NativeHost` owns GLFW, the OpenGL context, ImGui and the stock pinned GLFW and
OpenGL3 backends on its creating thread. Only one host may exist at a time.
The official backend installs and restores input callbacks; no synthetic
capture flags are supplied. Initialization failures and normal/exceptional
exit use the same RAII teardown. Renderer textures are released while their
context is current, followed by platform backend, ImGui, window and GLFW.
Detached viewports and docking are disabled. The OS supplies title bar, move,
resize, minimize and close controls.

Inter Regular/SemiBold and OFL are copied to `roots-gui-assets/fonts` beside
the executable. Linux resolves this directory from `/proc/self/exe`, so an
unrelated working directory is supported. Missing assets fail initialization
with a diagnostic. Font bytes belong to the atlas; no borrowed temporary font
buffer survives loading. ImGui's dynamic atlas owns texture updates. Device
object recreation uses the stock renderer API and the next frame recreates
the font texture.

GLFW supplies content scale each frame. Fresh theme tokens are derived from
user font scale times content scale; layout and explicit font sizes scale once.
Repeated application cannot compound style scaling. Minimum logical client
size is 960×640, scaled for the current DPI. Minimized or zero-sized clients
wait for events without rendering; restoration resumes the same host. Pinned
ImGui quantizes fractional font sizes to the nearest integer raster size.

| Platform | Source | Compile | Native evidence |
| --- | --- | --- | --- |
| Linux X11/OpenGL3 | Production host and executable | Standalone and parent Qt OFF/ON | Authenticated owned Xvfb, real Marco WM, Mesa llvmpipe; production Draw and relocated executable |
| Windows/OpenGL3 | Guarded dependency/backend source; executable asset locator needs platform implementation | Unverified | No host available; follow-on required |
| macOS/Metal | Guarded matching backend source; production host explicitly reports pending Metal lifecycle | Unverified | No host available; follow-on required |

Linux checks exercise both 96 and 144 Xft DPI startup profiles (observed GLFW
scale 1 and 1.5), 1200×820 and 960×640 logical clients, Light/Dark and user font
scales 100%, 150%, 200%. Actual production framebuffer captures and native
frame root crops are retained. They exercise resize, iconify/restore, font
changes, renderer texture recreation, repeated launch/close, real XTest
keyboard/mouse input and WM close, owner-thread rejection and failure cleanup.
The visual matrix requires all content and Close to remain reachable. The
capture helper encodes actual RGB readbacks losslessly; it creates no UI pixels.
This is bootstrap evidence, independent of the earlier dependency consumer.

The virtual software display does not qualify physical monitor transitions,
hardware GPU drivers, IME, assistive technology, live mixed-monitor DPI or
Windows/macOS. Runtime content-scale polling is implemented; physical monitor
transition evidence remains follow-on work. Marco emits the stock GLFW
`_NET_REQUEST_FRAME_EXTENTS` timeout warning before mapping; post-map native
decorations and their captured extents are independently verified. No upstream
source or warning suppression changes are applied.

Native checks run explicitly through `tests/platform/owned_display.py` with
private authentication, configuration, D-Bus and bounded process cleanup.
They require installed Xvfb, Marco, xrdb and XTest, and never install packages
or use the user's display. `roots_gui_native_lifecycle` instruments the same
production host/draw code. Ordinary CTest build metadata checks do not claim
native qualification. The [visual runner](render-harness.md) reproduces this matrix and compares actual pixels against an explicitly inspected external baseline.

Mapped native decoration extents are cached rather than queried repeatedly while minimized; mapped DPI changes refresh that observation. The renderer still uses actual client/framebuffer sizes independently.

The application-owned [Linux accessibility bridge](linux-accessibility.md) uses
the same native owner and immutable publications. Its real AT-SPI bus, Orca
braille-monitor journey and failure/teardown evidence are separate from the
historical bootstrap matrix above. Speech/audio, hardware braille, complete IME
and physical/mixed-DPI/platform follow-ons are not implied by monitor evidence.


## Linux QR camera

From a QR import dialog, Find cameras checks a bounded list of local V4L2
character-device identities without opening them. Open selected camera makes
the explicit access request and confirms the device still matches. Start capture
begins streaming at the selected discrete resolution. Linux reports permission
and device failures through the adapter. PNG and decoded-text import remain
available when the camera is stopped or unavailable.

The capture path accepts single-plane V4L2 MMAP streams with uncompressed YUYV
or grayscale at 160×120 through 1280×960. It validates format, stride, mapping
identity, offsets and frame lengths before accessing any pixels. It uses at
most four mapped buffers and 12 MiB of mappings, with at most four decoding
attempts per second and one published frame. Capture ends after two minutes,
or after five seconds without frames. MJPEG, multiplanar capture, and modes
without discrete resolution evidence are unavailable. The kernel interface and
buffer contract are described in the
[V4L2 capture documentation](https://docs.kernel.org/userspace-api/media/v4l/mmap.html).

Stopping, closing, hiding, losing window focus, or changing document context
retires the stream and any pending validation. The worker sends STREAMOFF,
unmaps buffers and closes the descriptor. Reopening requires an explicit action.
A driver call already blocked in the kernel is governed by the driver; polling
for normal frames waits no more than 50 ms per iteration.

The portable camera interface reports unavailable on macOS and Windows.
Follow-on work includes an AVFoundation capture adapter and usage description,
TCC permission/denial/revocation, selected device and format validation, and
real preview/lifetime evidence on macOS. Windows needs a Media Foundation
capture adapter, system camera privacy/denial/revocation handling, format and
buffer validation, and real preview/lifetime evidence. Physical Linux camera
model, driver and lighting interoperability also requires device evidence.
