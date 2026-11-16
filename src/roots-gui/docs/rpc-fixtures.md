# RPC and independent daemon fixtures

These are test-owned fixtures. The application never launches a daemon, and
these scripts implement no production connection service or signing custody.
All network requests target explicit `127.0.0.1` ports; no proxy environment,
external peers, user datadir or mainnet is used.

`tests/rpc/fake_http.py` provides a scripted HTTP server and reference client.
The twelve named cases cover wallet routing, gated out-of-order completion and
stale session identity, authentication, HTTP failure, malformed/duplicate/NaN
JSON, response identity, JSON-RPC error, disconnect/reconnect, request timeout,
exception cleanup, bounded incomplete-request teardown and transcript redaction. Injected gates must be observed,
released and settled. A failed subtest does not create a negative passing count:
failed parent identities and detailed subcase records are counted separately.
The fixture is ready for later production transport tests; it is not evidence
that the production libcurl adapter already exists.

`tests/integration/regtest_tests.py` accepts exact `--roots-daemon` and/or
`--core-daemon` paths and `--output` outside the source repository. At least one
binary is required; no binary autodiscovery or version substitution occurs.
Each supplied profile records its resolved binary SHA256, actual RPC backend
version and full foreground argv. The profile requires v32.1, regtest and zero
peers. Missing Core is reported as an unexecuted profile, never a passed Core
compatibility gate. Earlier read-only RPC/help probes remain separate evidence.

Each profile creates two blank, private-keys-disabled descriptor wallets with
public single-key and 2-of-2 multisig descriptors. It mines 102 synthetic regtest
blocks to mature test outputs, constructs funded unsigned PSBTs, enriches with
`sign=false, finalize=false`, verifies absent signatures/incomplete finalization,
releases only its fixture-owned coin locks and verifies an empty mempool. No
private descriptor, seed or private key is supplied. The synthetic funding is
not transferable to mainnet. Authentication, missing wallet/method, invalid
address, occupied-port startup failure and a post-readiness held timeout are
also checked. Each complete supplied profile executes exactly 41 checks.

Pinned RPC facts matter to the fixture: `listunspent.spendable` is deprecated
and always true, so custody is checked through `private_keys_enabled=false`.
PSBTv2 decoding exposes top-level inputs/outputs rather than an obligatory
network unsigned transaction `tx` member. Neither field is silently interpreted
as signing authority. Core/Roots differences beyond executed profiles remain
subject to the later compatibility matrix.

The reusable `OwnedRegtest` class creates a mode-0700 datadir under validated
session scratch when `GESTALT_SESSION_TMP_CONTEXT` is present. Without that
variable it uses ordinary Python TMPDIR/TEMP/TMP/default temp selection.
Every present context must validate its nonempty fields, TMPDIR and owner marker;
empty, malformed, missing or mismatched managed data fails visibly without
falling back. Selection leaves environment variables unchanged for child
inheritance. Eight scratch regression cases (37 subcases) cover both modes.
It binds a chosen local RPC port, disables listen/discovery/
DNS/Tor/NAT connections, uses an ephemeral cookie and keeps the daemon in the
foreground. It stops/waits only its own `Popen` child, then TERM/KILLs that child
with bounded waits if needed. SIGTERM unwinds the fixture; on Linux a process-
local parent-death signal also requests daemon shutdown if its fixture dies.
External SIGKILL/datadir recovery is not qualified by the ordinary timeout test.
The occupied listener belongs to the test and remains open after the failed
child exits. No process-name/port-based killing is used.

Console/debug logs are retained in full with known cookie/auth bytes redacted;
raw logs and cookies remain in private scratch until owned cleanup removes the
datadir. Transcripts retain only method, request identity, wallet-routing flag,
HTTP status and the explicit false signing/finalization policy. They omit auth
headers, cookies, parameters, raw responses and credential-bearing URLs. The
retained-log absence check does not publish the secret it searches for.

`run_fixture.py` allocates a unique artifact directory for each CTest invocation
and verifies nonzero coherent results and child settlement. `run_suite.py`
checks exact intended identities before executing CTest, preserves old/new
engine reports and writes unique JUnit/log/result files. No zero selection or
missing daemon capability can report success.

| Public build target | Literal CTest label | Current selected cases |
| --- | --- | --- |
| `roots_gui_model_tests` | `roots-gui.model` | Four domain/preference/fixture/screen wrappers; persistence implementation is later work |
| `roots_gui_rpc_tests` | `rpc` | Two wrappers: twelve HTTP cases and eight scratch cases/37 subcases |
| `roots_gui_ui_tests` | `ui` | Two production Draw journeys and seven applicable ImKit consumers |
| `roots_gui_regtest_tests` | `regtest` | One wrapper, only registered with an explicit daemon path; 41 checks per supplied backend |
| `roots_gui_visual_tests` | `visual` | Two wrappers: actual native matrix/comparison and six visual rejection contracts |
| `roots_gui_boundary_tests` | `boundary` | Actual source/build audit, seven rejection probes and five engine rejection checks |

Namespaced `roots-gui.rpc/ui/regtest` label aliases are also supplied. Configure
`ROOTS_GUI_TEST_ROOTS_DAEMON` and/or `ROOTS_GUI_TEST_CORE_DAEMON` with explicit
absolute paths; defaults are empty. Without one, the regtest public target
fails selection instead of creating a skipped or counterfeit passing case.
The fake/regtest fixtures require authorized loopback sockets: the build-only
`offline_exec` AF_INET denial is intentionally unsuitable for those runtime
suites. Configure/build sources remain local/offline; network capability is not
broadened by the fixture. Use `cmake --build <build> --target <public-target> -j 2`
and retain the reported unique result directory. Never relink a tested binary
while a suite is running. Native desktop suites remain sequential and separate.

See [render-harness.md](render-harness.md) for the completed visual/boundary entry points and explicit baseline/matrix inputs.
