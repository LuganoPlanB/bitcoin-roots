# Third-party source notices

## GUI, host, transport and metadata inputs

`ui-lock.json` and `ui-source-manifest.json` record original source paths and
exact identities inside `ui-sources.tar.gz`. Upstream source bytes are unchanged;
regular archive member modes are normalized to 0644.
The separate GUI-local CMake adapters select core targets, not Gallery/editor
modules. Preserve these notices and the full original files in distributions.

| Source | Attribution / terms | Full original notice in UI archive |
| --- | --- | --- |
| ImKit 4.1.0 core | AOKIMOTOHIDE, 2026; MIT | `imkit/LICENSE`, `imkit/THIRD_PARTY_NOTICES.md`; inline source notices |
| Dear ImGui 19297 docking and selected backends | Omar Cornut, 2014–2026; MIT | `imgui/LICENSE.txt`; backend source headers |
| Bundled STB support within ImGui | Sean Barrett and individual contributors; original public-domain/MIT notices | Complete embedded notices in `imgui/imstb_*.h`; covered distribution also retains ImGui MIT license |
| GLFW 3.5.1 | Marcus Geelnard, Camilla Löwy and contributors; zlib/libpng | `glfw/LICENSE.md`; original platform source notices |
| GLFW bundled Vulkan definitions | Khronos Group and retained authors; Apache-2.0 | `glfw/deps/glad/vulkan.h` original attribution; full Apache terms also in shared `licenses/Apache-2.0.txt` |
| GLFW unused example/test support and Wayland protocols | Kim Gräsman getopt BSD-3-Clause; Marcus Geelnard tinycthread zlib; STB/Nuklear public-domain or their embedded alternative grants; individual MIT/X11 Wayland protocol grants | Complete original notices in `glfw/deps/`; no examples/tests/Wayland targets are compiled |
| Inter 4.1 Regular/SemiBold | The Inter Project Authors, 2016; SIL OFL 1.1 | `imkit/assets/fonts/Inter-OFL.txt`; unmodified host-loaded fonts, not sold separately |
| curl 8.23.0 | Daniel Stenberg and contributors, 1996–2026; curl permissive license | `curl/COPYING`, original source notices and `curl/THANKS` |
| curl auxiliary ISC sources | Daniel Stenberg and individually retained authors; ISC, including `curl AND ISC` in disabled SSH adapter | Full embedded grants in `curl/m4/`, related scripts and `curl/lib/vssh/vssh.c`; no Autoconf/SSH dependency is enabled |
| curl unused TFTP test server | Regents of the University of California, Daniel Stenberg et al.; BSD-4-Clause-UC | Complete original grant in `curl/tests/server/tftpd.c`; not compiled/linked. This source includes software developed by the University of California, Berkeley and its contributors. |
| SQLite 3.54.1 amalgamation | SQLite authors; public-domain dedication | Beginning of `sqlite/sqlite3.c`, `sqlite3.h` and `sqlite3ext.h` |
| Xcursor/Xrandr/Xinerama/Xi/Xfixes public headers | Keith Packard, Compaq/HP, Oracle, The Open Group, Digital, Red Hat and other retained authors; permissive MIT/X11 variants with attribution/no-endorsement clauses | Each `x11/notices/<library>/COPYING` plus complete original header notices |

No ImKit Gallery/media, Noto font, editor/show implementation, proprietary
artwork or Java dependency is included. System Xlib/OpenGL/OpenSSL libraries
are not bundled by this source delivery; eventual binary packaging must retain
their applicable notices if redistributed. Linux Mesa/Xvfb execution evidence
does not establish licensing or native qualification for another platform.

Linux password-protected metadata additionally uses the installed native
libsodium SDK/runtime (>=1.0.18), under ISC and its preserved component terms.
No source or binary is bundled here. The observed native provider was Debian
1.0.18-1+deb13u1; see [native ISC notice](licenses/libsodium-ISC.txt) and the
[protected-session contract](../docs/protected-sessions.md). Eventual binary
packaging must retain the complete applicable native-library notices.

## QR code inputs

| Input | Pinned upstream source | License and retained notice |
| --- | --- | --- |
| Nayuki QR Code Generator C 1.8.0 | [720f62bddb7226106071d4728c292cb1df519ceb](https://github.com/nayuki/QR-Code-generator/tree/720f62bddb7226106071d4728c292cb1df519ceb/c) | MIT text in `qr/nayuki/qrcodegen.c` and `qrcodegen.h` |
| quirc C decoder | [f2e4271ae43a15d9263704e07bc86cba5697d74d](https://github.com/dlbeer/quirc/tree/f2e4271ae43a15d9263704e07bc86cba5697d74d/lib) | ISC-style permission and disclaimer in `qr/quirc/LICENSE` and source headers |
| stb_image PNG decoder | [9600522de53973336fa033a289858a4354cbb04c](https://github.com/nothings/stb/tree/9600522de53973336fa033a289858a4354cbb04c) | MIT or public domain, retained in `qr/stb/LICENSE` |
| Blockchain Commons Bytewords alphabet | [4479fb81b2350ae8bafa042a5572b9c64c2c32ca](https://github.com/BlockchainCommons/bc-ur/blob/4479fb81b2350ae8bafa042a5572b9c64c2c32ca/src/bytewords.cpp) | BSD-2-Clause-Patent, retained in `qr/Bytewords-LICENSE` |
| Published PSBT QR vector | [BCR-2020-006, e4a4fbb186e2e7625ccdf7149aec4f0a5adaf850](https://github.com/BlockchainCommons/Research/blob/e4a4fbb186e2e7625ccdf7149aec4f0a5adaf850/papers/bcr-2020-006-urtypes.md#partially-signed-bitcoin-transaction-psbt-psbt) | © 2020 Blockchain Commons, exact wire hex and UR data in `qr/vectors/` |

The production binary compiles these inputs from local source. PNG import
compiles stb_image only with PNG and memory input enabled. The UR transport
implements the indexed subset of
[BCR-2020-005](https://github.com/BlockchainCommons/Research/blob/e4a4fbb186e2e7625ccdf7149aec4f0a5adaf850/papers/bcr-2020-005-ur.md)
and uses the [BCR-2020-012](https://github.com/BlockchainCommons/Research/blob/e4a4fbb186e2e7625ccdf7149aec4f0a5adaf850/papers/bcr-2020-012-bytewords.md)
alphabet. The full bc-ur reference library is used for independent verification
outside the production build.

## Protocol provider source notices

`provider-lock.json` records exact revisions, source URLs, archive/patch hashes
and input hashes. Archives contain upstream source, not compiled libraries.
Every original file and embedded notice is retained byte-for-byte. Local changes
are separate patches, applied only in an out-of-source preparation directory.

| Source | Attribution / terms | Notice location inside libwally archive |
| --- | --- | --- |
| libwally-core 1.6.1 | Jon Griffiths (Blockstream), 2016 and source contributors; MIT | `LICENSE` and individual source headers |
| secp256k1-zkp | Upstream Bitcoin Core/secp256k1 and Elements/Blockstream contributors; MIT | `src/secp256k1/COPYING` and source headers |
| CCAN endian, compiler, build_assert, str/hex | CC0/public-domain dedications | Per-module `src/ccan/ccan/*/LICENSE` and `_info` |
| CCAN base64, SHA256, SHA512, RIPEMD160 | MIT/BSD-MIT, retained author notices | Per-module `LICENSE`, `_info` and headers |
| CCAN TAP | Nik Clayton, 2004; BSD-2-Clause | Complete copyright/conditions/disclaimer at start of `src/ccan/ccan/tap/tap.c` and `_info` |
| Android CPU feature source | Android Open Source Project, 2016; Apache-2.0 | `src/cpufeatures/NOTICE`; full terms also in `licenses/Apache-2.0.txt` |
| Wycheproof test material | Google/contributors; Apache-2.0 | `src/secp256k1/src/wycheproof/WYCHEPROOF_COPYING` |
| Six Autoconf Archive macros | Individual retained copyright holders; GPL-3.0-or-later with each macro's configure-output exception | `tools/build-aux/m4/ax_*.m4`; full GPL terms also in `licenses/GPL-3.0.txt` |

The full Configure Script Exception is in `licenses/Autoconf-exception-3.0.txt`,
from the exact SPDX license-list-data revision recorded in the lock file.
The Autoconf macros are retained source inputs and are unused by this CMake
build; they are not linked into the application. Their original exception text
is preserved, including the Autoconf Configure Script Exception reference in
`ax_subdirs_configure.m4`. No GPL exception is invented or extended here.
The Linux build does not compile the Android CPU feature source. Preserve all
applicable notices in source redistribution and the eventual binary notices.
The application does not assume that all files share libwally's top-level license.

`bips-provider-vectors.tar.gz` preserves BIP174, BIP340, BIP370 and BIP371 source
documents and the BIP340 public test CSV from the pinned bitcoin/bips revision.
BIP174/370/371 are authored by Ava Chow. BIP340 credits Pieter Wuille, Jonas
Nick and Tim Ruffing. The documents state BSD-2-Clause and retain their complete
copyright/license sections. Full BSD terms are reproduced below. These are
public protocol vectors, not user keys or wallet data. Upstream libwally tests
also include explicitly synthetic private-key fixtures; no application API or
metadata store uses them for custody, and tests are not shipped in the executable.

## BSD-2-Clause terms

Applicable copyright and author notices are retained in each original source.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

## Test-only inputs

`test_inputs/manifest.json` identifies exact upstream engine, ImGui null backend
and selected ImKit test bytes. Dear ImGui Test Engine is governed by its separate
v1.04 license, **not** the MIT license covering ImGui core. See the full engine
license at `engine/imgui_test_engine/LICENSE.txt` inside `test-inputs.tar.gz` and the adoption
basis in `../docs/test-harness.md`. Engine third-party Str and stb retain their
own notices. Matching ImGui and ImKit test sources retain MIT licenses in their
respective archive directories. The deterministic local source archive retains
all 37 original source/notice files and their original names/bytes; its separate
lock binds the unchanged manifest and original upstream archive/revision pins.
Configure extracts only into an external build tree and fetches nothing. No
filename/include adaptation or license change is applied. None of the engine is
linked into roots-gui.

## Installed Linux accessibility SDK and protocol

The application-owned adapter links the system GIO/GLib SDK (LGPL-2.1-or-later);
no GLib runtime source is bundled. AT-SPI2 D-Bus public interface signatures
follow GNOME at-spi2-core 2.56.2 protocol documentation. Application declarations
are maintained separately from the unchanged ImKit archive. The reference XML
and source URLs/hashes are retained with execution evidence, not shipped as
vendor code. System-library license obligations remain applicable to distribution.
