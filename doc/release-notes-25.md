# Release notes for PR 25

- `getmempoolinfo` now reports `"fullrbf": false` when `-mempoolreplacement`
  or `-mempoolfullrbf` select opt-in replacement or disable replacement.
  Previously it always reported `true`.

- Opt-in replacement still permits non-signaling TRUC/v3 conflicts, even
  though `fullrbf` is `false`. Disabling replacement rejects both signaling
  and non-signaling input conflicts, including TRUC/v3 transactions.

This changes RPC reporting only. Relay policy and Bitcoin Core consensus
compatibility are unchanged.
