# Offline GUI source inputs

`third_party/ui-lock.json` pins the local `ui-sources.tar.gz` and its complete
`ui-source-manifest.json`. Each manifest entry records original upstream path,
source identity, SHA256, size and Git blob where applicable. There are no
configure-time downloads. Existing provider archives, conformance patch, locks
and public-data policy remain independent and unchanged.
Source bytes are unchanged; regular archive member modes are normalized to
0644. Required build scripts are invoked by their interpreters, as tested by
the fresh offline build. This archive does not claim original executable modes.

| Input | Exact source | Delivered/use contract |
| --- | --- | --- |
| ImKit 4.1.0 | `986d8d286a74fb7b2f724b9ef5d008af139aa887` | 15 core implementation units, recursive public/internal include closure, MIT notices and original upstream CMake for provenance. No Gallery/editor/show implementation. |
| Dear ImGui docking, version 19297 | `367b2c24f399988ddafc0bb4628da0106bcc09be` | Four core units, their headers/config/STB support and matching GLFW/OpenGL3/Metal backend sources; MIT. |
| GLFW 3.5.1 | `d9d6f0f1f967807ffade6598ea9a631ebaf37a56` | Complete source archive, zlib/libpng terms. Linux uses real X11/GLX; Wayland is disabled pending separate qualification. |
| Inter 4.1 | Unmodified Regular/SemiBold and OFL from pinned ImKit tree | Host-loaded fonts; original hashes and SIL OFL retained. Noto and unrelated artwork/media are not delivered. |
| curl 8.23.0 | Official source archive with exact SHA256 in lock | HTTP/HTTPS-only static libcurl with system OpenSSL; proxy and Basic authentication APIs retained. curl permissive license and all original source notices retained. |
| SQLite 3.54.1 | Official amalgamation archive with exact SHA256 in lock | `sqlite3.c`, `sqlite3.h`, `sqlite3ext.h`, including public-domain dedication; independent GUI metadata library, no Core database access. Threadsafe, extension loading omitted. |
| X11 extension headers | libXcursor 1.2.3, libXrandr 1.5.4, libXinerama 1.1.5, libXi 1.8.2, libXfixes 6.0.1; exact official archive hashes | Unmodified public headers and each full COPYING, MIT/X11 permissive terms. Supply missing build headers; GLFW dynamically loads existing native libraries. No X server or system library installation. |

curl and SQLite choices were checked against their official
[release table](https://curl.se/docs/releases.html) and
[release history](https://sqlite.org/chronology.html) on 2026-11-15. This records
selection provenance, not an ongoing security-support guarantee. Archives were
retrieved over HTTPS; independent release-signature authenticity is unverified.
The earlier provisional curl/SQLite archives were not adopted.

## Preparation and target ownership

The production standalone/parent entry now invokes the preparation wrapper
automatically; see [build.md](build.md). The direct commands below are for the
independent dependency consumer and explicit input audits.

Run `python3 third_party/prepare_ui.py --destination /absolute/build/inputs`
before configure. It checks archive and manifest digests, rejects staging within
the source repository, validates the complete file set and every staged hash,
and rejects changed preexisting content. Staging is disposable and never becomes
a reference checkout. A repeated preparation verifies the same exact bytes.

`cmake/ui-dependencies.cmake` supplies a core-only source adapter instead of
executing ImKit's development/package CMake. Its original CMake unconditionally
declares editor/show targets; their unused sources are intentionally absent.
The adapter sets `IMKIT_IMGUI_TARGET=roots_gui_imgui`, links `imkit::imkit` to
that existing target and preserves every original ImKit source and version guard.
It compiles exactly one four-unit ImGui target. All consumers/backends/ImKit
share the pinned `imgui.h` and unmodified `imconfig.h`, with no layout-changing
macros. No duplicate ImGui, compatibility-guard relaxation or Gallery is used.

The GLFW backend preserves native decoration. Linux compiles the matching
OpenGL3 renderer with docking, viewport and dynamic-texture APIs present.
Docking/keyboard flags are host-owned; detachable OS viewports remain disabled
until their actual lifecycle is qualified. ImKit never owns the context,
fonts, native window, renderer or application state. Windows/OpenGL3 and
macOS/Metal source branches are guarded follow-on work; Linux results do not
qualify those hosts. The supplied sibling ImGui 19303 is incompatible and is
never a passing consumer include/source dependency.

`cmake/service-dependencies.cmake` builds pinned static libcurl and SQLite.
Dependency options are local function variables; caller BUILD_TESTING and
BUILD_SHARED_LIBS remain unchanged. HTTP/TLS request policy, credentials,
certificate validation, bounded workers and metadata schema/migrations belong
to future service implementations. API option acceptance does not prove a TLS
handshake, proxy/auth behavior or durable metadata. No keys/mnemonics are stored.
The provider still limits standard multisig finalization to 15 keys and does
not promise arbitrary tapscript/Miniscript satisfaction.

Required system prerequisites remain a C/C++20 toolchain, CMake/Ninja, native
Xlib/OpenGL development files and OpenSSL development libraries. Their exact
observed versions/build configuration belong to execution evidence. Bundled
headers do not replace runtime native libraries. No source cache, sibling
checkout, package-manager install or network access is needed for the tested
Linux build with those prerequisites present.

## Focused consumers and limits

`tests/ui-inputs` is an independent consumer, not the production executable.
Configure with `-DROOTS_GUI_UI_SOURCE=/absolute/build/inputs`, both compiler
launchers set to ccache when available, then build with `-j 2`. CTest runs the
CPU ImKit consumer and transport/metadata API consumer under the offline runner.
The audit validates all source identities, exact core unit counts, common
includes/configuration, compiler-launcher use and strict rejection of ImGui
19303 or absent texture capabilities.

The separate Linux native consumer runs on an authenticated owned Xvfb/Mesa
display. It initializes the actual GLFW/X11/OpenGL3 backends, renders ImKit,
uploads its dynamic font texture and reads a nonuniform framebuffer, then
settles all owned resources/processes. This is software-renderer dependency
capability evidence. It is not the production L1.6 lifecycle, physical monitor/
mixed-DPI/IME/accessibility evidence, window-manager decoration capture or a
macOS/Windows pass. Ordinary CPU tests never fabricate renderer backend flags.

## Explicit updates

1. Review source compatibility, applicable notices, redistribution and the exact
   upstream revision/archive hashes. Preserve provider pins separately.
2. Acquire the explicit archives listed in `vendor_ui_sources.py` into a local
   maintainer input directory; keep original download receipts and hashes.
3. Run that script with explicit read-only ImKit/ImGui repositories containing
   the pinned objects and `--acquired-inputs`. It reads committed Git objects,
   checks each acquired archive, and writes deterministic source-only delivery
   files. It has no network operation and is never called by configure.
4. Review the complete resulting lock/manifest/archive scope and notices. Run
   fresh preparation/configure/build with network denied and empty compiler
   cache, then CPU, source/guard and separately labeled native capability checks.
   Broaden platform validation for any changed host/backend contract.
