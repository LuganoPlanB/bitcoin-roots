# Bitcoin Roots Linux distribution

The x86-64 archive contains the Bitcoin Roots desktop executable, Inter fonts,
the desktop entry and icon, license notices, and three application libraries
under `lib/roots-gui`: OpenSSL `libssl.so.3` and `libcrypto.so.3`, and
`libsodium.so.23`. The executable finds these libraries and its fonts relative
to its own location. Extract and move the whole tree together.

## Install, launch and remove

Use a dedicated directory under your account. Choose a new prefix whose parent
already exists. The installer requires Python 3.10 or later. These commands use
an extracted archive and a separate installation with a space in its name.

```sh
mkdir "$HOME/roots-distribution"
tar -xzf roots-gui-linux-x86_64.tar.gz -C "$HOME/roots-distribution"
python3 "$HOME/roots-distribution/install.py" install "$HOME/Bitcoin Roots"
"$HOME/Bitcoin Roots/bin/roots-gui"
python3 "$HOME/roots-distribution/install.py" uninstall "$HOME/Bitcoin Roots"
```

For desktop launchers, the bundle includes `share/applications/org.bitcoinroots.RootsGui.desktop`
and its matching hicolor icon. A desktop session resolving this entry needs the
prefix's `bin` in its `PATH` and the prefix's `share` in `XDG_DATA_DIRS`.
Alternatively, launch the executable by its full path. Installation does not
change desktop settings, start services or register file associations.

Keep the original archive to remove the installation. Installation validates
the file inventory and refuses an existing prefix. Removal requires the
matching distribution manifest, refuses a changed installed file and removes
only verified package files and directories left empty. The prefix should be
owned by a single user and kept free of concurrent modifications during either
operation. Wallet metadata and preferences stored through XDG are preserved.

## Linux prerequisites

The distribution targets the Debian 13 x86-64 glibc base (glibc 2.41, GCC 14
C++ runtime). An X11 display and an OpenGL driver are required. The OS provides
the ELF loader, C/C++ runtimes, Xlib/XCB and their X11 dependencies, GLVND/OpenGL
and the selected driver, GLib/GIO, zlib, Zstandard, and their dependencies, as
well as the system TLS
trust configuration for authenticated HTTPS. Native accessibility uses the
session D-Bus and AT-SPI2 service. The optional public file chooser uses the
OS's Zenity. The Unix clipboard lets the user paste a public PSBT or transaction
even when the chooser is unavailable. The GUI can launch and inspect a public
document without configuring a Bitcoin server. Connected wallet operations
require an independently installed and configured compatible server.

The runtime was exercised with the Debian 13 system libraries and X11
software rendering. Native Wayland, other distributions, other CPU
architectures and physical GPU or assistive hardware remain unqualified.
Camera devices, external IME preedit/candidate presentation, and physical
mixed-DPI moves require separate qualification. The archive contains no server,
wallet credentials, fixtures, or independent optional service providers.

## Build the archive

From the frozen GUI source tree, run `packaging/build-linux.sh NEW_BUILD_DIRECTORY`.
Its argument must be a new directory outside the GUI source. It uses CMake
3.22 or later, Ninja, a C/C++ toolchain (C++20), Python 3.10 or later, pkg-config,
patch, and GNU core utilities on the Debian 13 x86-64 baseline. Development
dependencies are OpenSSL 3, libsodium, GLib/GIO, Xlib, XCB and OpenGL. The
copyright files for Debian's `libssl3t64` and `libsodium23` are also required.
The script stages a Release build with the GUI tests disabled, without compiler
cache or fetch steps. Patched libwally/secp256k1, ImGui, ImKit, GLFW, curl and
SQLite build from the verified source archives in `third_party/`. Build with
network access disabled for independent offline reproduction. The destination
retains the separate `build`, `stage` and tar archive for inspection.

## Packaging follow-ons

- **macOS.** Build an architecture-qualified `.app` with `Contents/MacOS`,
  resources, Info.plist, a Metal host and relocatable framework/dylib paths.
  Qualify private installation/removal, native offline launch, clipboard and
  dialogs, VoiceOver, and display scaling on a clean Mac. Hardened runtime,
  Developer ID signing, notarization and Gatekeeper behavior require a separate
  authorized release workflow.
- **Windows.** Qualify the chosen MSVC runtime and architecture, stage the
  executable, resources and needed DLLs under the application installation,
  and verify relocatable paths and owned MSI/MSIX install/remove behavior on a
  clean Windows image. Native OpenGL host, clipboard/dialogs, UI Automation,
  Narrator and mixed-DPI checks precede any release. Signing and release
  publication require a separate authorized workflow.
