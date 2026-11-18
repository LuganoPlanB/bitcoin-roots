# Test Engine and model harness

`ROOTS_GUI_BUILD_TESTS` adds separate hook-bearing ImGui, null backend, ImKit
core, engine and production draw objects. Every linked GUI test translation
unit shares `tests/engine/config.h`, assertions enabled, the pinned obsolete
API policy and the same ImGui headers. Production `roots-gui` links its own
unhooked objects and never the engine. One ImGui implementation exists in each
executable. Model command tests link neither ImGui nor engine.

The exact engine revision is `7c808483d6f0a69b9d0843d5f4a141f0a9f74a58`.
Its **separate Dear ImGui Test Engine License v1.04** is preserved in
`engine/imgui_test_engine/LICENSE.txt` inside `third_party/test-inputs.tar.gz`. This is not
MIT code. Adoption uses Article 1.1's publicly released OSI-open-source
Derivative Software criterion for this public MIT project, with Article 2.2's
copyright/license redistribution requirement. No paid license or commercial
trial is accepted. Consumers using the engine outside that criterion must
independently meet an eligible license model. Str retains its public-domain/
perpetual-copy grant; capture-disabled stb retains its own MIT/public-domain
notice. Normal production builds with tests OFF do not process these inputs.

`third_party/test_inputs/manifest.json` records each original file hash and
upstream revision/path. The original engine archive SHA256 is
`6aaefa028e00ca7b3fdad73848c83870bd903db61f083ac0d476db88a57b36c4`.
The matching null backend is from ImGui
`367b2c24f399988ddafc0bb4628da0106bcc09be`; selected ImKit tests are from
`986d8d286a74fb7b2f724b9ef5d008af139aa887`. All original bytes/notices are
preserved. `test-inputs-lock.json` also binds the deterministic local archive and
unchanged manifest hashes. Test-enabled configure verifies/extracts all 37 files
only into the external build's `tests/engine/inputs/test-<archive hash>` tree,
then verifies the complete prepared tree. No sibling or cache supplies sources.
Original names, including Str/Str.h, remain unchanged inside the archive; Git
contains the lowercase archive path rather than adapted upstream source names.
No repository linter exemption is needed. Normal tests-OFF builds do not process
this archive. `vendor_test_inputs.py` reproducibly packages an explicitly supplied
original tree only after checking every original manifest hash; it never fetches.
`tests/engine/test_inputs.py` exercises packaging identity and malformed/tampered
input rejection separately from engine interaction assertions. Updates require
explicit acquisition, license/compatibility review, exact manifest refresh and
fresh offline build/test evidence. No reference checkout is a build input.

The runner is adapted from the modern-imgui skill's original headless caller.
`roots_gui_engine_tests FILTER REPORT.xml EXPECTED_COUNT [undrained]` selects
an exact category. It truncates stale reports, bounds frames, requires a drained
queue and positive exact executed/success counts, and checks export existence.
CTest supplies an independent wall-clock timeout. Each process has private
JUnit output; reports include unselected registrations and queued entries,
which are **not executed cases**. The current registry contains90 entries:
88 positive cases in14 independently counted production categories, plus the
two intentional failure/timeout fixtures used by rejection checks. Categories include:

- `roots.preferences`: real production `DrawPreferences`, typed refresh draft,
  hide-balances checkbox, clicked Save/Cancel, keyboard activation, disabled
  invalid Save, rejected save and observed fake requests/state.
- `roots.bootstrap`: real production `DrawBootstrap` and clicked Close result.

The legacy `DrawPreferences` panel is compiled production code with an explicit
public preference request port. The workspace's actual Preferences/Server route
uses the current connection form and bounded authenticated service; the legacy
panel's fake port is not a durability/RPC claim. Save validation also runs as an independent application command;
Cancel restores the saved draft and reports cancellation without a Save request.
The fake accepts/rejects requests explicitly. No test claims metadata durability,
wallet signing custody. The other production categories cover appearance,
components, privacy, shell, documents, commands, semantics, Server, history,
addresses, coins and coin controls. Their typed fixtures and local storage
adapters exercise scopes, acknowledgements, failure recovery and cancelled/late
boundaries. Actual owned-daemon/native evidence remains separate.

Seven ImKit checks reuse theme, design-system, workflow, charts and three API
consumers. The workflow adaptation extracts the pinned notification ordering,
request overflow and workspace/setting request/semantic-tree checks, excluding
editor canvas and unrelated workflows. Original full source remains available
for provenance, but is not built. Design-system uses upstream's allocation
measurement helper only; no Gallery/editor implementation is built. The
remaining six source files compile unchanged. Their nested assertion totals are
not exported upstream; suite identities and outcomes are counted separately.

The integrated domain suite retains nine cases and exact money/identity/state
assertions; fixture/screen contracts remain separately named checks. These
validate planned ownership and command effects, not service implementation.

`tests/engine/check_runner.py` independently verifies deliberate assertion
failure, unknown filter, wrong expected count, an actually queued undrained
selection and a held callback with an actual outer timeout. It checks child
exit, result counts/identities or timeout-entry marker, and settlement. A timeout
probe's killed process does not promise a completed JUnit export. These five
passing rejection checks are not additional successful GUI journeys.

`entry_smoke.py --inject-failure` separately proves failed native smoke
accounting and owned-process cleanup. Attempted checks equal passed plus failed.
Its relocated package and working directory are siblings, so startup exercises
executable-relative font lookup independently of cwd. Run owned native desktop
suites sequentially; the earlier concurrent 144-DPI timeout remains unexplained
and excluded from qualification.

All CTest cases have labels and bounded waits; use `ctest --no-tests=error
--output-on-failure --output-junit <unique-path> -j 2`. Null-backend results
establish simulated interactions, not framebuffer/native input/platform facts.
The native evidence in [native-platform.md](native-platform.md) remains separate,
and the current visual runner reproduces its full bootstrap matrix.
Parent acceptance uses the approved representative native journey and explicit
exclusions described in [render-harness.md](render-harness.md); the historical
full visual cross-product is not required on every parent.

## Wallet theme gates

`roots_gui.theme.contracts` checks every specified light/dark role, actual
source-over contrast, appearance file round trips/failure reporting, fresh DPI
derivation and exact nested ImKit style restoration. `roots_gui.theme.assets`
checks reviewed JSON/SVG hashes, proportions and retained notices.
`roots_gui.engine.appearance` drives the production bootstrap's Light/Dark/System
combo and verifies typed host requests. These join the exact public UI selector.

On Linux, `roots_gui.theme.native_gallery` captures production `DrawThemeGallery`
at 1200x820/960x640, both themes, 100/150/200% scale, and four real input states.
It checks 48 real primary-fill RGB samples against the specified rest/hover/
pressed palette, all primary bounds and teardown. The scrolling inspection area
keeps the bottom action visible at 200%. `roots_gui.theme.native_appearance`
uses a disposable GSettings keyfile under the owned desktop's isolated XDG
directory: live System Light/Dark/Light, independent host reload, and explicit
Dark against a Light desktop. These are three native journeys, not a universal
desktop-provider or accessibility claim. Both native tests join the public
visual selector and retain unique manifests, exact commands/times, source/font/
binary/capture hashes and process-settlement records through `native_runner.py`.

Native gallery focus checks require Continue itself focused and keyboard
navigation visible, after bounded real Tab traversal from the label input;
global `NavVisible` alone cannot pass. The production child shares navigation
scope with the footer. A separate native negative first reaches Continue, then
Tabs away and must return exactly one target-focus assertion failure while
global navigation remains visible. Its observed failure/teardown is retained
separately from the 48 positive scenarios.

For all twelve actual focused captures, `focus_pixels.py` measures the clear
background gap, integrated antialiased ring thickness and contrast directly
from framebuffer scanlines. The checks require a scaled minimum 2px gap,
scaled 2px effective stroke and 3:1 contrast. Retained original collapsed-gap
captures fail this checker. `roots_gui.theme.focus_checker` contains nine
synthetic signal fixtures, including collapsed-gap and wrong-target rejection;
these validate the checker and never count as native application journeys.

## Shared component gates

The public UI selector includes `roots_gui.engine.components`, whose nine
selected Test Engine cases drive the production shared controls and validate
exact command values, Enter/blur, rejected writes, stable selection IDs,
disabled actions and dialog focus return. The native visual selector also
includes `roots_gui.theme.native_components`: 132 actual rendered fixture scenes,
including twelve native full-value Copy requests. See [components.md](components.md)
for contracts, API fallback reasons, fixture boundaries and evidence layers.

The UI selector also runs five `roots.privacy` cases and twenty snapshot
checker probes. The native extension clicks actual privacy/reduced-motion
controls and checks 132 presentation/annotation snapshots, including 36 hidden
scenes with no chart samples. See [amount-privacy.md](amount-privacy.md) for
explicit reveal for review, borrowed-frame lifetime and native accessibility limits.
