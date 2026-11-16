# Product requirements and evidence ownership

This workspace implements the Linux RPC-wallet release of specification v1.0,
sections 1–20. `tests/fixtures/contracts.json` is the machine-readable section,
action, state and AC01–AC20 ownership register. It declares future journeys as
planned; registration is not implementation or passing evidence. The L1.1
model suite proves only the listed exact-money and state contracts. The screen
inventory supplies individual focus/disabled rules and planned state scenarios;
service owners replace typed fixtures with actual behavior. See
[screen-inventory.md](screen-inventory.md) for routing, dialog and evidence rules.

Required profile: node/wallet observations; wallet create/load/restore/descriptor
import; receive/address/UTXO/history workflows; funding, deliberate review,
external/daemon signing, independent finalization and explicit broadcast;
full public-data offline workflows; local preferences/metadata; file/static QR
exchange and tested Linux accessibility. One daemon wallet per wallet tab.
Display-name changes never rename a daemon directory. Local deletion never
implies remote disk deletion; unload is separate. Local file paths have no
automatic meaning to a remote daemon. General daemon configuration/launch and
direct wallet-data access are outside the application boundary.

The native frame contains File/View/Tools/Help, wallet/transaction tabs, the
six-item Transactions/Send/Receive/Addresses/UTXOs/Settings rail, content and
status. Hide the account strip; Add Account explains its missing real service.
Reference sizes are 1200×820 and 960×640. Preserve Sparrow's spatial hierarchy
and workflow sequence while using the published Plan B semantic wallet theme.
Do not turn a fixture, disabled menu or unavailable state into a shipped feature
claim. Required RPC functionality cannot be downgraded merely to close a task.

Each screen/action group has an owning service, support profile, transition,
fixture and acceptance IDs in the register. Every group exercises normal,
empty, loading, error, stale/offline, locked and unavailable data as applicable.
Financial inputs use the exact domain parsers; review preserves full destination
and exact amounts. Hide Amounts extends to charts, diagrams, tooltips,
accessibility and totals. Lock clears sensitive presentation and fences replies.

Local seed/private-key custody, payjoin, payment codes, arbitrary-chain indexing
and privacy construction strategy remain explicit extension contracts. No
mnemonic storage or direct GUI USB/HID signer implementation is authorized.
Fiat/explorer/update providers are opt-in and independently failing; they are
never prerequisites for Bitcoin operations.

For each implemented screen, native production captures must cover reference
and compact sizes, Light/Dark, 100/150% font scale plus 200% stress and reachable
primary actions. Deterministic fixture IDs/fonts/dates accompany pixels. Native
IME, screen readers, dialogs, camera, devices and mixed-DPI evidence needs actual
Linux capability; null-backend frames prove different facts. macOS/Windows are
recorded follow-on obligations, never counted as Linux passes.
