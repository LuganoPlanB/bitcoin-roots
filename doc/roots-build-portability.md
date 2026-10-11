# Roots v31 build portability

This foundation preserves build selection on the authenticated Core v31.1
base. It does not implement the deferred runtime contracts in the
[catalog](roots-features.md). Use the existing [Unix build guide](build-unix.md)
and [dependency list](dependencies.md) for the underlying Core prerequisites.

| Selection | Default | Requirement or limitation |
| --- | --- | --- |
| `BUILD_GUI` | OFF | ON requires a real Qt SDK; QR is enabled by default and requires QRencode. |
| `WITH_QT_VERSION` | 6 | Exactly 5 or 6; Qt 5 >= 5.11.3 or Qt 6 >= 6.2. Static Qt 5 requires the plugin-import CMake command introduced in 5.14. |
| `WITH_SYSTEM_LEVELDB` | OFF | ON requires LevelDB headers, MemEnv headers and a linkable `NewMemEnv`, either in LevelDB or a separate helper library. |
| `WITH_SYSTEM_LIBSECP256K1` | OFF | ON requires pkg-config metadata and the recovery, extrakeys, schnorrsig, ellswift and MuSig APIs used by v31, including `secp256k1_musig_pubkey_get`. |
| `WITH_MINIUPNPC` | OFF | Dependency discovery only, API >= 17. UPnP runtime and depends recipe integration are deferred to the peer theme. |
| `ENABLE_WALLET` / `BUILD_TESTS` | ON | Descriptor wallets use SQLite; no Berkeley DB option is restored. |
| `ENABLE_EXTERNAL_SIGNER` | ON | Retains the v31 external signer boundary; sweep extensions are deferred. |
| `ENABLE_IPC` | ON except Windows | May be explicitly disabled. IPC needs Cap'n Proto and the upstream libmultiprocess build prerequisites. |
| `WITH_QRENCODE` / `WITH_DBUS` | Conditional | Inherit v31 GUI/platform conditions. QR-to-PNG extensions are deferred. |

The embedded LevelDB fork remains the default. System LevelDB is an explicit
operator choice and does not contain the embedded fork's changes. Its finder
checks actual MemEnv linkage instead of assuming library presence proves
usability. Finder fixture tests check integrated helpers, split helpers,
missing-helper rejection and rejection of a broken Debug configuration despite
a usable Release library. Repeated integrated/split cache reconfiguration also
checks helper rediscovery and consumer linkage; these fixtures do not certify a
real distribution package.

Native SHA acceleration remains attached to the v31 crypto targets and source
files, using their compiler/architecture probes. No global architecture flag
is added and no cryptographic primitive or consensus rule is changed.

Example headless configuration (choose an available generator):

```sh
cmake -S . -B build-headless -G Ninja -DBUILD_GUI=OFF -DENABLE_IPC=OFF
cmake --build build-headless -j2
ctest --test-dir build-headless --output-on-failure -j2
```

For an installed Qt SDK, configure a separate directory with `-DBUILD_GUI=ON`
and `-DWITH_QT_VERSION=5` or `6`. Keep QR enabled for the full GUI profile;
`-DWITH_QRENCODE=OFF` is a deliberate reduced profile and cannot prove QR
coverage. Run the registered Qt tests with an available display or the
supported offscreen platform plugin. An unsuccessful dependency discovery is
an unresolved build gate, not a passing GUI test.

Unsupported or deferred switches are not silently treated as runtime
features. MiniUPnPc runtime and depends integration are owned by the peer theme;
Windows taskbar progress is owned by desktop integration. The old dedicated
Tor-subprocess declaration has no implementation at the pinned source.
No package installation or generated third-party compatibility shim is
part of this foundation.
