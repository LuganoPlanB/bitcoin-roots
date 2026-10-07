# Release notes for PR 33

- Port the maintained Roots features directly onto official Bitcoin Core
  v30.3, retaining their semantic history and adapting to the current
  transaction identifiers, fee rates, descriptor wallets and orphanage APIs.
- Keep Roots policy defaults and the distinction between local relay/mining
  policy and block consensus rules. Preserve priority-ancestor accounting
  and configured RBF reporting, including the opt-in TRUC/v3 exception.
- Retain native Core 30 Qt6 and IPC build support, with optional system Qt5
  compatibility. Fix QR label wrapping to use QString's size type on both
  Qt versions; retain guarded sweep and advanced coin-control interfaces.
- Use Core 30's descriptor-wallet requirements. This port does not restore
  the removed legacy BDB build option or add a Roots-specific wallet format.

These are candidate change notes, not a release announcement. Platform
acceptance and canonical integration remain subject to current CI and review.
