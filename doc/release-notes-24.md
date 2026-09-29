# Release notes for PR 24

- Block templates no longer credit a transaction selected for the coin-age
  priority reserve toward its descendants' feerate. Previously a descendant
  could be selected on its already-included parent's fee, including a
  descendant that pays no fee.

This changes local block-template selection only. It does not change Bitcoin
Core consensus compatibility or relay policy.
