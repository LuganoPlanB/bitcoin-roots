# Release notes for PR 20

- GUI peer details now clear optional state fields when a refresh cannot obtain
  current state, preventing stale information from a previously selected peer
  from remaining visible.
- The send dialog now initializes its Replace-By-Fee choice from the wallet
  default and lets users override it for one send. The confirmation reports the
  transaction's effective BIP125 signal; replacement acceptance remains subject
  to network policy.

These interface changes preserve Bitcoin Core consensus compatibility. They do
not change Roots' local mempool, relay, or mining-template policy boundaries.
