# Roots policy input compatibility

**v31 candidate status: L7 local policy, L8 graph/dynamic dust and L9 mining consumers implemented.**
Carrier cost, sub-dust modified fees, replacement/TRUC modes and script reuse
have native consumers. Dynamic dust publication is synchronized. Serialized
size and coin-age priority controls now affect local templates, with the same
scoped parsing grammar and no staged-control warnings.
Only integrated
Roots `confrw` precedence awaits L12; CLI, ordinary config and JSON settings
passed the L7.1 startup matrix. Candidate tests are
[`policy_args_tests.cpp`](../../src/test/policy_args_tests.cpp) and
[`feature_policy_args.py`](../../test/functional/feature_policy_args.py).
L7.2 carries the source
[`policy_args` fuzz target](../../src/test/fuzz/policy_args.cpp): Boolean and
integer independent grammar oracles, trailing-junk rejection, quarter-vbyte
canonical round trips, dynamic-mode syntax round trips, JSON types and raw
negation. Clang 19 ASan/UBSan/libFuzzer executed 103,911 inputs over 121 seconds
with seed 31 and no findings. This bounds the reported campaign, not all inputs.
Historical paths/results below are source evidence.

**Contract ID: policy-parsing.** Scope: startup input for the Roots-added local
policy controls listed below. This contract supplements `runtime-config` and the
policy contracts in [the catalog](../roots-features.md). It does not change
admission rules, block validity, or defaults. Scoped startup validation enforces
the decided grammar; the baseline column records the former permissive behavior
and explains migration.

## Scope, defaults and ranges

| Options (without leading `-`) | Default | Accepted effective value |
| --- | --- | --- |
| `subdustfeepenalty`, `rejectparasites`, `acceptunknownwitness`, `datacarrierfullcount`, `permitbareanchor` | `1` | Boolean below |
| `rejecttokens`, `permitbarepubkey`, `permitbaredatacarrier`, `acceptnonstddatacarrier` | `0` | Boolean below |
| `maxscriptsize` | `1650` bytes | Integer, 0 through 4,294,967,295 |
| `bytespersigopstrict` | `20` bytes | Integer, 0 through 4,294,967,295 |
| `maxtxlegacysigops` | `2500` sigops | Integer, 0 through 4,294,967,295 |
| `minrelaycoinblocks` | `0` coin blocks | Integer, 0 through 9,223,372,036,854,775,807 |
| `minrelaymaturity` | `0` blocks | Integer, 0 through 2,147,483,647 |
| `blockmaxsize` | Unset: 4,000,000 serialized-byte cap, with 1,200,000 default weight cap | Integer, 0 through 4,000,000; retain existing template clamping |
| `blockprioritysize` | `100000` bytes | Integer, 0 through 9,223,372,036,854,775,807 |
| `datacarriercost` | `1` vbyte/data byte | Fixed point, 0 through 1,073,741,823.75; round up to quarters of a vbyte |
| `dustdynamic` | `off` | Existing structured grammar below |
| `mempoolreplacement` | `fee,-optin` (full RBF) | Mode/list below |
| `mempooltruc` | `enforce` | Mode/list below |
| `permitephemeral` | anchor allowed, dust/send disallowed | Mode/list below |
| `spkreuse` | `allow` | `allow`, `conflict`, or Boolean alias |

Inherited Core options, including `bytespersigop`, `datacarrier`,
`datacarriersize`, `permitbaremultisig`, `mempoolfullrbf`, `blockmaxweight`,
`blockreservedweight`, and fee amounts, keep their existing parsers. Changing
Roots defaults for an inherited option does not bring its parser into this scope.
Peer, wallet, RPC and runtime options are outside this contract.

**Baseline help conflict:** `blockmaxsize` help advertised `300000` as its
default, but initialized template options leave the serialized cap at 4,000,000
when it is unset. Omitting both size and weight options retains the conservative
1,200,000 weight cap. Explicitly setting only `blockmaxsize` makes weight
unrestricted up to the consensus maximum; setting only `blockmaxweight` leaves
the serialized cap at 4,000,000. Preserve these effective defaults and option
interactions. Updated help reports the actual unset serialized cap.

## Grammar and migration decisions

A Boolean accepts exactly `0`, `1`, or the empty string. The empty string,
including an elided CLI flag, means true. Keep `-noOPTION`, `-noOPTION=1`, and
`-noOPTION=0` (the last means true); explicit empty negation means false. A
negated value must itself be empty, `0`, or `1` before conversion, so malformed
negations cannot hide behind the inherited coercion. Other numbers, signs,
leading zeros, whitespace, textual `true`/`false`, fractions, and suffixes are
errors. JSON Boolean settings retain their corresponding Boolean meaning.

Integers consume the complete ASCII decimal string, with an optional `+` or `-`
sign and one or more digits. Leading zeros are allowed. The parsed value must
be nonnegative and fit the range above; `-0` remains zero. Empty/elided values,
whitespace, fractions, suffixes, and overflow are errors. The upper bounds
prevent narrowing/wrapping or saturating an out-of-range value; coin-block and
priority-size controls retain the nonnegative range of the former signed
64-bit getter rather than silently expanding it. Existing `-noOPTION` and
`-noOPTION=1` retain zero; `-noOPTION=0` retains one, except fixed-point cost,
where its existing string conversion also means a cost of one vbyte. Negated
values are checked as Booleans before conversion.

Carrier cost retains the existing whole-value `ParseFixedPoint` grammar:
optional minus sign, integer part `0` or a nonzero digit followed by digits,
optional decimal point followed by at least one digit, and optional `e`/`E`
exponent with optional sign and at least one digit. The result must be exactly
representable in hundredths and within the range above. Thus `1.2300`, `1e0`,
`1e-2` and `-0` remain valid; `+1`, `01`, `.5`, `1.`, `0.001`, whitespace,
trailing junk and arithmetic overflow remain invalid. There is no new ban on
exponents or redundant fractional zeros. The existing fixed-point parser's
bounded mantissa/exponent rules remain authoritative, including for zero.

`dustdynamic` retains `off`/`0` or
`[<multiplier>*]target:<blocks>` / `[<multiplier>*]mempool:<kvB>`.
The multiplier uses the same fixed-point grammar with thousandth precision,
scaled range 1 through 2,147,483,647; default multiplier is 3. Target is an
unsigned decimal integer from 2 through the existing estimator horizon.
Mempool position is a signed decimal integer from 1 through 2,147,483,647.
Their existing `ToIntegral` grammar rejects a leading `+` and accepts leading
zeros. Empty values, extra separators, suffixes and unknown modes are errors.
`-nodustdynamic` and `-nodustdynamic=1` retain `off`/`0`; the inherited
double-negative `-nodustdynamic=0` yields `1` and remains an invalid mode.
Validate the raw negated Boolean value before conversion.

Keep these mode/list forms and their existing precedence semantics:

- `mempoolreplacement`: Boolean alias, or `fee`, `optin`, `-optin` tokens
  separated by `,` or `+`. `0` disables replacement; empty/`1` enables fee-based
  replacement. Repeated recognized tokens retain their current meaning;
  the last `optin`/`-optin` token wins. Preserve the existing interaction with
  inherited `mempoolfullrbf`, including its contradiction warning.
- `mempooltruc`: Boolean alias, or `reject`, `accept`, `enforce`, `optin`,
  `-enforce`, `-optin`, `0` tokens separated by `,` or `+`. Empty/`1` means
  enforce. `0`/`reject` means reject; enforce takes priority over accept;
  later assignments to the same flag win. Preserve all recognized aliases.
- `permitephemeral`: Boolean alias, or `anchor`, `dust`, `send`, `reject`, `0`
  tokens with an optional `-` prefix, separated by `,` or `+`. Empty/`1` enables
  all three exceptions, `0`/`reject` disables them. Preserve existing implied
  flags and last assignment to each flag, including signed recognized tokens.
- `spkreuse`: `allow`/`1`/empty allows reuse, `conflict`/`0` rejects conflicts.

Lists must contain recognized nonempty tokens only; unknown tokens and leading,
trailing, or doubled separators are errors. Whitespace is not token syntax.
Booleans used as aliases must consume the entire string; `2` and `1junk` are
errors. Negation retains each mode's existing Boolean alias behavior.

| Input class | Baseline behavior | Decided behavior / migration |
| --- | --- | --- |
| Omitted option | Built-in default | Unchanged |
| Empty/elided Boolean or list | Enables Boolean behavior | Unchanged; use `=0` to disable explicitly |
| Empty/elided integer | Converts to zero | Error; write `=0` |
| `+1`, `01`, `-0` integer | Numeric conversion | Retain within range |
| `+1`, `01`, `-1`, `2` Boolean | Nonzero becomes true | Error; write `=1` or `=0` |
| CLI value containing outer whitespace | Numeric getters trim; carrier cost rejects | Error for scoped values; remove whitespace |
| Config whitespace around `=`/value | Config reader trims before parsing | Retain config formatting; parser sees trimmed value |
| `12junk`, `1.5` integer | Prefix becomes 12 or 1 | Error; write the intended whole integer |
| `junk`, `false`, `0.5` Boolean | Converts to false | Error; write `=0` |
| Signed overflow | Getter saturates, then may narrow/wrap | Error; choose an in-range value |
| Negative nonzero integer | May wrap in unsigned destinations | Error; use zero or a supported positive value |
| Unknown list token / empty token | Ignored, possibly changing the selected mode | Error; remove or correct token |
| Unknown `spkreuse` string | Selects conflict rejection | Error; write `conflict` |
| Exact fractional/exponent carrier cost | Whole-value parse | Unchanged; not a partial parse |
| Malformed negated value | Converts permissively before policy sees it | Error; use an explicit `0` or `1` |

These incompatibilities replace silent guesses with actionable startup errors.
They do not authorize narrowing valid documented policy semantics or changing
how accepted values influence transactions or templates.

## Sources, precedence and diagnostics

Preserve `runtime-config` precedence: forced settings, CLI, read/write config,
JSON settings, network config section, default config section, built-in default.
For scalar options the last CLI occurrence and first config occurrence win after
inherited negation processing. Validate the selected effective value; shadowed
ordinary values need not cause errors. Raw negations must be checked before the
argument manager converts them, even when subsequently shadowed. No change to
how config comments or outer whitespace are removed is intended. Config lines
without `=` remain syntax errors; CLI elision follows the option grammar above.

Invalid scoped effective values must produce a startup error naming the option,
the offending value, and its accepted grammar/range. Preserve the existing
`Invalid -datacarriercost value: '…'` diagnostic prefix and the specific
`dustdynamic` failure context, adding useful bounds without disguising the
reason. Errors must not report unrelated configuration values or secrets.

## Baseline evidence and remaining acceptance gates

Characterized at `f66eca4ae466e198fecd66cac22a0033918b12ec` (same production
parser as source `17e1d484aba2a881be03b8928fba3c0125e027a2`). Paths:
argument conversion (`src/common/args.cpp`, source reference),
config reading (`src/common/config.cpp`, source reference),
merging (`src/common/settings.cpp`, source reference),
mempool options (`src/node/mempool_args.cpp`, source reference),
startup options (`src/init.cpp`, source reference),
template options (`src/node/miner.cpp`, source reference), and
fixed-point conversion (`src/util/strencodings.cpp`, source reference).

Focused baseline evidence: `getarg_tests` (10 cases),
`feature_config_args.py`, and a 160-case isolated regtest startup matrix exercising
Boolean/integer/cost classes, list aliases/junk, CLI/config pairs, repeated
options, section precedence, negation and bounds. Configuration trims outer
whitespace; CLI does not. The cost cases verify last CLI / first config /
CLI-over-config / network-over-default selection with invalid shadowed values.
The matrix produced 127 clean startups/shutdowns, 30 expected startup diagnostics,
and the three existing runtime aborts described below. All 160 outcomes matched
the source-derived characterization; an expected rejection is not a test failure.

**Historical source runtime gap (repair authorized for L8):** non-off `dustdynamic` modes can parse successfully but
abort startup because the mempool is constructed without its required scheduler.
Parser acceptance does not prove these modes operate. This contract records that
baseline failure; The user authorized a bounded L8 runtime repair with node-owned scheduling,
retry/shutdown lifetime safety, unavailable-estimate floor fallback and safe
multiplier scaling. L8.2 implements that authorized repair; it is not an L7
parser claim. See [sub-dust runtime semantics](subdust.md).

Acceptance: policy_args_tests.cpp (`src/test/policy_args_tests.cpp`, source reference)
`boolean_values`, `integer_boundaries`, `carrier_cost_compatibility`, `mode_tokens`,
`raw_negation_is_scoped`, `defaults_and_mode_semantics`,
`settings_types_and_precedence`, and `dynamic_dust_grammar_without_runtime_claim`
cover parser boundaries, exact weight rounding, defaults, types and aliases.
feature_policy_args.py (`test/functional/feature_policy_args.py`, source reference) `run_test`
covers isolated CLI/config startup parity, shadowing, negation and unchanged
inherited Core parsing. Dynamic-dust non-off grammar is unit-tested without
claiming a working startup. Configuration and affected
carrier/sub-dust/dust/replacement/TRUC/template policy suites remain regression
gates.
Retirement requires an upstream parser with the same grammar, default values,
precedence, diagnostics and covered policy behavior. No PR number is known;
a release note must be added when one is assigned.

## Scoped fuzz evidence

`policy_args` in policy_args.cpp (`src/test/fuzz/policy_args.cpp`, source reference) exercises
the production option validator, carrier-cost and dynamic-dust parsers, and raw
Boolean negation. Its independent bounded decimal oracle checks complete integer
consumption and range limits; Boolean grammar, rejection of appended junk,
canonical cost/dynamic round trips and JSON setting types have explicit
invariants. The entire input is a value, retaining embedded NULs; only the argv
negation check skips NUL-containing values because argv strings cannot carry them.
Dynamic parsing does not construct a dynamic mempool or establish runtime support.

`subdust_fee_penalty` exercises actual admission and entry fee accounting against
a wide-integer oracle; its supported arithmetic domain and remaining gates are
in [the sub-dust contract](subdust.md). Both targets use the existing fuzz registry.

Use the existing `libfuzzer` preset when its compiler runtime is installed. A
sanitizer corpus replay can also use the supported standalone fuzz runner:

```sh
cmake -B build_fuzz_policy -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ \
  -DCMAKE_BUILD_TYPE=Debug -DBUILD_FOR_FUZZING=ON -DSANITIZERS=address,undefined \
  -DENABLE_WALLET=OFF -DBUILD_GUI=OFF -DENABLE_IPC=OFF
cmake --build build_fuzz_policy -j4
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 FUZZ=policy_args \
  timeout 120 build_fuzz_policy/bin/fuzz /path/to/local/policy_args
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 FUZZ=subdust_fee_penalty \
  timeout 120 build_fuzz_policy/bin/fuzz /path/to/local/subdust_fee_penalty
```

Keep inputs and artifacts outside Git. Capture the exact source revision and dirty
diff, binary/configuration, generator, PRNG seed, input counts, wall time and exits
for every campaign. The preservation run at base `b60a6e04` plus the L5 test diff
used GCC 14 ASan/UBSan, Python PRNG seed `310503`, 52 parser boundary spellings
plus 5,000 mutations, and 225 arithmetic boundaries plus 1,000 generated cases.
Both 120-second replay budgets completed: 5,052 parser inputs in 34 seconds and
1,225 arithmetic inputs in 4 seconds, without sanitizer reports. These are bounded
smoke results, not proof of exhaustive coverage.

Parser seeds include empty/Boolean/signed/overflow integers, exact and rounded
carrier costs, exponents, embedded NUL and trailing junk, malformed lists, JSON
types, dynamic multiplier/target bounds and a 4,096-digit value. Arithmetic seeds
combine rates `0`, `1`, `3000`, `INT64_MAX/2` and `INT64_MAX`; actual fees `0`,
`1000`, `1000000`; deltas `-MAX_MONEY`, `0`, `MAX_MONEY`; zero-valued P2TR/P2A/
OP_RETURN, mixed sixteen-output fixtures and sixteen above-default-dust outputs.
Arithmetic inputs consume integers from the end using `FuzzedDataProvider`:
rate (8 bytes), fee (3), delta plus `MAX_MONEY` (7), output count minus one (1),
then each output value (2) and script index (1); integers are little endian and
the chunks are concatenated in reverse consumption order.

The installed Clang 19 lacked compiler-rt/libFuzzer/ASan archives; the compile
probe failed before testing. GCC sanitizer compilation and execution succeeded.
The executed campaigns therefore used corpus replay, including generated
mutations, with no coverage-guided engine. A coverage-guided libFuzzer campaign
remains an explicit evidence gap, and cannot be claimed from these replay counts.

## Candidate split and current gates

L7 carries complete-token/negation/type/cost/mode syntax and local standardness,
carrier budgets, fixed dust and relay age/maturity. The native v31 cluster limits
and typed script flags remain intact. Mode-resolution/enforcement tests belong
L8, and template-normalization tests belong L9; their source assertions are
retained as explicit owning-theme gates, not represented as L7 passes.

`feature_policy_args.py` retains independently runnable CLI/config/JSON cases;
only its historical text read/write-config integration cases defer to L12.
`policy_args_tests` retains parser boundaries and local/ephemeral defaults.
Dynamic-dust grammar is tested without a runtime support claim.
