# Native visual and boundary runners

The six plan entry points are `roots_gui_model_tests`, `roots_gui_rpc_tests`,
`roots_gui_ui_tests`, `roots_gui_regtest_tests`, `roots_gui_visual_tests` and
`roots_gui_boundary_tests`. Their literal CTest labels are `roots-gui.model`,
`rpc`, `ui`, `regtest`, `visual` and `boundary`. The public dispatcher verifies
exact selected identities and current JUnit outcomes; zero selection cannot
pass. Model/RPC/UI/regtest contracts remain in [rpc-fixtures.md](rpc-fixtures.md).
The additional `roots_gui_applicable_tests` target runs the union of the current
deterministic model/RPC/UI/regtest/boundary cases once, without label-overlap
duplicates. Before execution every dispatcher reconciles all83 current GUI
CTest identities and all six label memberships, then checks selected JUnit
identities/counts. Missing/new cases or label drift fail before execution. This
full aggregate profile requires Linux, tests enabled and the explicitly supplied
owned Roots daemon. Individual suites account for daemon-conditional
registrations from explicit CMake cache inputs, so a node-less configuration can
still run pure model/fixture RPC coverage. Empty regtest selection fails; a
different profile cannot silently pass the full aggregate with missing capabilities.

The aggregate selects61 cases. It explicitly excludes10 visual matrix/baseline
entries, five native accessibility/platform entries and seven separate L3 native
journeys. They are exclusions, not selected skips or passes. Under the approved
lean verification contract, one representative current native journey and
identity-bound unchanged evidence accompany this deterministic aggregate.
Affected native evidence must be refreshed if its executed binary changes;
missing inspected baselines, Core runtime and physical/platform qualifications
remain explicit. The individual full visual/UI/regtest targets retain their
own complete native selections for deliberate qualification runs.

Visual runs use the production `NativeHost`, stock pinned GLFW/OpenGL3 and
production Draw functions. The host owns context, input, resources and thread;
its optional content callback composes actual production panels inside that
frame. The default executable draws the wallet workspace with deliberate public
recovery/connection choices. The older preference-gallery port remains a fixture;
the actual Server menu uses the production connection service.
The visual producer contains no Test Engine hooks. Its fake service records
requests without custody or network effects.

The native selection includes 18 lifecycle scenarios at each of 96/144 Xft DPI
and twelve preference panels at 96 DPI. Both 1200×820 and 960×640 logical
viewports, Light/Dark and 100/150/200% user font scales are captured. The two
bootstrap profiles each produce nineteen raw/client/native-frame captures;
preferences produce twelve. A real XTest Save rejection is rendered before an
intentional failed assertion, preserving a fifty-first actual screenshot.
A separate missing-readback probe returns unavailable with no fabricated image.
These expected rejection probes are distinct from the 48 passing native
scenarios. All owned display groups, native children and temporary auth files
must settle. Desktop suites run serially.

`run_visual.py --capture-candidate` collects an **unreviewed** external candidate;
it does not adopt a baseline. Inspect images and state/viewport/font facts,
then supply a separate external `curation.json` with `method` equal to
`manual-inspection` and `manifestSha256` equal to the reviewed manifest bytes.
The receipt expresses test-baseline curation, never parent review acceptance.
Set `ROOTS_GUI_VISUAL_BASELINE` to that directory for ordinary visual testing.
A missing/unreviewed baseline or missing native capability returns nonzero
unavailable; it cannot pass or silently substitute null-backend frames.

`pixels.py` compares all 36 positive matrix identities using exact RGB channels,
without tolerance or ignored regions. It verifies raw file hashes, dimensions,
DPI, theme, font scale and bundled font/UI input identities. Different candidate
binary hashes are expected across versions; the actual bytes are recorded.
PNG encoding is lossless from real OpenGL/root-window PPM readbacks. No UI
pixels are generated, edited or placed into Git. Numerical comparison details,
original screenshots, manifests, child logs and teardown records remain in
unique external directories. Six independent visual contract probes verify
same-file success, wrong actual baseline pixels, missing capture, zero
selection, unreviewed baseline and injected unavailable native capability.

Actual captures exposed a missing explicit component theme: dark Save text was
white on a white button. `DrawPreferences` now receives the host theme for both
actions and returns their observed bounds. Its label is above a responsive
input so both actions remain reachable at the compact 200% stress profile.
Headless engine cases continue to call this same production function.

Native diagnostics also captured a new sequential X11 stall in the frame-extents
query during minimization. GLFW may wait for a WM reply for an unmapped window;
per-frame observation no longer needs that request. The host caches known mapped
decorations and refreshes them for mapped DPI changes. The lifecycle fixture
asserts that minimized frames retain cached extents without another query.
The stock backend/pin and renderer feature guards are unchanged. Original
failed runs, watchdog stack and exact diagnostic source/binary remain evidence;
the older concurrent timeout lacks a historical stack and stays unattributed.

The boundary runner reuses the existing CMake target/include/actual-header/link/
symbol audit and adds actual Git staged, unstaged and untracked source-path
checks. Both diffs disable rename detection so source and destination paths
remain visible, and their union is deduplicated. GUI paths
and the two planned parent CMake hooks form the source allowlist. An isolated
uncommitted synthetic repository and explicit synthetic link-fragment inputs
exercise fifteen acceptance/rejection cases; each forbidden scope/link child
returns exit 1. Tracked-change fixtures reuse an existing commit's objects in
a private repository without creating commits or checking out application
source. Synthetic text exercises staged additions, modifications, deletions,
rename boundaries and mixed index/working-tree/untracked changes. They never
link Core or mutate the application repository or its index.
The five Test Engine rejection cases also carry the `boundary` label.

Supply `ROOTS_GUI_BOUNDARY_PARENT_OFF`, `PARENT_ON`, `PARENT_QT_OFF` and
`PARENT_QT_ON` (each with the `ROOTS_GUI_BOUNDARY_` prefix) to include the complete
parent matrix. Without them, the standalone audit runs and explicitly reports
unexecuted parent profiles; that is not whole-L1 parent-hook qualification.
Test-enabled GUI configuration creates compile-command metadata locally, without
changing parent cache/compiler options. CMake ≥3.27 requests the codemodel in
the same configure. With older supported CMake, configure once more after the
query file is created before running the boundary audit. No source downloads,
sibling checkout or shared compiler-cache contents are required.

The qualified native environment is authenticated owned Xvfb with real Marco
and Mesa llvmpipe. It proves actual software-rendered frames, native decoration
and observed lifecycle/input. Physical displays, live mixed-monitor DPI, GPU
hardware, IME, accessibility and Windows/macOS remain separately unqualified.
