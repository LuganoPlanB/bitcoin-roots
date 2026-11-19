# Server connection and capability negotiation

Tools → Switch Server opens the production Server form directly. File →
Preferences (Ctrl+,) opens General and provides a Server route. Private Index
and Public Index show the missing history index adapter and do not change the
working RPC profile. Returning to Direct RPC requires a new Test if the prior
proof was retired.
Select an explicit endpoint, credential reference, expected network genesis and
optional exact daemon wallet name. TLS verifies peer and hostname; an explicit
CA, SOCKS5 with proxy DNS and HTTP CONNECT are supported. The current desktop
composition supplies cookie credentials. Enter the exact absolute path or use
the local file chooser when available. Password authentication requires a
native credential provider. Normal startup reads no cookie, searches no node
home and makes no RPC call. A direct onion profile is rejected before credential
resolution. SOCKS5 delegates DNS, and HTTP CONNECT delegates origin routing.

Test Connection starts one cancellable worker with a 30-second total budget.
Read-only `getnetworkinfo`, `getblockchaininfo`, `getblockhash(0)`, `help` and
`getopenrpcinfo` establish identity and support. `getwalletinfo` checks an
explicit wallet; a schema-proven `listdescriptors(private=false)` checks public
inventory acceptance. No probe changes policy, allocates addresses, imports,
signs or broadcasts. Only the UI owner thread polls completions and applies
results. The working profile remains active while a replacement is tested.

Authentication, network identity, initial download, pruning, wallet availability,
scanning and wallet flags remain separate. An unavailable wallet does not invent
a zero balance. A generic -32601 is described as wallet RPC unavailable, rather
than diagnosing a disabled-wallet build. Initial download complete does not
claim continuously current synchronization. Public-probe rejection does not
infer an exact policy cause from branding or an ambiguous error code.

Current OpenRPC metadata proves methods and named arguments, including direct
nested option properties. Absent options and unproven union branches remain
unavailable. If OpenRPC is unsupported, help proves registration only; required
arguments stay unproven. Catalogs, schemas and option sets are bounded. Metadata
support is distinct from signing authorization, lock state or a complete
spending path. Later workflows must enforce those constraints independently.

Apply accepts only the exact unchanged successfully tested profile/genesis/wallet.
Edits and cancellation invalidate proof; late results cannot restore it. Apply
prepares the replacement before an atomic swap, advances the connection session
and issues no RPC. A failed test or allocation leaves the working profile intact.
Profiles are in memory here; durable storage belongs to a subsequent work
package. Applied connections use [bounded refresh](rpc-refresh.md); offline mode
in this form or Status disables Test and refresh, cancels pending workers and
retains dated observations. Going online retains the working profile and does
not restore cancelled Test proof. Backend help, descriptions, errors and descriptors do not enter
operational diagnostics.

The shared ImKit dialog/forms/actions retain native input and stable IDs. The
active dialog retires workspace navigation from the published accessibility
packet. Its body scrolls at compact sizes while Test Connection, Apply and Cancel
remain reachable. Whole-form failures use visible wrapped text because pinned
ImKit ValidationMessage only decorates the preceding item.

Focused tests: `roots_gui.connection.models`, `roots_gui.rpc.capabilities` and
`roots_gui.engine.server`. Source-bound Roots/Core metadata fixtures distinguish
method/argument support from branding. An explicit owned Roots daemon also
exercises actual node-only and public-only wallet profiles. No pinned Core runtime
pass is claimed without a supplied exact binary. One actual 960x640, 150% Linux
journey covers Apply and a failed replacement retaining the working connection.
Other platforms and native secure-store qualification remain follow-on work.
