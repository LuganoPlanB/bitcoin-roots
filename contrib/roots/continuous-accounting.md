# Continuous adaptation accounting

Every post-methodology change must account for each changed path and hunk in
the same review: `add`, `update`, `absorb`, or an explicit `exempt` decision.
Each entry names one concrete path and hunk, adaptation, provenance, risk,
dependencies, replay impact, tests, an exact-path scope, and rationale.
Wildcards and catch-all units are not permitted. Reviewers reject an
undocumented hunk.

For an embargoed security fix, record only `embargoed`, an opaque bounded
confidential tracking reference, a real `YYYY-MM-DD` reconciliation deadline,
and `pending` reconciliation state during the embargo. Before public release,
reconcile every such hunk into an ordinary add/update/absorb/exempt entry with
its eventual provenance, risk, dependencies, replay impact, and tests. The
public manifest must never contain confidential vulnerability details.

## Pull-request transport record

The read-only pull-request gate reads the candidate's
`contrib/roots/continuous-accounting-pr.json` as data and validates every atom
other than that one transport file with the same contract. The transport file
is deliberately excluded from its own atom set: requiring it to contain its
own digest would be circular. This exception is fixed to that exact path and
does not exempt any other path, hunk, generated output, or documentation
change. The gate executes only trusted-base validator code; candidate scripts
are never imported or executed.

Run the read-only gate with full immutable commit IDs:

```sh
python3 contrib/devtools/roots-continuous-accounting.py \
  --repository /absolute/path/to/roots \
  --base <40-hex-base> \
  --head <40-hex-head> \
  --record /absolute/path/to/accounting.json --public-release
```
