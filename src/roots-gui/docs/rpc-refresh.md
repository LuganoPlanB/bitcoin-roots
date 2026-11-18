# Read-only refresh and dated offline observations

Applying a proven connection enables the owner-thread refresh service. Normal
startup remains empty and makes no request. One cancellable worker and one
completion slot admit one batch at a time, with a ten-second total RPC budget.
Successful batches poll after five seconds; failures back off through five,
ten, twenty, forty and sixty seconds. Ctrl+R requests an explicit refresh without
creating a queue. There is no automatic state-changing RPC or mutation replay.

Each request and reply owns the backend session, network genesis, daemon wallet,
document ID, draft revision and lock epoch, plus exact profile, connection
session, cancellation generation and sequence. Only the UI owner adopts a reply.
Rebinding, cancellation, offline and lock transitions fence old completions;
even an uncancellable late source cannot replace another context's observations.
Eight bounded caches retain only their exact identity's last known values.
Locked views expose no cached payload or identity. This is a refresh fence for
later lock integration, not a claim of encrypted storage or an implemented
GUI lock panel.

`getblockchaininfo` and `getblockhash(0)` recheck the network before wallet calls.
Proven wallet methods use the exact URI-encoded daemon name; `getwalletinfo`
checks that name again. `getbalances` categories use preserved JSON numeric
lexemes and exact Money parsing. Trusted, pending and immature are observations,
not a spendable balance or history-completeness claim. No financial payload is
put in the public operational status stream.

Schema-proven `listsinceblock(blockhash, include_removed)` supplies one owned
delta with transactions, removed rows and lastblock. It remains pending until a
consumer acknowledges that exact identity, sequence and lastblock. No new
history batch overwrites it, and the cursor advances only at acknowledgement.
Balance/node polling may continue. Transaction normalization and rendered wallet
history remain their later assigned work; no history rows or wallet documents
are manufactured here. The current connection-level observer uses its own
document identity; future wallet/document consumers bind their full contexts.

Failures preserve each screen's successful value and UTC observation date, with
separate chain, wallet and history errors/freshness. Status shows Offline,
retained height, Last success and fixed secret-free diagnostics. Offline mode
in Status or Server immediately fences refresh and connection-test work; it
issues no new transport call, including Test, Ctrl+R or scheduled polling.
Already dispatched remote work may still finish, but its result is discarded.
Going online allows a fresh batch while retaining the original cached date until
success. Cookie references are resolved per call; authentication failure is
never replayed, and a later scheduled or explicit refresh can read a renewed
cookie. No default node home is searched.

Focused checks are `roots_gui.refresh.models`, `roots_gui.connection.models`,
`roots_gui.rpc.refresh` and `roots_gui.theme.connection_offline_stale`.
They cover held wallet/network/epoch replacements, backoff, cancellation,
history acknowledgement, exact categories, Unicode/slash wallet paths, cookie
renewal, screen errors and offline call suppression. An explicitly supplied
owned Roots regtest daemon exercises initial and incremental empty public-wallet
refreshes. The native Linux journey uses real Ctrl+R and pointer input at
960x640, 150% to inspect a failed refresh followed by dated offline cache.
No pinned Core runtime, macOS/Windows, durable store, hardware or full parent
suite qualification is implied by these focused checks.
