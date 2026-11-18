# Standalone and parent builds

The standalone entry is this directory's `CMakeLists.txt`. From the repository
root, with an output directory outside the source repository:

```sh
cmake -S src/roots-gui -B ../roots-gui-build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build ../roots-gui-build -j 2
ctest --test-dir ../roots-gui-build --output-on-failure --no-tests=error -j 2
../roots-gui-build/roots-gui --build-contract
```

This configuration does not process the daemon/Qt build or require Boost,
Core libraries or Core internal headers. Native Xlib/OpenGL, OpenSSL and libsodium>=1.0.18
development files, C/C++20, CMake ≥3.22, Python ≥3.10 and Git for the reviewed
provider patch are system prerequisites. The pinned sources and notices in
[build-inputs.md](build-inputs.md) are the complete local bundled inputs.
Configuration verifies/extracts them into private build directories; it never
downloads sources or reads sibling checkouts. Reconfiguration checks every UI
source hash and the unchanged prepared provider snapshot. Changed pins produce
a distinct source identity; modified existing staging fails rather than being
silently adopted. Source preparation intentionally requires build outputs
outside the source repository.

ccache is selected when available and no compiler launcher was explicitly
chosen or disabled. Existing parent launchers are reused; linking is not cached.
The parent's explicit `WITH_CCACHE=OFF` also suppresses GUI auto-selection.
Build commands use `-j 2`, including the isolated provider subprocess. Compiler
and toolchain choices are forwarded to that subprocess; its options, targets
and cache remain independent of the parent Core secp256k1 build.

The parent integration changes only two places: `BUILD_ROOTS_GUI` defaults OFF
in the root options, and `src/CMakeLists.txt` conditionally adds this directory.
Enable it independently of the existing Qt option:

```sh
cmake -S . -B ../roots-parent-build -G Ninja -DBUILD_ROOTS_GUI=ON
cmake --build ../roots-parent-build --target roots-gui -j 2
../roots-parent-build/bin/roots-gui --build-contract
```

Existing daemon, wallet, IPC, tests and `BUILD_GUI` defaults are preserved.
`BUILD_GUI=ON` continues to select Qt; both GUIs can be configured together.
Turning roots-gui OFF creates no GUI-local targets, source preparation or new
dependency searches. GUI child directory properties exclude inherited Core
include paths/definitions while leaving parent targets and global compiler
settings intact. Upstream dependency options are function-local; provider
CMake runs separately to avoid generic target/cache collisions.

| Target | Responsibility |
| --- | --- |
| `roots_gui_core` | Exact domain and application build metadata; private standalone protocol/transport/SQLite dependencies |
| `roots_gui_ui` | Production draw entry, document/recovery orchestration and ImKit integration; storage remains an adapter |
| `roots_gui_platform` | Native host, stock backend lifecycle, DPI/fonts and renderer resources |
| `roots-gui` | Executable entry and explicit diagnostic command dispatch |
| `roots_gui_provider_build` / `roots_gui_protocol` | Isolated public-data library build and imported interface, never Core targets |

`ROOTS_GUI_BUILD_TESTS` defaults ON for standalone and OFF under the parent.
Enable it explicitly in a parent build to register the build-contract check
and the separate [engine/model harness](test-harness.md). Production objects
remain unhooked; GUI test objects use their own consistent configuration.
The current executable supports `--version` and `--build-contract`; the latter
reports versions from linked libraries without creating a native context,
connecting to a node or opening metadata. Default startup opens the native workspace described in
[workspace-shell.md](workspace-shell.md), with deliberate public recovery and
connection choices. Runtime font files and their license
are copied beside the executable under `roots-gui-assets/fonts`; keep that
directory with the binary. Existing authenticated daemon wallets have read-only
history/address/coin services and scoped local metadata; wallet creation/import,
receiving allocation and financial transaction services remain later work.

The focused matrix covers standalone and both parent GUI options OFF/ON.
Actual GUI binaries were built with Qt OFF and ON; the existing headless
default targets also built and eight wallet/descriptor CTest suites passed.
The file-API audit compares all 125 legacy target contracts across paired
roots-gui OFF/ON configurations, including Qt tests, and checks old options,
the new dependency closure, actual headers, link libraries, symbols and one
compiler launcher. It normalizes build-directory paths, not compiler flags.
curl's own `HAVE_CONFIG_H` remains valid and resolves its own configuration;
Core headers remain forbidden. Reuse of these build-only profiles requires
unchanged integration inputs and their retained identities. It never establishes
Core/Qt runtime or physical-platform qualification.

Windows/OpenGL3 and macOS/Metal remain guarded follow-on source branches.
Linux build results do not qualify those hosts. Physical monitor/IME/assistive
hardware remain unqualified. Owned Roots RPC, local metadata/protection/recovery
and Linux virtual-display workspace journeys have separately scoped evidence;
Core runtime and other platforms remain unexecuted. No private-key/seed custody is authorized.
Public-data provider limits and
explicit signing/broadcast boundaries remain unchanged.

All six public test targets are mapped in [render-harness.md](render-harness.md). Full CTest additionally requires an explicit inspected visual baseline and native capability; absence returns unavailable rather than a passing skip.
The additional `roots_gui_applicable_tests` aggregate checks the complete current
Linux/explicit-owned-daemon registry and runs its61 unique deterministic
model/RPC/regtest/UI/boundary cases once. The22 separate native/baseline/platform
entries are excluded explicitly, with representative current native proof and
the remaining qualifications reported separately under the approved lean gate.
An added/missing case or changed public suite label fails before execution.

Linux accessibility additionally links the installed gio-2.0 SDK (>=2.66).
Real-bus tests require the installed AT-SPI registry, Python GI/Atspi typelib,
D-Bus and existing owned-display tools. The separate Orca gate requires an
installed working screen reader with its supported GTK braille monitor; missing capability fails
explicitly. See [Linux accessibility](linux-accessibility.md).
