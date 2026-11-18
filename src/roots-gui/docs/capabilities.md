# Capability and unavailable-state contract

Capability is observed per method/argument/wallet/network, never inferred from
the daemon's product name. Available has no disabled reason; unavailable has a
typed reason and a user-facing explanation. Unknown is distinct from zero,
unspent, complete or available. Fixture registrations are planned coverage.

| Capability | Release classification | Owner / unavailable explanation |
| --- | --- | --- |
| Chain, node, balances, history, addresses, coins, fees | Required RPC | RPC observations; method/argument unsupported, wallet disabled/unloaded, pruned data, authentication, stale/offline each remain distinct |
| Create/load/restore/import wallet | Required when daemon supports corresponding operation | Wallet service; remote-path distinction and stateful cancellation reconciliation |
| Receiving offline | Required bounded public-data route | Receive service; only a previously reserved pool with visible provenance, otherwise no reserved offline address |
| Unsigned offline construction, PSBT0/2 inspect/merge/export/finalize | Required for supported policies | Standalone protocol adapter; exact policy/provider support, never Core-internal fallback |
| Signing | Required daemon/external PSBT routes | Explicit approval; daemon walletprocesspsbt true/false or validated external import; no local private custody |
| Hardware | Airgapped file/QR + supported daemon signer RPC | Signer service; missing daemon signer differs from disconnected device; direct GUI USB/HID outside profile |
| Accounts | Extension | MissingAccountService; hide account strip and explain Add Account |
| Indexed spending/history completeness | Extension | Missing history index means Unknown, not unspent/zero |
| Privacy optimization | Extension | Missing construction strategy; Efficiency remains required RPC behavior |
| Local keys, payjoin, payment codes | Extension | Missing dedicated service; negotiated transactions re-enter review |
| Fiat, explorer, updates | Optional opt-in | Permission/provider failure does not disable Bitcoin workflows |
| Native platform | Linux required; macOS/Windows follow-on | Distinguish source-only, compiled, native-tested, unavailable; no cross-platform claim from Linux |

The model's typed reasons include unsupported method/argument, wallet disabled,
pruned data, offline, privacy locked, missing protocol/account service, no direct
device transport and unknown. The [connection service](server-connection.md)
maps transport/authentication errors separately without leaking credentials
in URLs, logs, captures or session files.
Inspection/enrichment always supplies explicit sign=false/finalize=false;
signing uses true/false. This repository's dangerous-public-key RPC gate must be
probed: public descriptor/key inventory is not assumed available by default.
No capability probe may allocate addresses, import wallets, sign or broadcast.

The connected-wallet workspace now implements authenticated connection testing,
bounded read-only refresh, exact history, public descriptor-derived addresses,
coin inventory and local public metadata/recovery. Public descriptor enumeration
never allocates a receive/change index. Local protection/GUI privacy lock, daemon
encryption and external daemon coin locks are separate capabilities. Scoped
Freeze and temporary draft reservation observations require acknowledged
metadata plus current wallet/chain/lock evidence; missing evidence stays Unknown.

Send Selected pins every exact outpoint and reason into an inspectable draft.
Explicit fresh preflight may acquire an owned GUI-instance reservation; it is
not daemon exclusion or funding approval. Wallet creation/import, receiving
allocation, construction/funding, signing and broadcasting remain later services
and report unavailability. The classifications above are release requirements,
not claims that those future services are already implemented. Explorer access
requires deliberate permission; no refresh/restored draft replays an action.
