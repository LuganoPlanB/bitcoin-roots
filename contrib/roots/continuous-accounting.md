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

Run the read-only gate with full immutable commit IDs:

```sh
python3 contrib/devtools/roots-continuous-accounting.py \
  --repository /absolute/path/to/roots \
  --base <40-hex-base> \
  --head <40-hex-head> \
  --record /absolute/path/to/accounting.json --public-release
```
