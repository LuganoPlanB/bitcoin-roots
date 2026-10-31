# Roots v32 build portability

This foundation uses the exact Core v32.1 base and native C++20, filesystem,
compiler, cryptographic acceleration and Qt6 APIs. It adds only required Roots
build support. See [dependencies](dependencies.md), the [Unix guide](build-unix.md)
and the [port review](roots-v32-port-review.md) for prerequisites and ownership.
Feature preservation remains subject to each owning milestone's verification.

| Selection | Default | Requirement or limitation |
| --- | --- | --- |
| `BUILD_GUI` | OFF | Qt >=6.2; QR support requires QRencode and remains enabled by default. No Qt5 selection is provided. |
| `WITH_SYSTEM_LEVELDB` | OFF | Requires headers and linkable MemEnv in LevelDB or a separate helper; every available Release/Debug configuration is checked. |
| `WITH_SYSTEM_LIBSECP256K1` | OFF | Requires pkg-config metadata and recovery, extrakeys, schnorrsig, ellswift and MuSig APIs, including `secp256k1_musig_pubkey_get`. |
| `WITH_MINIUPNPC` | OFF | Dependency discovery only, API >=17; UPnP runtime/depends integration belongs to the peer milestone. Windows vcpkg users must select the `miniupnpc` feature explicitly. |
| `ENABLE_WALLET` / `BUILD_TESTS` | ON | Descriptor wallets use SQLite; no Berkeley DB build option is restored. |
| `ENABLE_EXTERNAL_SIGNER` | ON | Native signer support; later wallet extensions have separate acceptance. |
| `ENABLE_IPC` | ON except Windows | Needs Cap'n Proto and native libmultiprocess prerequisites; explicit OFF is a reduced profile. |
| `WITH_QRENCODE` / `WITH_DBUS` | Conditional | Native v32 GUI/platform conditions. |
| `BUILD_BENCH` | OFF | ON builds `bench_bitcoin`; use `-list` to verify selection before a bounded `-sanity-check` smoke. |

Embedded LevelDB/secp256k1 remain the defaults, with native crypto flags scoped
to upstream targets and source files. Finder fixtures exercise integrated/split
MemEnv, missing/broken helpers and cache transitions; they do not certify a real
distribution package. Homebrew fixtures verify matching Boost/Cap'n Proto header
aliases retain system classification without hiding project warnings or another
installation. Linux simulation does not establish macOS execution.
The secp256k1 finder fixture is registered when pkg-config is available;
configuration explicitly reports its absence instead of adding that optional
system-library tool as a prerequisite for embedded-library builds.

The old libevent finder/header workaround is omitted because v32 supplies native
HTTP transport and has no libevent dependency. Qt5 adaptation is superseded by
native Qt6. Filesystem/compiler bridges remain native; no compatibility shim is
introduced. Windows GUI resources use configured absolute source paths; actual
Windows/macOS/static/32-bit builds remain separate platform gates.

Configure separate directories for headless and full Qt6 profiles:

```sh
cmake -S . -B build-headless -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_GUI=OFF -DBUILD_BENCH=ON
cmake --build build-headless -j 2
cmake -S . -B build-qt6 -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_GUI=ON
cmake --build build-qt6 -j 2
```

Use a short disposable temporary root for runtime tests: IPC Unix socket paths
have a platform length limit. Isolate Qt settings/runtime files as well:

```sh
roots_test_tmp=$(mktemp -d /tmp/roots-test.XXXXXX)
mkdir -p "$roots_test_tmp/settings" "$roots_test_tmp/runtime"
chmod 700 "$roots_test_tmp/runtime"
TMPDIR="$roots_test_tmp" build-headless/bin/test_bitcoin --report_level=detailed -- \
  -testdatadir="$roots_test_tmp/native" > "$roots_test_tmp/native.log" 2>&1
TMPDIR="$roots_test_tmp" ctest --test-dir build-headless --output-on-failure -j 2
TMPDIR="$roots_test_tmp" QT_QPA_PLATFORM=offscreen \
  XDG_CONFIG_HOME="$roots_test_tmp/settings" XDG_RUNTIME_DIR="$roots_test_tmp/runtime" \
  ctest --test-dir build-qt6 --output-on-failure -j 2
```

Redirect native test output through the shell rather than Boost's `--report_sink`.
On the tested host, that option makes the fork-based `test_LockDirectory` child
fail during Boost reporter cleanup on pristine Core and the foundation alike;
the default reporter sink passes. Preserve complete CTest `LastTest.log` files
when result XML truncates per-test output. Missing external script-assets data
remains an explicit skip, not a passing asset-validation result.

Keep QR enabled for the full GUI profile. A dependency discovery failure is a
gap, not test execution. Record explicit reductions (IPC/USDT/ZeroMQ etc.) and
missing prerequisites with their owners. Do not install packages to clear a gate
without separate authorization. Use disposable explicit regtest datadirs even for
help; never relink executables while dependent tests run.
