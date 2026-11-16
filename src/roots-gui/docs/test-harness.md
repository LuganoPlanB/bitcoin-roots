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
which are **not executed cases**. Current successful engine categories are:

- `roots.preferences`: real production `DrawPreferences`, typed refresh draft,
  hide-balances checkbox, clicked Save/Cancel, keyboard activation, disabled
  invalid Save, rejected save and observed fake requests/state.
- `roots.bootstrap`: real production `DrawBootstrap` and clicked Close result.

`DrawPreferences` is compiled production panel code with an explicit public
preference request port. It is not yet routed by the bootstrap or backed by
persistence/RPC. Save validation also runs as an independent application command;
Cancel restores the saved draft and reports cancellation without a Save request.
The fake accepts/rejects requests explicitly. No test claims metadata durability,
wallet services, signing custody or an implemented preferences menu route.

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
