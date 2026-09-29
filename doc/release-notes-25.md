# Release notes for PR 25

- `getmempoolinfo` now reports `"fullrbf": false` when `-mempoolreplacement`
  or `-mempoolfullrbf` select opt-in replacement or disable replacement.
  Previously it always reported `true`.

This changes RPC reporting only. Relay policy and Bitcoin Core consensus
compatibility are unchanged.
