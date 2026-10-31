# Roots policy input compatibility

This v32 port implements scoped startup parsing, local standardness, carrier
extra weight, sub-dust modified fees and replacement/TRUC/reuse controls over
the native graph. Dynamic dust publishes synchronized admission snapshots;
mining controls enforce serialized size, priority area and native weight limits. Read/write
text configuration and effective-policy RPC integration also remain pending.
Native Core CLI, config and JSON selection are retained. See the
[graph fee contract](subdust.md) for the active accounting boundaries.

Contract: `policy-parsing`. Adapted from Roots source
`e5445b7c96193c7ec141705aac5ba96651e0ee30` with independent oracle corrections
from `9aaf62146fae1a89d65121468633d5c1701699e9`. Jaromil supplied the carried
implementation and contracts; upstream Bitcoin Core attribution is retained.

The ranges below describe parsing and active mining defaults. Local policy
never changes consensus block validity. Native v32 reserved-weight and maximum
weight validation remains strict: incompatible reserve/maximum settings fail
rather than silently producing an empty template.

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

Dynamic dust initializes after successful chainstate loading and estimator
construction, then updates every 15 minutes. The block-target mode uses the
native block-policy long-horizon estimate at an 80% success threshold; the
mempool mode uses actual fees at the selected decimal-kvB policy-size position.
The configured `dustrelayfee` is both floor and missing-estimate fallback.
Scaling uses thousandths, rounding down with signed saturation. The synchronized
rate reaches standardness, new admission deficits and node/wallet interfaces;
resident deficits remain immutable. See [the lifecycle contract](subdust.md).

Keep these mode/list forms and their existing precedence semantics:

- `mempoolreplacement`: Boolean alias, or `fee`, `optin`, `-optin` tokens
  separated by `,` or `+`. `0` disables input-conflict replacement; empty/`1`
  enables fee-based input-conflict replacement. Repeated recognized tokens
  retain their current meaning;
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

Replacement selection above governs conflicting prevouts. With
`spkreuse=conflict`, script-only conflicts using independent inputs still face
native replacement economics even under Never (`mempoolreplacement=0`) or
OptIn with a non-signaling original. A transaction that also conflicts on a
prevout must pass the input-conflict mode gate. This preserves the released
runtime behavior; older catalog prose saying Never disables all replacement
was broader than its implementation. The current help makes the scope explicit.

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
